// Modern UI (0.7): dark cards, smooth Inter text, Tabler icons, a status bar
// and a left icon rail. Everything is drawn into the 320x240 RGB565
// framebuffer; the device pushes it to the LCD, the simulator saves PNGs.
// theme=retro in settings.ini switches to the 0.6 look (cui_render_retro.cpp).
#include "cui_state.h"
#include "cui_ui_assets.h"
#include "cui_popup_art.h"
#include "cui_names.h"
#include "qr/qrcodegen.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace cui {

// Optional map painter (device: SD tiles). Returns false if it drew nothing.
static MapPainter g_map = nullptr;
void setMapPainter(MapPainter p) { g_map = p; }

namespace {

constexpr int RAIL = 44;              // left icon rail width
constexpr int X0 = 50, XR = 314;      // content area
constexpr int CW = XR - X0;           // 264
constexpr int TOP = 26;               // below the status bar

const char* kTitle[PG_COUNT] = {"Home", "Heard", "Signal", "Channel", "Area", "Radios", "Status"};
const uint8_t kRailIcon[PG_COUNT] = {I_HOME, I_LIST, I_CHART, I_WAVE, I_MAP, I_NET, I_INFO};

// ---- helpers ---------------------------------------------------------------
uint16_t dimc(uint16_t c, int keep) {  // darken a color: keep 0..256
  int r = ((c >> 11) & 31) * keep >> 8, g = ((c >> 5) & 63) * keep >> 8, b = (c & 31) * keep >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
uint16_t tint(uint16_t c) { return dimc(c, 60); }             // icon-square background

uint16_t kindCol(Kind k) {
  switch (k) {
    case K_ANNOUNCE: case K_PATHRESP: return T.orange;
    case K_DATA: case K_RESOURCE:     return T.blue;
    case K_LINK: case K_LINKCLOSE: case K_KEEPALIVE: return T.violet;
    case K_PATHREQ:                   return T.yellow;
    case K_PROOF:                     return T.green;
    default:                          return T.sub;
  }
}
uint8_t kindIcon(Kind k) {
  switch (k) {
    case K_ANNOUNCE: case K_PATHRESP: return I_BCAST;
    case K_DATA: case K_RESOURCE:     return I_MAIL;
    case K_LINK: case K_LINKCLOSE: case K_KEEPALIVE: return I_PLUG;
    case K_PATHREQ:                   return I_ROUTE;
    default:                          return I_RADAR;
  }
}

void fmtAgo(uint32_t ms, char* o, size_t n) {
  const uint32_t s = ms / 1000;
  if (s < 60) snprintf(o, n, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(o, n, "%lum", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(o, n, "%luh", (unsigned long)(s / 3600));
  else snprintf(o, n, "%lud", (unsigned long)(s / 86400));
}
void fmtBytes(uint32_t b, char* o, size_t n) {
  if (b < 10000) snprintf(o, n, "%luB", (unsigned long)b);
  else if (b < 10000000) snprintf(o, n, "%luK", (unsigned long)(b / 1024));
  else snprintf(o, n, "%luM", (unsigned long)(b / 1048576));
}
long localNow() { const uint32_t u = unixNow(); return u ? (long)u + S.tz_min * 60L : 0; }
void fmtTime(long t, char* o, size_t n, bool ampm = false) {
  if (!t) { snprintf(o, n, "--:--"); return; }
  int h = (int)((t / 3600) % 24); const int m = (int)((t / 60) % 60);
  if (S.clock12) { const bool pm = h >= 12; h %= 12; if (!h) h = 12;
    if (ampm) snprintf(o, n, "%d:%02d %s", h, m, pm ? "PM" : "AM"); else snprintf(o, n, "%d:%02d", h, m); }
  else snprintf(o, n, "%02d:%02d", h, m);
}
void fmtDate(long t, char* o, size_t n) {
  if (!t) { snprintf(o, n, "waiting for GPS time"); return; }
  long z = t / 86400 + 719468;                          // days -> civil date (H. Hinnant)
  const long era = z / 146097, doe = z - era * 146097;
  const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  const int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);
  static const char* wd[] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
  static const char* mo[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  snprintf(o, n, "%s, %s %d", wd[(t / 86400) % 7], mo[m - 1], d);
}

void nameOf(const uint8_t* dest, int station, char* o, size_t n) {
  if (S.privacy) { snprintf(o, n, "* * * *"); return; }
  if (const char* nk = nickname(dest)) { snprintf(o, n, "%s", nk); return; }
  if (station >= 0 && S.st[station].name[0]) { snprintf(o, n, "%s", S.st[station].name); return; }
  char h[16]; hexShort(dest, h); snprintf(o, n, "%s", h);
}
bool named(const uint8_t* dest, int station) {
  return nickname(dest) || (station >= 0 && S.st[station].name[0]);
}

int rssiBars(int r) { return r > -70 ? 4 : r > -90 ? 3 : r > -105 ? 2 : r > -118 ? 1 : 0; }
void sigBars(Fb& fb, int x, int y, int level, uint16_t on) {   // 4 rising bars, 14x12
  for (int i = 0; i < 4; ++i) {
    const int h = 3 + i * 3;
    fb.fillRound(x + i * 4, y + 12 - h, 3, h, 1, i < level ? on : T.stroke);
  }
}

void card(Fb& fb, int x, int y, int w, int h) { fb.fillRound(x, y, w, h, 9, T.card); }
// Card header: small colored icon + caps label. Returns the y below it.
int cardHead(Fb& fb, int x, int y, uint8_t ic, uint16_t c, const char* label) {
  fb.icon(x + 8, y + 7, ic, 13, c);
  fb.txt(x + 25, y + 6, label, T.sub, F_SM);
  return y + 22;
}
void progress(Fb& fb, int x, int y, int w, int frac256, uint16_t c) {
  fb.fillRound(x, y, w, 5, 2, T.stroke);
  const int f = w * frac256 / 256;
  if (f > 1) fb.fillRound(x, y, f < 5 ? 5 : f, 5, 2, c);
}

// ---- chrome ----------------------------------------------------------------
void rail(Fb& fb) {
  fb.fill(0, 0, RAIL, H, T.rail);
  for (int i = 0; i < PG_COUNT; ++i) {
    const int y = 4 + i * 33;
    const bool on = i == S.page;
    if (on) fb.fillRound(6, y + 2, 32, 28, 9, T.accent_bg);
    fb.icon(13, y + 7, kRailIcon[i], 18, on ? T.accent : T.muted);
    if (i == PG_LIST && S.msg_unread) {
      char b[4]; snprintf(b, sizeof b, "%lu", (unsigned long)(S.msg_unread > 9 ? 9 : S.msg_unread));
      fb.fillCircle(33, y + 7, 6, T.red);
      fb.txtC(33, y + 1, b, rgb(0xFFFFFF), F_SM);
    }
  }
}

void battery(Fb& fb, int x, int y, int pct) {   // 20x10 glyph
  fb.strokeRound(x, y, 18, 10, 3, T.sub);
  fb.fill(x + 18, y + 3, 2, 4, T.sub);
  if (pct >= 0) {
    const int w = 14 * pct / 100;
    if (w > 0) fb.fillRound(x + 2, y + 2, w, 6, 1, pct <= 15 ? T.red : (pct <= 30 ? T.yellow : T.green));
  } else {
    fb.fill(x + 6, y + 4, 6, 2, T.sub);           // USB power: a plug-ish dash
  }
}

void statusBar(Fb& fb, uint32_t now) {
  fb.txt(X0 + 2, 4, kTitle[S.page], T.text, F_TITLE);
  int xr = XR;
  char clk[12]; fmtTime(localNow(), clk, sizeof clk);
  xr = fb.txtR(xr, 5, clk, T.text, F_BOLD) - 6;
  xr = xr - fb.txtW(clk, F_BOLD);
  // battery
  if (S.battery >= 0) {
    char b[6]; snprintf(b, sizeof b, "%d%%", S.battery);
    const int w = Fb::txtW(b, F_SM);
    fb.txt(xr - w, 6, b, T.sub, F_SM);
    xr -= w + 24;
    battery(fb, xr, 7, S.battery);
  } else {
    xr -= 22; battery(fb, xr, 7, -1);
  }
  xr -= 18;
  fb.icon(xr, 5, I_GPS, 13, S.gps_fix ? T.accent : dimc(T.muted, 160));
  // link: USB / Wi-Fi / BT, green when the host is talking to us
  const uint8_t li = S.link == LINK_WIFI ? I_WIFI : S.link == LINK_BT ? I_BT : I_USB;
  xr -= 17;
  fb.icon(xr, 5, li, 13, S.host ? T.green : dimc(T.muted, 160));
  // live RX / TX dots
  const bool rx = S.last_rx_ms && now - S.last_rx_ms < 600;
  const bool tx = S.last_tx_ms && now - S.last_tx_ms < 600;
  xr -= 10; fb.fillCircle(xr, 11, 3, tx ? T.orange : T.stroke);
  xr -= 9;  fb.fillCircle(xr, 11, 3, rx ? T.green : T.stroke);
  int xl = X0 + 4 + Fb::txtW(kTitle[S.page], F_TITLE) + 8;
  if (S.demo)    xl = fb.chip(xl, 6, "DEMO", T.violet, T.bg, F_SM) + 4;
  if (S.privacy) fb.chip(xl, 6, "PRIVATE", T.muted, T.bg, F_SM);
}

// ---- Home ------------------------------------------------------------------
int newestRx() {
  for (int i = 0; i < S.log_count; ++i) {
    const int j = (S.log_head - 1 - i + kLogLen) % kLogLen;
    if (!S.log[j].tx) return j;
  }
  return -1;
}

void pageHome(Fb& fb, uint32_t now) {
  const long t = localNow();
  char b[48];
  // clock
  fmtTime(t, b, sizeof b);
  const int xe = fb.txt(X0 + 2, TOP - 2, b, T.text, F_HUGE);
  if (S.clock12 && t) fb.txt(xe + 4, TOP + 22, (t / 3600) % 24 >= 12 ? "PM" : "AM", T.sub, F_BOLD);
  // right column: date, place, uptime
  fmtDate(t, b, sizeof b);
  fb.txtR(XR - 2, TOP + 2, b, T.text, F_BOLD);
  if (S.area[0] && !S.privacy) {
    snprintf(b, sizeof b, "%s", S.area); Fb::fit(b, 120, F_BODY);
    const int w = Fb::txtW(b, F_BODY);
    fb.txt(XR - 2 - w, TOP + 19, b, T.accent, F_BODY);
    fb.icon(XR - 2 - w - 15, TOP + 19, I_MAP, 13, T.accent);
  } else {
    fb.txtR(XR - 2, TOP + 19, S.gps_fix ? (S.cities ? "outside towns" : "GPS fix") : "no GPS fix", T.muted, F_BODY);
  }
  const uint32_t up = (now - S.boot_ms) / 1000;
  snprintf(b, sizeof b, "up %luh %02lum", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60));
  fb.txtR(XR - 2, TOP + 36, b, T.muted, F_SM);

  const int y1 = 82, y2 = 160, w = 130, h = 74, xa = X0, xb = X0 + w + 4;
  // LAST HEARD
  card(fb, xa, y1, w, h);
  int y = cardHead(fb, xa, y1, I_BCAST, T.orange, "LAST HEARD");
  const int k = newestRx();
  if (k < 0) fb.txt(xa + 9, y + 2, "Listening...", T.muted, F_BODY);
  else {
    const LogEntry& e = S.log[k];
    if (e.station < 0 && e.name[0] && !S.privacy) snprintf(b, sizeof b, "%s", e.name);
    else nameOf(e.dest, e.station, b, sizeof b);
    Fb::fit(b, w - 18, F_BOLD);
    fb.txt(xa + 9, y, b, T.text, F_BOLD);
    snprintf(b, sizeof b, "%d dBm  %.1f dB", e.rssi, e.snr);
    fb.txt(xa + 9, y + 17, b, T.sub, F_SM);
    char ago[8]; fmtAgo(now - e.t_ms, ago, sizeof ago);
    snprintf(b, sizeof b, "%s ago  %u hop%s", ago, e.hops, e.hops == 1 ? "" : "s");
    fb.txt(xa + 9, y + 32, b, T.muted, F_SM);
    sigBars(fb, xa + w - 24, y1 + 6, rssiBars(e.rssi), T.green);
  }
  // RADIO
  card(fb, xb, y1, w, h);
  y = cardHead(fb, xb, y1, I_ANT, T.accent, "RADIO");
  snprintf(b, sizeof b, "%.3f", S.radio.freq_hz / 1e6);
  int xe2 = fb.txt(xb + 9, y - 4, b, T.text, F_BIG);
  fb.txt(xe2 + 3, y + 5, "MHz", T.sub, F_SM);
  snprintf(b, sizeof b, "SF%u  %luk  4/%u  %ddBm", S.radio.sf, (unsigned long)(S.radio.bw_hz / 1000), S.radio.cr, S.radio.txp);
  Fb::fit(b, w - 16, F_SM);
  fb.txt(xb + 9, y + 21, b, T.sub, F_SM);
  snprintf(b, sizeof b, "noise %d dBm", S.noise);
  fb.txt(xb + 9, y + 35, b, S.interference ? T.orange : T.muted, F_SM);
  // NODES
  card(fb, xa, y2, w, h);
  y = cardHead(fb, xa, y2, I_USERS, T.violet, "NODES");
  int heard = 0, fresh = 0;
  for (int i = 0; i < kMaxStations; ++i) {
    if (!S.st[i].used || now - S.st[i].last_ms > 3600000UL) continue;
    heard++;
    if (now - S.st[i].first_ms < 3600000UL) fresh++;
  }
  snprintf(b, sizeof b, "%d", heard);
  xe2 = fb.txt(xa + 9, y - 4, b, T.text, F_BIG);
  fb.txt(xe2 + 4, y + 5, "last hour", T.sub, F_SM);
  if (fresh) { snprintf(b, sizeof b, "+%d new", fresh); fb.chip(xa + w - 8 - Fb::txtW(b, F_SM) - 10, y2 + 5, b, tint(T.violet), T.violet, F_SM); }
  snprintf(b, sizeof b, "people %lu  nomad %lu", (unsigned long)S.svc_count[0], (unsigned long)S.svc_count[2]);
  Fb::fit(b, w - 16, F_SM);
  fb.txt(xa + 9, y + 21, b, T.sub, F_SM);
  if (S.echo_count) {
    char ago[8]; fmtAgo(now - S.echo_ms, ago, sizeof ago);
    snprintf(b, sizeof b, "repeated %s ago", ago);
    Fb::fit(b, w - 16, F_SM);
    fb.txt(xa + 9, y + 35, b, T.green, F_SM);
  } else {
    snprintf(b, sizeof b, "%lu announces", (unsigned long)(S.svc_count[0] + S.svc_count[1] + S.svc_count[2] + S.svc_count[3] + S.svc_count[7]));
    fb.txt(xa + 9, y + 35, b, T.muted, F_SM);
  }
  // CHANNEL
  card(fb, xb, y2, w, h);
  y = cardHead(fb, xb, y2, I_WAVE, T.blue, "AIRTIME");
  const float pct = airtimePct();
  snprintf(b, sizeof b, "%.1f%%", pct);
  xe2 = fb.txt(xb + 9, y - 4, b, pct >= 8 ? T.orange : T.text, F_BIG);
  fb.txt(xe2 + 4, y + 5, "of 10%", T.sub, F_SM);
  progress(fb, xb + 9, y + 22, w - 18, (int)(pct / 10.0f * 256 > 256 ? 256 : pct / 10.0f * 256), pct >= 8 ? T.orange : T.blue);
  snprintf(b, sizeof b, "rx %lu  tx %lu", (unsigned long)S.rx_total, (unsigned long)S.tx_total);
  fb.txt(xb + 9, y + 35, b, T.muted, F_SM);
}

// ---- Heard -----------------------------------------------------------------
void pageHeard(Fb& fb, uint32_t now) {
  if (!S.log_count) {
    card(fb, X0, TOP + 4, CW, 60);
    fb.icon(X0 + 12, TOP + 25, I_RADAR, 18, T.accent);
    fb.txt(X0 + 40, TOP + 17, "Listening...", T.text, F_BOLD);
    fb.txt(X0 + 40, TOP + 34, "Packets you hear will show up here", T.muted, F_SM);
    return;
  }
  const int rows = 6, rh = 34;
  for (int i = 0; i < rows && i < S.log_count; ++i) {
    const int j = (S.log_head - 1 - i + kLogLen) % kLogLen;
    const LogEntry& e = S.log[j];
    const int y = TOP + 2 + i * rh;
    if (i == 0) fb.fillRound(X0, y, CW, rh - 2, 8, T.card);
    const uint16_t kc = e.tx ? T.accent : kindCol(e.kind);
    fb.fillRound(X0 + 5, y + 4, 24, 24, 7, tint(kc));
    fb.icon(X0 + 10, y + 9, e.tx ? I_ANT : kindIcon(e.kind), 13, kc);
    char name[48];
    if (e.kind == K_PATHREQ) {
      char h[16]; hexShort(e.wanted, h);
      const char* nk = nickname(e.wanted);
      snprintf(name, sizeof name, "Path to %s", S.privacy ? "* * *" : nk ? nk : h);
    } else if (e.station < 0 && e.name[0] && !S.privacy && !nickname(e.dest)) snprintf(name, sizeof name, "%s", e.name);
    else nameOf(e.dest, e.station, name, sizeof name);
    if (e.tx) { char t[56]; snprintf(t, sizeof t, "You: %s", name); snprintf(name, sizeof name, "%s", t); }
    Fb::fit(name, 150, F_BOLD);
    const bool hashOnly = !S.privacy && !e.name[0] && !named(e.dest, e.station) && e.kind != K_PATHREQ;
    fb.txt(X0 + 36, y + 3, name, hashOnly ? T.sub : T.text, F_BOLD);
    char sub[48], via[24] = "";
    if (e.has_via && !S.privacy) {
      const char* nk = nickname(e.via);
      char h[16]; hexShort(e.via, h);
      snprintf(via, sizeof via, "  via %s", nk ? nk : h);
    }
    const char* svc = e.service >= 0 ? kServices[e.service].label : nullptr;
    snprintf(sub, sizeof sub, "%s%s%s  %u hop%s%s", kindLabel(e.kind), svc ? " " : "", svc ? svc : "", e.hops, e.hops == 1 ? "" : "s", via);
    Fb::fit(sub, 170, F_SM);
    fb.txt(X0 + 36, y + 18, sub, T.muted, F_SM);
    char ago[8]; fmtAgo(now - e.t_ms, ago, sizeof ago);
    fb.txtR(XR - 6, y + 18, ago, T.muted, F_SM);
    if (!e.tx) {
      char r[8]; snprintf(r, sizeof r, "%d", e.rssi);
      fb.txtR(XR - 6, y + 3, r, e.rssi > -90 ? T.green : (e.rssi > -110 ? T.text : T.orange), F_BOLD);
    } else fb.txtR(XR - 6, y + 3, "TX", T.accent, F_BOLD);
    if (i) fb.hline(X0 + 36, y, CW - 42, T.stroke);
  }
}

// ---- Signal ----------------------------------------------------------------
void pageSignal(Fb& fb, uint32_t now) {
  (void)now;
  char b[32];
  const int w = 130, h = 102, xa = X0, xb = X0 + w + 4, ya = TOP + 2, yb = TOP + 108;
  // signal chart
  card(fb, xa, ya, w, h);
  cardHead(fb, xa, ya, I_CHART, T.green, "SIGNAL");
  fb.txtR(xa + w - 8, ya + 6, "SNR", T.orange, F_SM);
  fb.txtR(xa + w - 34, ya + 6, "RSSI", T.green, F_SM);
  const int gx = xa + 8, gw = w - 16, gy = ya + 26, gh = h - 34;
  for (int g = 0; g <= 2; ++g) fb.hline(gx, gy + g * gh / 2, gw, T.stroke);
  for (int pass = 0; pass < 2; ++pass) {
    float lx = -1, ly = 0;
    for (int i = 0; i < S.sig_n; ++i) {
      const int k = (S.sig_head - S.sig_n + i + kSigHist) % kSigHist;
      const float x = gx + (float)i * (gw - 1) / (kSigHist - 1);
      float v = pass == 0 ? (S.sig_rssi[k] + 130) / 70.0f : (S.sig_snr[k] + 15) / 30.0f;
      v = v < 0 ? 0 : v > 1 ? 1 : v;
      const float yy = gy + gh - 1 - v * (gh - 2);
      if (lx >= 0) fb.lineAA(lx, ly, x, yy, pass ? T.orange : T.green);
      lx = x; ly = yy;
    }
  }
  // airtime gauge
  card(fb, xb, ya, w, h);
  cardHead(fb, xb, ya, I_CLOCK, T.blue, "TX AIRTIME 1H");
  const float pct = airtimePct();
  const float frac = pct / 10.0f > 1 ? 1 : pct / 10.0f;
  const int cx = xb + w / 2, cy = ya + 84;
  fb.arc(cx, cy, 44, 8, 0, 180, T.stroke);
  if (frac > 0.01f) fb.arc(cx, cy, 44, 8, (int)(180 - frac * 180), 180, pct >= 8 ? T.orange : T.blue);
  snprintf(b, sizeof b, "%.1f%%", pct);
  fb.txtC(cx, cy - 26, b, T.text, F_BIG);
  fb.txtC(cx, cy + 2, "of 10% budget", T.muted, F_SM);
  // packets per minute
  card(fb, xa, yb, w, h);
  cardHead(fb, xa, yb, I_ANT, T.accent, "PACKETS / MIN");
  uint16_t mx = 4;
  for (int i = 0; i < kMinuteBins; ++i) if (S.min_rx[i] + S.min_tx[i] > mx) mx = S.min_rx[i] + S.min_tx[i];
  const int bh = h - 32;
  for (int i = 0; i < kMinuteBins; ++i) {
    const int bi = kMinuteBins - 1 - i;
    const int x = xa + 9 + i * 4;
    const int hr = S.min_rx[bi] * bh / mx, ht = S.min_tx[bi] * bh / mx;
    if (hr) fb.fillRound(x, yb + h - 6 - hr, 3, hr, 1, T.accent);
    if (ht) fb.fillRound(x, yb + h - 6 - hr - ht, 3, ht, 1, T.orange);
  }
  // who's out there
  card(fb, xb, yb, w, h);
  cardHead(fb, xb, yb, I_USERS, T.violet, "WHO'S OUT THERE");
  uint32_t mxs = 1;
  for (int i = 0; i < 8; ++i) if (S.svc_count[i] > mxs) mxs = S.svc_count[i];
  const struct { int idx; const char* l; uint16_t c; } rows[5] = {
    {0, "People", T.orange}, {2, "NomadNet", T.blue}, {1, "Propagation", T.violet}, {3, "Calls", T.green}, {7, "Other", T.sub}};
  for (int i = 0; i < 5; ++i) {
    const int y = yb + 24 + i * 15;
    const uint32_t v = S.svc_count[rows[i].idx];
    fb.txt(xb + 9, y, rows[i].l, T.sub, F_SM);
    progress(fb, xb + 72, y + 5, 34, (int)(256 * v / mxs), rows[i].c);
    snprintf(b, sizeof b, "%lu", (unsigned long)v);
    fb.txtR(xb + w - 8, y, b, T.text, F_SM);
  }
}

// ---- Channel ---------------------------------------------------------------
uint16_t heat(int16_t rssi, int16_t noise) {
  if (rssi <= -200) return T.card2;
  const int d = rssi - noise;
  if (d < 4) return rgb(0x14334A);
  if (d < 10) return rgb(0x155E75);
  if (d < 20) return rgb(0x0E9488);
  if (d < 30) return rgb(0x2DD4BF);
  if (d < 40) return rgb(0x99F6E4);
  return rgb(0xFDE68A);
}

void statTile(Fb& fb, int x, int y, int w, const char* k, const char* v, uint16_t vc) {
  fb.fillRound(x, y, w, 42, 8, T.card);
  fb.txtC(x + w / 2, y + 5, k, T.muted, F_SM);
  fb.txtC(x + w / 2, y + 19, v, vc, F_BOLD);
}

void pageChannel(Fb& fb, uint32_t now) {
  (void)now;
  char b[40];
  const int tw = (CW - 12) / 4;
  snprintf(b, sizeof b, "%d", S.noise); statTile(fb, X0, TOP + 2, tw, "FLOOR", b, T.text);
  snprintf(b, sizeof b, "%.1f%%", airtimePct()); statTile(fb, X0 + tw + 4, TOP + 2, tw, "AIRTIME", b, T.text);
  snprintf(b, sizeof b, "%u", S.min_rx[0] + S.min_tx[0]); statTile(fb, X0 + 2 * (tw + 4), TOP + 2, tw, "PKT/MIN", b, T.text);
  snprintf(b, sizeof b, "%lu", (unsigned long)S.intf_events);
  statTile(fb, X0 + 3 * (tw + 4), TOP + 2, tw, "INTERF", b, S.interference ? T.orange : T.text);
  // waterfall
  const int y0 = TOP + 50;
  card(fb, X0, y0, CW, 160);
  snprintf(b, sizeof b, "%.3f MHz  SF%u  %luk", S.radio.freq_hz / 1e6, S.radio.sf, (unsigned long)(S.radio.bw_hz / 1000));
  fb.icon(X0 + 8, y0 + 7, I_WAVE, 13, T.accent);
  fb.txt(X0 + 25, y0 + 6, b, T.sub, F_SM);
  fb.txtR(XR - 8, y0 + 6, "last 5 min", T.muted, F_SM);
  const int gx = X0 + 9, gy = y0 + 24, cw = 4, rh = 18;
  const int curcol = (int)(S.chan_sec % 60);
  for (int r = 0; r < kChanRows; ++r)
    for (int c = 0; c < 60; ++c) {
      int16_t v = S.chan[r][c];
      if (r == 0 && c > curcol) v = -200;
      const int x = gx + c * cw, y = gy + r * (rh + 2);
      fb.fill(x, y, cw - 1, rh, heat(v, S.noise));
      const uint8_t fl = (r == 0 && c > curcol) ? 0 : S.chan_flags[r][c];
      if (fl & 1) fb.fill(x, y, cw - 1, 3, T.orange);
      else if (fl & 2) fb.fill(x, y + rh - 3, cw - 1, 3, T.red);
    }
  fb.fillRound(gx + curcol * cw - 1, gy - 4, cw + 1, 3, 1, T.text);
  const int ly = gy + kChanRows * (rh + 2) + 4;
  fb.txt(gx, ly, ":00", T.muted, F_SM); fb.txtC(gx + 30 * cw, ly, ":30", T.muted, F_SM); fb.txtR(gx + 60 * cw, ly, ":59", T.muted, F_SM);
  int x = gx;
  const uint16_t lg[5] = {rgb(0x14334A), rgb(0x0E9488), rgb(0x2DD4BF), rgb(0x99F6E4), rgb(0xFDE68A)};
  for (int i = 0; i < 5; ++i) fb.fillRound(x + i * 9, ly + 18, 8, 6, 2, lg[i]);
  fb.txt(x + 50, ly + 14, "signal", T.sub, F_SM);
  fb.fillRound(x + 96, ly + 18, 10, 6, 2, T.orange); fb.txt(x + 110, ly + 14, "you sent", T.sub, F_SM);
  fb.fillRound(x + 168, ly + 18, 10, 6, 2, T.red); fb.txt(x + 182, ly + 14, "interference", T.sub, F_SM);
}

// ---- Area ------------------------------------------------------------------
void coveragePlot(Fb& fb, int x, int y, int w, int h, bool over_map) {
  const int n = coverageCount();
  if (!n) { if (!over_map) fb.txtC(x + w / 2, y + h / 2 - 6, "drive around to build a coverage map", T.muted, F_SM); return; }
  // fit the samples around the current position (or their center)
  int32_t clat = S.gps_fix ? S.lat : coverage(n - 1).lat, clon = S.gps_fix ? S.lon : coverage(n - 1).lon;
  int32_t span = 2000;                                  // micro-degrees, at least ~200 m
  for (int i = 0; i < n; ++i) {
    const CovSample& c = coverage(i);
    const int32_t d = (int32_t)fmaxf(fabsf((float)(c.lat - clat)), fabsf((float)(c.lon - clon)) * 0.78f);
    if (d > span) span = d;
  }
  const float s = (fminf(w, h) / 2 - 6) / (float)span;
  for (int i = 0; i < n; ++i) {
    const CovSample& c = coverage(i);
    const int px = x + w / 2 + (int)((c.lon - clon) * 0.78f * s), py = y + h / 2 - (int)((c.lat - clat) * s);
    const uint16_t cc = c.rssi > -90 ? T.green : c.rssi > -105 ? T.yellow : c.rssi > -115 ? T.orange : T.red;
    fb.fillCircle(px, py, 3, cc);
  }
  if (S.gps_fix) { fb.fillCircle(x + w / 2, y + h / 2, 5, rgb(0xFFFFFF)); fb.fillCircle(x + w / 2, y + h / 2, 3, T.blue); }
}

void pageArea(Fb& fb, uint32_t now) {
  (void)now;
  char b[48];
  // place card
  card(fb, X0, TOP + 2, CW, 54);
  fb.fillRound(X0 + 8, TOP + 12, 34, 34, 9, tint(T.accent));
  fb.icon(X0 + 16, TOP + 20, I_MAP, 18, T.accent);
  if (S.area[0] && !S.privacy) { snprintf(b, sizeof b, "%s", S.area); Fb::fit(b, CW - 60, F_BIG); fb.txt(X0 + 50, TOP + 6, b, T.text, F_BIG); }
  else fb.txt(X0 + 50, TOP + 10, !S.cities ? "No cities.bin on SD" : S.gps_fix ? "Outside listed towns" : "Waiting for GPS fix", T.text, F_BOLD);
  if (S.gps_fix) {
    char g[8]; maidenhead(S.lat, S.lon, g);
    if (S.privacy) snprintf(b, sizeof b, "%s  %u sats", g, S.sats);
    else snprintf(b, sizeof b, "%.4f, %.4f  %s  %u sats", S.lat / 1e6, S.lon / 1e6, g, S.sats);
    fb.txt(X0 + 50, TOP + 34, b, T.sub, F_SM);
  } else fb.txt(X0 + 50, TOP + 34, "position stays on this device", T.muted, F_SM);
  // map / coverage card
  const int my = TOP + 60, mh = 92;
  card(fb, X0, my, CW, mh);
  bool map = false;
  if (g_map && S.gps_fix && !S.privacy) map = g_map(fb, X0 + 2, my + 2, CW - 4, mh - 4, S.lat, S.lon);
  if (!S.privacy) coveragePlot(fb, X0 + 2, my + 2, CW - 4, mh - 4, map);
  fb.fillRound(X0 + 6, my + 5, 82, 16, 6, dimc(T.card, 220));
  fb.txt(X0 + 11, my + 6, "COVERAGE", T.sub, F_SM);
  snprintf(b, sizeof b, "%d pts", coverageCount());
  fb.txtR(XR - 8, my + 6, b, T.muted, F_SM);
  // trip
  const int ty = my + mh + 4;
  card(fb, X0, ty, CW, H - 4 - ty);
  fb.icon(X0 + 8, ty + 6, I_ROUTE, 13, T.violet);
  fb.txt(X0 + 25, ty + 5, "THIS TRIP", T.sub, F_SM);
  fb.txtR(XR - 8, ty + 5, "NODES", T.muted, F_SM);
  for (int i = 0; i < S.visit_n && i < 2; ++i) {
    const AreaVisit& v = S.visits[S.visit_n - 1 - i];
    const int y = ty + 20 + i * 15;
    char t[12] = "--:--";
    if (v.enter_unix) fmtTime((long)v.enter_unix + S.tz_min * 60L, t, sizeof t);
    fb.txt(X0 + 9, y, t, T.muted, F_SM);
    snprintf(b, sizeof b, "%s", S.privacy ? "* * *" : v.name); Fb::fit(b, 150, F_BODY);
    fb.txt(X0 + 52, y - 1, b, i == 0 ? T.accent : T.text, F_BODY);
    snprintf(b, sizeof b, "%u", v.heard);
    fb.txtR(XR - 8, y, b, T.text, F_SM);
  }
}

// ---- Radios (host interfaces) ----------------------------------------------
uint8_t ifIcon(uint8_t k) {
  switch (k) { case IF_LORA: return I_ANT; case IF_KISS_VU: return I_RADAR; case IF_KISS_HF: return I_SAT;
    case IF_TCP: return I_NET; case IF_AUTO: return I_WIFI; default: return I_ROUTER; }
}
uint16_t ifCol(uint8_t k) {
  switch (k) { case IF_LORA: return T.accent; case IF_KISS_VU: return T.orange; case IF_KISS_HF: return T.yellow;
    case IF_TCP: return T.violet; case IF_AUTO: return T.green; default: return T.sub; }
}
const char* ifLabel(uint8_t k) {
  switch (k) { case IF_LORA: return "LoRa"; case IF_KISS_VU: return "VHF/UHF"; case IF_KISS_HF: return "HF";
    case IF_TCP: return "TCP/IP"; case IF_AUTO: return "LAN"; default: return "other"; }
}

void pageRadios(Fb& fb, uint32_t now) {
  (void)now;
  char b[48];
  if (!S.have_host_stats) {
    card(fb, X0, TOP + 4, CW, 96);
    fb.fillRound(X0 + 10, TOP + 16, 34, 34, 9, tint(S.host ? T.green : T.orange));
    fb.icon(X0 + 18, TOP + 24, S.link == LINK_WIFI ? I_WIFI : S.link == LINK_BT ? I_BT : I_USB, 18, S.host ? T.green : T.orange);
    fb.txt(X0 + 54, TOP + 16, S.host ? "Computer connected" : "Waiting for a computer", T.text, F_BOLD);
    fb.txt(X0 + 54, TOP + 33, S.link == LINK_WIFI && S.ip[0] ? S.ip : "Reticulum talks to the RNode", T.sub, F_SM);
    fb.txt(X0 + 12, TOP + 62, "Add the ColorUI plugin to rnsd to list", T.muted, F_SM);
    fb.txt(X0 + 12, TOP + 76, "every interface (LoRa, VHF/UHF, HF, TCP)", T.muted, F_SM);
    return;
  }
  const int rh = 36;
  for (int i = 0; i < S.if_n && i < 5; ++i) {
    const Iface& f = S.ifs[i];
    const int y = TOP + 2 + i * (rh + 4);
    card(fb, X0, y, CW, rh);
    const uint16_t c = ifCol(f.kind);
    fb.fillRound(X0 + 6, y + 6, 24, 24, 7, tint(c));
    fb.icon(X0 + 11, y + 11, ifIcon(f.kind), 13, c);
    snprintf(b, sizeof b, "%s", f.name); Fb::fit(b, 150, F_BOLD);
    fb.txt(X0 + 38, y + 4, b, T.text, F_BOLD);
    char br[16], rx[12], tx[12];
    if (f.bitrate >= 1000000) snprintf(br, sizeof br, "%luM", (unsigned long)(f.bitrate / 1000000));
    else if (f.bitrate >= 1000) snprintf(br, sizeof br, "%.1fk", f.bitrate / 1000.0);
    else snprintf(br, sizeof br, "%lu", (unsigned long)f.bitrate);
    fmtBytes(f.rxb, rx, sizeof rx); fmtBytes(f.txb, tx, sizeof tx);
    snprintf(b, sizeof b, "%s  %sbps  rx %s  tx %s", ifLabel(f.kind), br, rx, tx);
    Fb::fit(b, 180, F_SM);
    fb.txt(X0 + 38, y + 20, b, T.muted, F_SM);
    fb.chip(XR - 8 - Fb::txtW(f.up ? "UP" : "DOWN", F_SM) - 10, y + 10, f.up ? "UP" : "DOWN",
            tint(f.up ? T.green : T.red), f.up ? T.green : T.red, F_SM);
  }
  const int y = TOP + 2 + (S.if_n < 5 ? S.if_n : 5) * (rh + 4);
  snprintf(b, sizeof b, "%u paths   %u known   %u queued", S.paths, S.dests, S.lxmf_q);
  if (y < H - 16) fb.txt(X0 + 4, y + 2, b, T.muted, F_SM);
}

// ---- Status ----------------------------------------------------------------
void drawQr(Fb& fb, int x, int y, int box, const uint8_t* hash) {
  char hex[33];
  for (int i = 0; i < 16; ++i) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
  static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(3)], tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(3)];
  if (!qrcodegen_encodeText(hex, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 3, qrcodegen_Mask_AUTO, true)) return;
  const int n = qrcodegen_getSize(qr);
  const int m = (box - 4) / (n + 2);                    // module size with a 1-module quiet zone
  const int off = (box - m * n) / 2;
  fb.fillRound(x, y, box, box, 6, rgb(0xFFFFFF));
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i)
      if (qrcodegen_getModule(qr, i, j)) fb.fill(x + off + i * m, y + off + j * m, m, m, rgb(0x000000));
}

void pageStatus(Fb& fb, uint32_t now) {
  char b[48];
  // connection card
  card(fb, X0, TOP + 2, CW, 40);
  const uint8_t li = S.link == LINK_WIFI ? I_WIFI : S.link == LINK_BT ? I_BT : I_USB;
  fb.fillRound(X0 + 6, TOP + 8, 28, 28, 8, tint(S.host ? T.green : T.orange));
  fb.icon(X0 + 11, TOP + 13, li, 18, S.host ? T.green : T.orange);
  const char* how = S.link == LINK_WIFI ? "Wi-Fi" : S.link == LINK_BT ? "Bluetooth" : "USB";
  snprintf(b, sizeof b, "%s via %s", S.host ? "Host connected" : "Waiting for host", how);
  fb.txt(X0 + 42, TOP + 6, b, T.text, F_BOLD);
  if (S.link == LINK_WIFI && S.ip[0]) snprintf(b, sizeof b, "IP %s  port 7633", S.ip);
  else snprintf(b, sizeof b, "%s", S.board);
  Fb::fit(b, CW - 50, F_SM);
  fb.txt(X0 + 42, TOP + 22, b, T.sub, F_SM);
  // left: details, right: QR of my address
  const bool qr = S.my_n > 0 && !S.privacy;
  const int lw = qr ? CW - 104 : CW;
  const int y0 = TOP + 46;
  card(fb, X0, y0, lw, H - 4 - y0);
  const uint32_t up = (now - S.boot_ms) / 1000;
  struct { const char* k; char v[32]; } rows[8];
  rows[0].k = "Radio"; snprintf(rows[0].v, 32, "%.3f SF%u %luk", S.radio.freq_hz / 1e6, S.radio.sf, (unsigned long)(S.radio.bw_hz / 1000));
  rows[1].k = "TX power"; snprintf(rows[1].v, 32, "%d dBm", S.radio.txp);
  rows[2].k = "Noise"; snprintf(rows[2].v, 32, "%d dBm", S.noise);
  rows[3].k = "Packets"; snprintf(rows[3].v, 32, "%lu rx / %lu tx", (unsigned long)S.rx_total, (unsigned long)S.tx_total);
  rows[4].k = "GPS"; if (S.gps_fix) snprintf(rows[4].v, 32, "fix, %u sats", S.sats); else snprintf(rows[4].v, 32, "no fix");
  rows[5].k = "SD card"; snprintf(rows[5].v, 32, "%s%s", S.sd ? "ok" : "none", S.cities ? ", cities" : "");
  rows[6].k = "Firmware"; snprintf(rows[6].v, 32, "%s", S.fw);
  rows[7].k = "Uptime"; snprintf(rows[7].v, 32, "%lud %02lu:%02lu", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24), (unsigned long)(up / 60 % 60));
  for (int i = 0; i < 8; ++i) {
    const int y = y0 + 6 + i * 18;
    fb.txt(X0 + 9, y, rows[i].k, T.muted, F_SM);
    Fb::fit(rows[i].v, lw - 70, F_SM);
    fb.txtR(X0 + lw - 9, y, rows[i].v, T.text, F_SM);
    if (i) fb.hline(X0 + 9, y - 3, lw - 18, T.stroke);
  }
  if (qr) {
    const int qx = XR - 100;
    card(fb, qx, y0, 100, H - 4 - y0);
    fb.txtC(qx + 50, y0 + 5, "MY LXMF", T.sub, F_SM);
    drawQr(fb, qx + 8, y0 + 22, 84, S.my_dest[0]);
    char hx[16]; hexShort(S.my_dest[0], hx);
    fb.txtC(qx + 50, y0 + 112, hx, T.muted, F_SM);
    fb.txtC(qx + 50, y0 + 128, "scan to add me", T.accent, F_SM);
  }
}

// ---- notifications -----------------------------------------------------------
void blit2x(Fb& fb, int x0, int y0, const uint8_t* pix, const uint16_t* pal, int w, int h, int clip_r = 0) {
  const int W2 = w * 2, H2 = h * 2;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint8_t i = pix[y * w + x];
      if (!i) continue;
      const uint16_t c = pal[i];
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
          const int px = 2 * x + dx, py = 2 * y + dy;
          if (clip_r) {                                   // round the image's corners
            const int cx = px < clip_r ? clip_r - px : px >= W2 - clip_r ? px - (W2 - clip_r - 1) : 0;
            const int cy = py < clip_r ? clip_r - py : py >= H2 - clip_r ? py - (H2 - clip_r - 1) : 0;
            if (cx && cy && cx * cx + cy * cy > clip_r * clip_r) continue;
          }
          fb.set(x0 + px, y0 + py, c);
        }
    }
}

int slideIn(uint32_t now) {
  const uint32_t t = now - S.ncur_start;
  if (t >= 220) return 256;
  const int k = (int)(t * 256 / 220);
  return 256 - ((256 - k) * (256 - k) >> 8);
}

void noteCard(Fb& fb, uint32_t now) {
  const NoteItem& n = S.ncur;
  const int w = 256, h = 160, x = (W - w) / 2;
  const int y = 38 - (h + 50) * (256 - slideIn(now)) / 256;
  fb.shade(0, 0, W, H, 110);
  fb.fillRound(x, y, w, h, 14, T.card);
  fb.strokeRound(x, y, w, h, 14, T.stroke);
  const bool area = n.kind == N_AREA;
  blit2x(fb, x + 8, y + 8, area ? kPopWelcome : kPopMessage, area ? kPopWelcomePal : kPopMessagePal, kPopCardW, kPopCardH, 8);
  char up[28];
  if (area) {
    // city-limits sign on the art keeps its pixel font
    for (size_t i = 0; i < sizeof up; ++i) { const char c = (S.privacy ? "A NEW AREA" : n.text)[i]; up[i] = (c >= 'a' && c <= 'z') ? c - 32 : c; if (!c) break; }
    up[sizeof up - 1] = 0;
    const int sx = x + 8 + 2 * 58, sw = 2 * 56, sy = y + 8 + 2 * 8;
    fb.textCenter(sx + sw / 2, sy + 6, "ENTERING", rgb(0xFFFFFF), 1, 6);
    const int adv = Fb::textWidth(up, 1, 7) <= sw - 8 ? 7 : 6;
    char fit[28]; snprintf(fit, sizeof fit, "%s", up);
    while (Fb::textWidth(fit, 1, adv) > sw - 6 && strlen(fit) > 3) fit[strlen(fit) - 1] = 0;
    fb.textCenter(sx + sw / 2, sy + 22, fit, rgb(0xFFFFFF), 1, adv);
    char line[40]; snprintf(line, sizeof line, "Welcome to %s", S.privacy ? "a new area" : n.text);
    Fb::fit(line, w - 20, F_TITLE);
    fb.txtC(W / 2, y + 112, line, T.text, F_TITLE);
    char sub[48];
    if (S.visit_n > 1) snprintf(sub, sizeof sub, "Area %d this trip  -  %u nodes in the last one", S.visit_id, (unsigned)S.visits[S.visit_n - 2].heard);
    else snprintf(sub, sizeof sub, "First area this trip");
    Fb::fit(sub, w - 20, F_SM);
    fb.txtC(W / 2, y + 136, sub, T.sub, F_SM);
  } else {
    char line[40];
    if (n.count > 1) snprintf(line, sizeof line, "%u messages for you", (unsigned)n.count);
    else snprintf(line, sizeof line, "Message for you");
    fb.txtC(W / 2, y + 112, line, T.text, F_TITLE);
    fb.txtC(W / 2, y + 136, n.text[0] ? n.text : "Encrypted - open your app to read it", T.sub, F_SM);
  }
}

void noteToast(Fb& fb, uint32_t now) {
  const NoteItem& n = S.ncur;
  const int w = 228, h = 46;
  const int x = W - w - 6 + (w + 12) * (256 - slideIn(now)) / 256, y = 26;
  const uint8_t* pix = kPopNode; const uint16_t* pal = kPopNodePal;
  char title[28] = "New node", body[40] = {0};
  uint16_t accent = T.accent;
  switch (n.kind) {
    case N_NODE:
      if (n.count > 1) snprintf(title, sizeof title, "%u new nodes", (unsigned)n.count);
      snprintf(body, sizeof body, "%s", S.privacy ? "* * *" : n.text);
      break;
    case N_HOST_UP:   pix = kPopHostUp; pal = kPopHostUpPal; snprintf(title, sizeof title, "Computer linked"); snprintf(body, sizeof body, "Reticulum is online"); accent = T.green; break;
    case N_HOST_LOST: pix = kPopHostLost; pal = kPopHostLostPal; snprintf(title, sizeof title, "Computer lost"); snprintf(body, sizeof body, "No host for 30 s"); accent = T.red; break;
    case N_GPS_FIX:   pix = kPopGpsFix; pal = kPopGpsFixPal; snprintf(title, sizeof title, "GPS fix"); snprintf(body, sizeof body, "%s", n.text); accent = T.green; break;
    case N_GPS_LOST:  pix = kPopGpsLost; pal = kPopGpsLostPal; snprintf(title, sizeof title, "GPS lost"); snprintf(body, sizeof body, "City locator paused"); accent = T.orange; break;
    case N_INTF:      pix = kPopIntf; pal = kPopIntfPal; snprintf(title, sizeof title, "Interference"); snprintf(body, sizeof body, "Noise spike on the channel"); accent = T.yellow; break;
    case N_BATT:      pix = kPopBatt; pal = kPopBattPal; snprintf(title, sizeof title, "Battery low"); snprintf(body, sizeof body, "%s", n.text); accent = T.red; break;
    case N_ECHO:      pix = kPopNode; pal = kPopNodePal; snprintf(title, sizeof title, "You were heard!"); snprintf(body, sizeof body, "Repeated by %s", S.privacy ? "* * *" : n.text); accent = T.green; break;
    case N_AIRTIME:   pix = kPopIntf; pal = kPopIntfPal; snprintf(title, sizeof title, "Airtime high"); snprintf(body, sizeof body, "%s", n.text); accent = T.orange; break;
    default: break;
  }
  fb.fillRound(x + 2, y + 3, w, h, 12, rgb(0x000000));        // soft shadow
  fb.fillRound(x, y, w, h, 12, T.card2);
  fb.strokeRound(x, y, w, h, 12, dimc(accent, 120));
  blit2x(fb, x + 4, y + 3, pix, pal, kPopIconW, kPopIconH, 6);
  fb.txt(x + 50, y + 6, title, accent, F_BOLD);
  Fb::fit(body, w - 58, F_SM);
  fb.txt(x + 50, y + 24, body, T.text, F_SM);
}

void drawNote(Fb& fb, uint32_t now) {
  if (!S.ncur.kind || now >= S.ncur_until) return;
  if (S.ncur.kind == N_AREA || S.ncur.kind == N_MSG) noteCard(fb, now);
  else noteToast(fb, now);
}

// ---- screensaver -------------------------------------------------------------
void screensaver(Fb& fb, uint32_t now) {
  fb.clear(rgb(0x000000));
  const long t = localNow();
  // drift a little every minute so nothing burns in
  const int m = (int)((t ? t : (long)(now / 1000)) / 60);
  const int dx = (m * 37) % 60 - 30, dy = (m * 23) % 40 - 20;
  char b[48];
  fmtTime(t, b, sizeof b);
  fb.txtC(W / 2 + dx, 70 + dy, b, T.text, F_HUGE);
  fmtDate(t, b, sizeof b);
  fb.txtC(W / 2 + dx, 120 + dy, b, T.sub, F_BOLD);
  if (S.area[0] && !S.privacy) fb.txtC(W / 2 + dx, 140 + dy, S.area, T.accent, F_BODY);
  const int k = newestRx();
  if (k >= 0) {
    const LogEntry& e = S.log[k];
    char nm[32], ago[8];
    if (e.station < 0 && e.name[0] && !S.privacy) snprintf(nm, sizeof nm, "%s", e.name); else nameOf(e.dest, e.station, nm, sizeof nm);
    fmtAgo(now - e.t_ms, ago, sizeof ago);
    snprintf(b, sizeof b, "last heard %s, %s ago", nm, ago);
    Fb::fit(b, 280, F_SM);
    fb.txtC(W / 2 + dx, 166 + dy, b, T.muted, F_SM);
  }
  if (S.msg_unread) {
    snprintf(b, sizeof b, "%lu new message%s", (unsigned long)S.msg_unread, S.msg_unread == 1 ? "" : "s");
    fb.chip(W / 2 + dx - Fb::txtW(b, F_SM) / 2 - 5, 186 + dy, b, tint(T.red), T.red, F_SM);
  }
}

}  // namespace

void render(Fb& fb, uint32_t now) {
  S.now_ms = now;
  if (S.retro) { renderRetro(fb, now); return; }
  if (S.idle && !wantWake()) { screensaver(fb, now); return; }
  fb.clear(T.bg);
  switch (S.page) {
    case PG_HEARD:   pageHome(fb, now); break;
    case PG_LIST:    pageHeard(fb, now); break;
    case PG_GRAPHS:  pageSignal(fb, now); break;
    case PG_CHANNEL: pageChannel(fb, now); break;
    case PG_AREA:    pageArea(fb, now); break;
    case PG_IFACES:  pageRadios(fb, now); break;
    default:         pageStatus(fb, now); break;
  }
  rail(fb);
  statusBar(fb, now);
  drawNote(fb, now);
}

}  // namespace cui
