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

bool watched(const uint8_t* h);   // cui_core.cpp

// Optional map painter (device: SD tiles). Returns false if it drew nothing.
static MapPainter g_map = nullptr;
void setMapPainter(MapPainter p) { g_map = p; }

namespace {

constexpr int RAIL = 44;              // left icon rail width
constexpr int X0 = 50, XR = 314;      // content area
constexpr int CW = XR - X0;           // 264
constexpr int ANN_X = XR - 96, ANN_Y = 34;   // Status page "Announce" button
int TOP = 26;                         // below the status bar (52 on pages with tabs)

const char* kTitle[PG_COUNT] = {"Home", "Heard", "Signal", "Channel", "Area", "Radios", "Status"};
const uint8_t kRailIcon[PG_COUNT] = {I_HOME, I_LIST, I_CHART, I_WAVE, I_MAP, I_NET, I_INFO};

// ---- helpers ---------------------------------------------------------------
int button(Fb& fb, int x, int y, const char* label, uint8_t ic, uint16_t c);
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
void heardPackets(Fb& fb, uint32_t now) {
  if (!S.log_count) {
    card(fb, X0, TOP + 4, CW, 60);
    fb.icon(X0 + 12, TOP + 25, I_RADAR, 18, T.accent);
    fb.txt(X0 + 40, TOP + 17, "Listening...", T.text, F_BOLD);
    fb.txt(X0 + 40, TOP + 34, "Packets you hear will show up here", T.muted, F_SM);
    return;
  }
  const int rh = 34, rows = (H - TOP - 4) / rh;
  const int off = S.scroll[PG_LIST];
  for (int i = 0; i < rows && i + off < S.log_count; ++i) {
    const int j = (S.log_head - 1 - (i + off) + kLogLen) % kLogLen;
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
void signalLive(Fb& fb, uint32_t now) {
  (void)now;
  char b[32];
  const int w = 130, h = (H - TOP - 8) / 2, xa = X0, xb = X0 + w + 4, ya = TOP + 2, yb = TOP + 6 + h;
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
  const int cx = xb + w / 2, cy = ya + h - 18;
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
    const int y = yb + 22 + i * ((h - 26) / 5);
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

void areaMap(Fb& fb, uint32_t now) {
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
  const int my = TOP + 60, mh = H - 4 - my - 44;
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
  for (int i = 0; i < S.visit_n && i < (H - 4 - ty - 20) / 15; ++i) {
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

void radiosIfaces(Fb& fb, uint32_t now) {
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
  const int maxr = (H - TOP - 18) / (rh + 4);
  for (int i = 0; i < S.if_n && i < maxr; ++i) {
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
  const int y = TOP + 2 + (S.if_n < maxr ? S.if_n : maxr) * (rh + 4);
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
  if (S.host) snprintf(b, sizeof b, "Linked via %s", how);
  else snprintf(b, sizeof b, "Waiting for host via %s", how);
  Fb::fit(b, S.host ? CW - 150 : CW - 50, F_BOLD);
  fb.txt(X0 + 42, TOP + 6, b, T.text, F_BOLD);
  if (S.host) button(fb, ANN_X, ANN_Y, "Announce", I_BCAST, T.accent);
  if (S.web[0]) snprintf(b, sizeof b, "Files: %s", S.web);
  else if (S.ip[0]) snprintf(b, sizeof b, S.host ? "%s:7633" : "Wi-Fi %s  port 7633", S.ip);
  else snprintf(b, sizeof b, "%s", S.board);
  Fb::fit(b, S.host ? CW - 150 : CW - 50, F_SM);
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

// ============================================================================
// 0.8: tabs, Nodes, History, Sky, Network, and the tap-opened views
// ============================================================================
constexpr int TAB_Y = 26, TAB_H = 22;

// Segmented tabs at the top of a page. w = width used (scroll buttons may sit to the right).
void tabs(Fb& fb, const char* const* labels, int n, int active, int w) {
  fb.fillRound(X0, TAB_Y, w, TAB_H, 8, T.card);
  const int sw = w / n;
  for (int i = 0; i < n; ++i) {
    const int x = X0 + i * sw;
    if (i == active) fb.fillRound(x + 2, TAB_Y + 2, sw - 4, TAB_H - 4, 6, T.accent_bg);
    fb.txtC(x + sw / 2, TAB_Y + 4, labels[i], i == active ? T.accent : T.sub, F_BOLD);
  }
}
// up/down scroll buttons to the right of the tabs
void scrollButtons(Fb& fb) {
  for (int k = 0; k < 2; ++k) {
    const int x = XR - 56 + k * 29;
    fb.fillRound(x, TAB_Y, 27, TAB_H, 8, T.card);
    const int cx = x + 13, cy = TAB_Y + 11, d = k ? 1 : -1;   // k=0 up, k=1 down
    fb.lineAA(cx - 5, cy - 2 * d, cx, cy + 3 * d, T.sub);
    fb.lineAA(cx, cy + 3 * d, cx + 5, cy - 2 * d, T.sub);
  }
}

// stations shown on the Nodes tab, after sort + filter
int nodeList(int* out, int cap) {
  int n = 0;
  for (int i = 0; i < kMaxStations && n < cap; ++i) {
    if (!S.st[i].used) continue;
    const int sv = S.st[i].service;
    const int f = S.node_filter;
    if (f == 1 && sv != 0) continue;
    if (f == 2 && sv != 2) continue;
    if (f == 3 && sv != 1) continue;
    if (f == 4 && sv != 3) continue;
    out[n++] = i;
  }
  for (int a = 1; a < n; ++a)                      // insertion sort, n <= 64
    for (int b = a; b > 0; --b) {
      const Station &x = S.st[out[b - 1]], &y = S.st[out[b]];
      bool swap = false;
      if (S.node_sort == 0) swap = y.last_ms > x.last_ms;
      else if (S.node_sort == 1) swap = y.rssi > x.rssi;
      else swap = y.hops < x.hops || (y.hops == x.hops && y.last_ms > x.last_ms);
      if (!swap) break;
      const int t = out[b]; out[b] = out[b - 1]; out[b - 1] = t;
    }
  return n;
}

const char* kSortName[3] = {"Recent", "Signal", "Hops"};
const char* kFilterName[5] = {"All", "People", "NomadNet", "Propagation", "Calls"};
constexpr int NODE_ROW_Y = 72, NODE_RH = 33;

uint8_t svcIcon(int sv) { return sv == 0 ? I_USERS : sv == 1 ? I_ROUTER : sv == 2 ? I_NET : sv == 3 ? I_MSG : I_BCAST; }
uint16_t svcCol(int sv) { return sv == 0 ? T.orange : sv == 1 ? T.violet : sv == 2 ? T.blue : sv == 3 ? T.green : T.sub; }

void heardNodes(Fb& fb, uint32_t now) {
  char b[48];
  int x = X0;
  for (int i = 0; i < 3; ++i) {
    const bool on = i == S.node_sort;
    x = fb.chip(x, 51, kSortName[i], on ? T.accent_bg : T.card, on ? T.accent : T.sub, F_SM) + 4;
  }
  snprintf(b, sizeof b, "%s  v", kFilterName[S.node_filter]);
  fb.chip(XR - Fb::txtW(b, F_SM) - 10, 51, b, S.node_filter ? T.accent_bg : T.card, S.node_filter ? T.accent : T.sub, F_SM);
  int idx[kMaxStations];
  const int n = nodeList(idx, kMaxStations);
  if (!n) { fb.txt(X0 + 6, NODE_ROW_Y + 8, "No stations yet", T.muted, F_BODY); return; }
  const int rows = (H - NODE_ROW_Y - 2) / NODE_RH, off = S.scroll[PG_LIST] < n ? S.scroll[PG_LIST] : 0;
  for (int r = 0; r < rows && r + off < n; ++r) {
    const Station& st = S.st[idx[r + off]];
    const int y = NODE_ROW_Y + r * NODE_RH;
    if (r) fb.hline(X0 + 36, y, CW - 42, T.stroke);
    const uint16_t c = svcCol(st.service);
    fb.fillRound(X0 + 5, y + 4, 24, 24, 7, tint(c));
    fb.icon(X0 + 10, y + 9, svcIcon(st.service), 13, c);
    nameOf(st.dest, idx[r + off], b, sizeof b);
    if (watched(st.dest)) { char t[52]; snprintf(t, sizeof t, "* %s", b); snprintf(b, sizeof b, "%s", t); }
    Fb::fit(b, 150, F_BOLD);
    fb.txt(X0 + 36, y + 3, b, named(st.dest, idx[r + off]) ? T.text : T.sub, F_BOLD);
    char via[24] = "";
    if (st.has_via && !S.privacy) { const char* nk = nickname(st.via); char h[16]; hexShort(st.via, h); snprintf(via, sizeof via, "  via %s", nk ? nk : h); }
    snprintf(b, sizeof b, "%s  %u hop%s  %lux%s", st.service >= 0 ? kServices[st.service].label : "node", st.hops, st.hops == 1 ? "" : "s", (unsigned long)st.count, via);
    Fb::fit(b, 170, F_SM);
    fb.txt(X0 + 36, y + 18, b, T.muted, F_SM);
    snprintf(b, sizeof b, "%d", st.rssi);
    fb.txtR(XR - 6, y + 3, b, st.rssi > -90 ? T.green : (st.rssi > -110 ? T.text : T.orange), F_BOLD);
    char ago[8]; fmtAgo(now - st.last_ms, ago, sizeof ago);
    fb.txtR(XR - 6, y + 18, ago, T.muted, F_SM);
  }
}

void signalHistory(Fb& fb, uint32_t now) {
  (void)now;
  char b[40];
  const uint32_t u = unixNow();
  const int y1 = TOP + 2, h1 = 104, y2 = y1 + h1 + 4, h2 = H - 4 - y2;
  card(fb, X0, y1, CW, h1);
  cardHead(fb, X0, y1, I_CHART, T.accent, "LAST 24 HOURS");
  { const int wn = Fb::txtW("noise", F_SM), wp = Fb::txtW("packets", F_SM);
    fb.txtR(XR - 8, y1 + 6, "noise", T.muted, F_SM); fb.fillRound(XR - 8 - wn - 10, y1 + 10, 6, 6, 2, T.orange);
    fb.txtR(XR - 8 - wn - 18, y1 + 6, "packets", T.muted, F_SM); fb.fillRound(XR - 8 - wn - 28 - wp, y1 + 10, 6, 6, 2, T.accent); }
  if (!u) { fb.txtC(X0 + CW / 2, y1 + 50, "needs GPS time", T.muted, F_SM); }
  else {
    uint16_t mx = 4;
    for (int i = 0; i < 24; ++i) { const HourBin& hb = S.hours[i]; if (hb.hour + 24 > u / 3600 && hb.pkts > mx) mx = hb.pkts; }
    const int gx = X0 + 10, gw = CW - 20, gy = y1 + 26, gh = h1 - 44, bw = gw / 24;
    float lx = -1, ly = 0;
    for (int k = 0; k < 24; ++k) {                     // oldest hour on the left
      const uint32_t hr = u / 3600 - 23 + k;
      const HourBin& hb = S.hours[hr % 24];
      const bool ok = hb.hour == hr;
      const int bh = ok ? hb.pkts * gh / mx : 0;
      const int x = gx + k * bw;
      fb.fillRound(x, gy + gh - (bh > 2 ? bh : 2), bw - 2, bh > 2 ? bh : 2, 1, ok && hb.pkts ? T.accent : T.stroke);
      if (ok && hb.noise_n) {
        const float nf = (float)hb.noise_sum / hb.noise_n;           // -125..-85 dBm
        float v = (nf + 125) / 40.0f; v = v < 0 ? 0 : v > 1 ? 1 : v;
        const float yy = gy + gh - v * gh, xx = x + bw / 2.0f;
        if (lx >= 0) fb.lineAA(lx, ly, xx, yy, T.orange);
        lx = xx; ly = yy;
      }
    }
    const long lt = (long)u + S.tz_min * 60L;
    for (int k = 0; k < 24; k += 6) {
      const int hh = (int)(((lt / 3600) - 23 + k) % 24 + 24) % 24;
      snprintf(b, sizeof b, "%d%s", S.clock12 ? (hh % 12 ? hh % 12 : 12) : hh, S.clock12 ? (hh < 12 ? "a" : "p") : "h");
      fb.txt(gx + k * bw, gy + gh + 3, b, T.muted, F_SM);
    }
    fb.txtR(gx + gw, gy + gh + 3, "now", T.muted, F_SM);
  }
  card(fb, X0, y2, CW, h2);
  cardHead(fb, X0, y2, I_USERS, T.violet, "LAST 7 DAYS");
  fb.txtR(XR - 8, y2 + 6, "nodes per day", T.muted, F_SM);
  if (u) {
    uint16_t mx = 2;
    for (int i = 0; i < 7; ++i) if (S.days[i].day + 7 > u / 86400 && S.days[i].nodes > mx) mx = S.days[i].nodes;
    static const char* wd[] = {"Th", "Fr", "Sa", "Su", "Mo", "Tu", "We"};
    const int gx = X0 + 10, bw = (CW - 20) / 7, gy = y2 + 36, gh = h2 - 56;
    for (int k = 0; k < 7; ++k) {
      const uint32_t d = u / 86400 - 6 + k;
      const DayBin& db = S.days[d % 7];
      const bool ok = db.day == d;
      const int bh = ok ? db.nodes * gh / mx : 0;
      const int x = gx + k * bw;
      fb.fillRound(x + 3, gy + gh - (bh > 2 ? bh : 2), bw - 8, bh > 2 ? bh : 2, 3, ok && db.nodes ? T.violet : T.stroke);
      if (ok && db.nodes) { snprintf(b, sizeof b, "%u", db.nodes); fb.txtC(x + bw / 2 - 1, gy + gh - bh - 13, b, T.text, F_SM); }
      fb.txtC(x + bw / 2 - 1, gy + gh + 3, wd[((long)d % 7 + 7) % 7], k == 6 ? T.accent : T.muted, F_SM);
    }
  }
}

void areaSky(Fb& fb, uint32_t now) {
  (void)now;
  char b[40];
  const int cx = X0 + 90, cy = TOP + 92, r = 84;
  card(fb, X0, TOP + 2, CW, H - TOP - 6);
  // sky dome: horizon, 30 and 60 degree rings, N/E/S/W
  for (int k = 1; k <= 3; ++k) fb.arc(cx, cy, r * k / 3, 1, 0, 360, T.stroke);
  fb.hline(cx - r, cy, 2 * r, T.stroke); fb.vline(cx, cy - r, 2 * r, T.stroke);
  fb.txtC(cx, cy - r - 1, "N", T.sub, F_SM); fb.txtC(cx, cy + r - 12, "S", T.muted, F_SM);
  fb.txt(cx + r - 9, cy - 13, "E", T.muted, F_SM); fb.txt(cx - r + 2, cy - 13, "W", T.muted, F_SM);
  int best[kSats]; int nb = 0;
  for (int i = 0; i < S.sats_n; ++i) {
    const SatInfo& s = S.sats_v[i];
    if (s.elev < 0) continue;
    const float rr = r * (90 - s.elev) / 90.0f, a = s.az * 3.14159f / 180.0f;
    const int px = cx + (int)(rr * sinf(a)), py = cy - (int)(rr * cosf(a));
    const uint16_t c = s.snr >= 35 ? T.green : s.snr >= 25 ? T.yellow : s.snr > 0 ? T.orange : T.muted;
    fb.fillCircle(px, py, 5, c);
    snprintf(b, sizeof b, "%u", s.prn);
    fb.txt(px + 6, py - 6, b, T.sub, F_SM);
    best[nb++] = i;
  }
  for (int a = 1; a < nb; ++a) for (int k = a; k > 0 && S.sats_v[best[k]].snr > S.sats_v[best[k - 1]].snr; --k) { int t = best[k]; best[k] = best[k - 1]; best[k - 1] = t; }
  const int lx = X0 + 186;
  fb.txt(lx, TOP + 8, "SATELLITES", T.sub, F_SM);
  snprintf(b, sizeof b, "%d", S.sats_n); fb.txt(lx, TOP + 22, b, T.text, F_BIG);
  snprintf(b, sizeof b, "in view, %d used", S.sats_used); fb.txt(lx, TOP + 48, b, T.muted, F_SM);
  if (S.hdop > 0) { snprintf(b, sizeof b, "HDOP %.1f", S.hdop); fb.txt(lx, TOP + 62, b, S.hdop < 2 ? T.green : T.sub, F_SM); }
  for (int k = 0; k < nb && k < 6; ++k) {
    const SatInfo& s = S.sats_v[best[k]];
    const int y = TOP + 82 + k * 16;
    snprintf(b, sizeof b, "%c%u", s.sys, s.prn); fb.txt(lx, y, b, T.sub, F_SM);
    progress(fb, lx + 34, y + 5, 30, s.snr > 50 ? 256 : s.snr * 256 / 50, s.snr >= 35 ? T.green : s.snr >= 25 ? T.yellow : T.orange);
    snprintf(b, sizeof b, "%d", s.snr); fb.txtR(XR - 8, y, b, T.text, F_SM);
  }
  if (!S.sats_n) fb.txt(lx, TOP + 82, "waiting for GPS", T.muted, F_SM);
}

void radiosNetwork(Fb& fb, uint32_t now) {
  (void)now;
  char b[24];
  card(fb, X0, TOP + 2, CW, H - TOP - 6);
  const int cx = X0 + CW / 2, cy = TOP + 86;
  uint8_t relays[8][16]; int nr = 0;
  int rcount[8] = {0};
  int direct = 0, via_n = 0;
  for (int i = 0; i < kMaxStations; ++i) {
    const Station& s = S.st[i];
    if (!s.used) continue;
    if (!s.has_via) { direct++; continue; }
    via_n++;
    int k = 0;
    for (; k < nr; ++k) if (!memcmp(relays[k], s.via, 16)) break;
    if (k == nr && nr < 8) memcpy(relays[nr++], s.via, 16);
    if (k < 8) rcount[k]++;
  }
  // direct stations on an inner ring, relays on the middle ring, their stations outside
  int di = 0;
  for (int i = 0; i < kMaxStations; ++i) {
    const Station& s = S.st[i];
    if (!s.used || s.has_via) continue;
    const float a = di++ * 6.2832f / (direct ? direct : 1);
    const int px = cx + (int)(34 * cosf(a)), py = cy + (int)(30 * sinf(a));
    fb.lineAA(cx, cy, px, py, T.stroke);
    fb.fillCircle(px, py, 3, svcCol(s.service));
  }
  for (int k = 0; k < nr; ++k) {
    const float a = k * 6.2832f / nr - 1.5708f;
    const int rx = cx + (int)(82 * cosf(a)), ry = cy - 8 + (int)(46 * sinf(a));
    fb.lineAA(cx, cy, rx, ry, dimc(T.violet, 160));
    int si = 0;
    for (int i = 0; i < kMaxStations; ++i) {
      const Station& s = S.st[i];
      if (!s.used || !s.has_via || memcmp(s.via, relays[k], 16)) continue;
      const float sa = a + (si++ - (rcount[k] - 1) / 2.0f) * 0.22f;
      const int px = rx + (int)(26 * cosf(sa)), py = ry + (int)(22 * sinf(sa));
      fb.lineAA(rx, ry, px, py, T.stroke);
      fb.fillCircle(px, py, 3, svcCol(s.service));
    }
    fb.fillCircle(rx, ry, 7, T.violet);
    fb.icon(rx - 6, ry - 6, I_ROUTER, 13, T.bg);
    const char* nk = nickname(relays[k]);
    if (nk && !S.privacy) snprintf(b, sizeof b, "%s", nk);
    else snprintf(b, sizeof b, "%02x%02x", relays[k][0], relays[k][1]);
    Fb::fit(b, 70, F_SM);
    fb.txtC(rx, ry + 8, b, T.sub, F_SM);
  }
  fb.fillCircle(cx, cy, 9, T.accent);
  fb.icon(cx - 6, cy - 6, I_ANT, 13, T.bg);
  snprintf(b, sizeof b, "%d relay%s", nr, nr == 1 ? "" : "s");
  fb.txt(X0 + 8, H - 22, b, T.violet, F_SM);
  snprintf(b, sizeof b, "%d direct", direct);
  fb.txtC(X0 + CW / 2, H - 22, b, T.text, F_SM);
  snprintf(b, sizeof b, "%d via relays", via_n);
  fb.txtR(XR - 8, H - 22, b, T.sub, F_SM);
}

// ---- tap-opened views --------------------------------------------------------
constexpr int OV_X = X0 - 2, OV_Y = 26, OV_W = CW + 4, OV_H = H - 30;

void closeButton(Fb& fb, int x, int y) {
  fb.fillCircle(x, y, 11, T.card2);
  fb.lineAA(x - 4, y - 4, x + 4, y + 4, T.text); fb.lineAA(x + 4, y - 4, x - 4, y + 4, T.text);
}
int button(Fb& fb, int x, int y, const char* label, uint8_t ic, uint16_t c) {
  const int w = Fb::txtW(label, F_BOLD) + (ic < I_COUNT ? 34 : 18);
  fb.fillRound(x, y, w, 28, 9, tint(c));
  int tx = x + 9;
  if (ic < I_COUNT) { fb.icon(tx, y + 7, ic, 13, c); tx += 18; }
  fb.txt(tx, y + 6, label, c, F_BOLD);
  return x + w;
}

void viewStation(Fb& fb, uint32_t now) {
  if (S.ov_idx < 0 || !S.st[S.ov_idx].used) { S.ov = OV_NONE; return; }
  const Station& st = S.st[S.ov_idx];
  char b[64];
  fb.fillRound(OV_X, OV_Y, OV_W, OV_H, 12, T.card);
  const uint16_t c = svcCol(st.service);
  fb.fillRound(OV_X + 8, OV_Y + 8, 34, 34, 9, tint(c));
  fb.icon(OV_X + 16, OV_Y + 16, svcIcon(st.service), 18, c);
  nameOf(st.dest, S.ov_idx, b, sizeof b); Fb::fit(b, OV_W - 90, F_TITLE);
  fb.txt(OV_X + 50, OV_Y + 7, b, T.text, F_TITLE);
  snprintf(b, sizeof b, "%s%s", st.service >= 0 ? kServices[st.service].aspect : "unknown app", watched(st.dest) ? "   * watched" : "");
  Fb::fit(b, OV_W - 90, F_SM);
  fb.txt(OV_X + 50, OV_Y + 27, b, T.sub, F_SM);
  closeButton(fb, OV_X + OV_W - 18, OV_Y + 18);
  // full hash
  char hx[33];
  for (int i = 0; i < 16; ++i) snprintf(hx + 2 * i, 3, "%02x", st.dest[i]);
  fb.txt(OV_X + 10, OV_Y + 48, S.privacy ? "* * * *" : hx, T.muted, F_SM);
  // facts grid
  char ago[8], first[8];
  fmtAgo(now - st.last_ms, ago, sizeof ago); fmtAgo(now - st.first_ms, first, sizeof first);
  struct { const char* k; char v[28]; } f[6];
  f[0].k = "LAST HEARD"; snprintf(f[0].v, 28, "%s ago", ago);
  f[1].k = "FIRST HEARD"; snprintf(f[1].v, 28, "%s ago", first);
  f[2].k = "PACKETS"; snprintf(f[2].v, 28, "%lu", (unsigned long)st.count);
  f[3].k = "SIGNAL"; snprintf(f[3].v, 28, "%d / %.1f", st.rssi, st.snr);
  f[4].k = "HOPS"; snprintf(f[4].v, 28, "%u", st.hops);
  f[5].k = "VIA"; if (st.has_via && !S.privacy) { const char* nk = nickname(st.via); char h[16]; hexShort(st.via, h); snprintf(f[5].v, 28, "%s", nk ? nk : h); } else snprintf(f[5].v, 28, "direct");
  for (int i = 0; i < 6; ++i) {
    const int x = OV_X + 10 + (i % 3) * 88, y = OV_Y + 66 + (i / 3) * 34;
    fb.txt(x, y, f[i].k, T.muted, F_SM);
    Fb::fit(f[i].v, 84, F_BOLD);
    fb.txt(x, y + 13, f[i].v, T.text, F_BOLD);
  }
  // RSSI sparkline of the last packets
  const int gx = OV_X + 10, gy = OV_Y + 138, gw = OV_W - 20, gh = 26;
  fb.fillRound(gx, gy, gw, gh, 6, T.card2);
  for (int i = 0; i < st.hist_n; ++i) {
    const int v = st.hist[(st.hist_head - st.hist_n + i + 16) % 16];
    int bh = (v + 130) * (gh - 6) / 70; bh = bh < 2 ? 2 : bh > gh - 6 ? gh - 6 : bh;
    fb.fillRound(gx + 6 + i * ((gw - 12) / 16), gy + gh - 3 - bh, (gw - 12) / 16 - 3, bh, 1, v > -90 ? T.green : v > -110 ? T.accent : T.orange);
  }
  button(fb, OV_X + 10, OV_Y + OV_H - 36, "Track signal", I_RADAR, T.accent);
}

void viewPacket(Fb& fb, uint32_t now) {
  if (S.ov_idx < 0) { S.ov = OV_NONE; return; }
  const LogEntry& e = S.log[S.ov_idx];
  char b[64];
  fb.fillRound(OV_X, OV_Y, OV_W, OV_H, 12, T.card);
  const uint16_t kc = e.tx ? T.accent : kindCol(e.kind);
  fb.fillRound(OV_X + 8, OV_Y + 8, 30, 30, 8, tint(kc));
  fb.icon(OV_X + 14, OV_Y + 14, e.tx ? I_ANT : kindIcon(e.kind), 18, kc);
  static const char* ptn[] = {"DATA", "ANNOUNCE", "LINKREQUEST", "PROOF"};
  static const char* dtn[] = {"SINGLE", "GROUP", "PLAIN", "LINK"};
  snprintf(b, sizeof b, "%s packet%s", kindLabel(e.kind), e.tx ? " (you sent)" : "");
  fb.txt(OV_X + 46, OV_Y + 7, b, T.text, F_TITLE);
  char ago[8]; fmtAgo(now - e.t_ms, ago, sizeof ago);
  snprintf(b, sizeof b, "%s ago  -  %u bytes  -  %lu ms on air", ago, e.len, (unsigned long)airtimeMsForBytes(e.len));
  fb.txt(OV_X + 46, OV_Y + 26, b, T.sub, F_SM);
  closeButton(fb, OV_X + OV_W - 18, OV_Y + 18);
  char hx[33], tid[33] = "-";
  for (int i = 0; i < 16; ++i) snprintf(hx + 2 * i, 3, "%02x", e.dest[i]);
  if (e.has_via) for (int i = 0; i < 16; ++i) snprintf(tid + 2 * i, 3, "%02x", e.via[i]);
  struct { const char* k; char v[48]; } r[8];
  r[0].k = "Flags"; snprintf(r[0].v, 48, "0x%02x  header %d%s%s", e.flags, (e.flags >> 6) & 1 ? 2 : 1, (e.flags >> 5) & 1 ? "  ctx-flag" : "", (e.flags >> 7) ? "  IFAC" : "");
  r[1].k = "Packet"; snprintf(r[1].v, 48, "%s", ptn[e.ptype & 3]);
  r[2].k = "Dest type"; snprintf(r[2].v, 48, "%s%s", dtn[e.dtype & 3], ((e.flags >> 4) & 1) ? "  (transport)" : "");
  r[3].k = "Destination"; snprintf(r[3].v, 48, "%s", S.privacy ? "* * *" : hx);
  r[4].k = "Relay ID"; snprintf(r[4].v, 48, "%s", S.privacy && e.has_via ? "* * *" : tid);
  r[5].k = "Context"; snprintf(r[5].v, 48, "0x%02x %s", e.context, contextLabel(e.context));
  r[6].k = "Hops"; snprintf(r[6].v, 48, "%u", e.hops);
  r[7].k = "Signal"; if (e.tx) snprintf(r[7].v, 48, "sent by this RNode"); else snprintf(r[7].v, 48, "%d dBm   SNR %.1f dB", e.rssi, e.snr);
  for (int i = 0; i < 8; ++i) {
    const int y = OV_Y + 46 + i * 18;
    fb.txt(OV_X + 10, y, r[i].k, T.muted, F_SM);
    Fb::fit(r[i].v, OV_W - 96, F_SM);
    fb.txt(OV_X + 84, y, r[i].v, T.text, F_SM);
    if (i) fb.hline(OV_X + 10, y - 3, OV_W - 20, T.stroke);
  }
  fb.txt(OV_X + 10, OV_Y + OV_H - 16, "Header only: the contents stay encrypted.", T.muted, F_SM);
}

void viewTracker(Fb& fb, uint32_t now) {
  if (S.ov_idx < 0 || !S.st[S.ov_idx].used) { S.ov = OV_NONE; return; }
  const Station& st = S.st[S.ov_idx];
  char b[48];
  fb.clear(T.bg);
  fb.icon(12, 10, I_RADAR, 18, T.accent);
  nameOf(st.dest, S.ov_idx, b, sizeof b); Fb::fit(b, 230, F_TITLE);
  fb.txt(36, 9, b, T.text, F_TITLE);
  closeButton(fb, W - 18, 18);
  const int sf = S.radio.sf ? S.radio.sf : 9;
  const float floor_db = -7.5f - 2.5f * (sf - 7);
  float m = st.snr - floor_db; m = m < 0 ? 0 : m > 30 ? 30 : m;
  const bool fresh = now - st.last_ms < 60000;
  const int cx = W / 2, cy = 150;
  fb.arc(cx, cy, 96, 14, 0, 180, T.stroke);
  const uint16_t c = m > 15 ? T.green : m > 7 ? T.yellow : T.orange;
  if (m > 0.3f) fb.arc(cx, cy, 96, 14, (int)(180 - m / 30 * 180), 180, fresh ? c : T.muted);
  // needle
  const float a = (180 - m / 30 * 180) * 3.14159f / 180.0f;
  fb.fillCircle(cx + (int)(89 * cosf(a)), cy - (int)(89 * sinf(a)), 9, T.text);
  fb.fillCircle(cx + (int)(89 * cosf(a)), cy - (int)(89 * sinf(a)), 5, fresh ? c : T.muted);
  snprintf(b, sizeof b, "%d", st.rssi);
  fb.txtC(cx, cy - 64, b, fresh ? T.text : T.muted, F_HUGE);
  fb.txtC(cx, cy - 16, "dBm", T.sub, F_BOLD);
  snprintf(b, sizeof b, "SNR %.1f dB   margin %.0f dB over the SF%d floor", st.snr, m, sf);
  fb.txtC(cx, cy + 8, b, T.sub, F_SM);
  char ago[8]; fmtAgo(now - st.last_ms, ago, sizeof ago);
  snprintf(b, sizeof b, "last packet %s ago  -  higher beep = stronger", ago);
  Fb::fit(b, W - 20, F_SM);
  fb.txtC(cx, H - 56, b, fresh ? T.muted : T.orange, F_SM);
  // last packets
  for (int i = 0; i < st.hist_n; ++i) {
    const int v = st.hist[(st.hist_head - st.hist_n + i + 16) % 16];
    int bh = (v + 130) * 30 / 70; bh = bh < 2 ? 2 : bh > 30 ? 30 : bh;
    fb.fillRound(40 + i * 15, H - 8 - bh, 11, bh, 2, v > -90 ? T.green : v > -110 ? T.accent : T.orange);
  }
}

void viewQuick(Fb& fb, uint32_t now) {
  const int slide = (int)((now - S.ov_ms) > 200 ? 256 : (now - S.ov_ms) * 256 / 200);
  const int h = 168, y = -h + h * slide / 256;
  fb.shade(0, 0, W, H, 120);
  fb.fillRound(6, y - 12, W - 12, h + 12, 14, T.card);
  const Settings& st = settings();
  struct { const char* l; const char* s; uint8_t ic; bool on; } t[4] = {
    {"Sounds", st.sounds ? "on" : "off", I_BCAST, st.sounds},
    {"Privacy", S.privacy ? "on" : "off", I_INFO, S.privacy},
    {"Screen off", "tap to wake", I_CLOCK, false},
    {"Theme", st.theme, I_CHART, true}};
  for (int i = 0; i < 4; ++i) {
    const int x = 14 + i * 74, yy = y + 10;
    fb.fillRound(x, yy, 70, 58, 10, t[i].on ? T.accent_bg : T.card2);
    fb.icon(x + 8, yy + 8, t[i].ic, 18, t[i].on ? T.accent : T.sub);
    fb.txt(x + 8, yy + 29, t[i].l, T.text, F_BOLD);
    fb.txt(x + 8, yy + 43, t[i].s, T.muted, F_SM);
  }
  // brightness slider
  const int sy = y + 82;
  fb.icon(16, sy + 2, I_INFO, 13, T.sub);
  fb.txt(34, sy, "Brightness", T.sub, F_SM);
  char b[40]; snprintf(b, sizeof b, "%d%%", st.brightness);
  fb.txtR(W - 18, sy, b, T.text, F_SM);
  fb.fillRound(16, sy + 20, W - 32, 8, 4, T.stroke);
  const int kx = 16 + (W - 32) * (st.brightness - 5) / 95;
  fb.fillRound(16, sy + 20, kx - 16 > 8 ? kx - 16 : 8, 8, 4, T.accent);
  fb.fillCircle(kx, sy + 24, 9, rgb(0xFFFFFF));
  // info + handle
  if (S.web[0]) snprintf(b, sizeof b, "%s   %s", S.web, S.fw);
  else snprintf(b, sizeof b, "%s", S.fw);
  Fb::fit(b, W - 32, F_SM);
  fb.txt(16, sy + 42, b, T.muted, F_SM);
  fb.fillRound(W / 2 - 20, y + h - 9, 40, 4, 2, T.muted);
}

}  // namespace

// --- quick panel setters live in cui_core.cpp ---
void qpSetBrightness(int pct);
void qpToggleSounds();
void qpTogglePrivacy();
void qpNextTheme();
void qpScreenOff();

bool onTap(int x, int y) {
  // tap-opened views first
  if (S.ov == OV_QUICK) {
    const int h = 168;
    if (y > h) { S.ov = OV_NONE; return true; }
    if (y >= 10 && y < 68) {
      const int i = (x - 14) / 74;
      if (i == 0) qpToggleSounds(); else if (i == 1) qpTogglePrivacy(); else if (i == 2) qpScreenOff(); else if (i == 3) qpNextTheme();
      return true;
    }
    if (y >= 92 && y < 126) { qpSetBrightness(5 + (x - 16) * 95 / (W - 32)); return true; }
    return true;
  }
  if (S.ov == OV_TRACK) { if (x > W - 44 && y < 44) S.ov = OV_NONE; return true; }
  if (S.ov == OV_STATION || S.ov == OV_PACKET) {
    if (x > OV_X + OV_W - 40 && y < OV_Y + 40) { S.ov = OV_NONE; return true; }
    if (S.ov == OV_STATION && y > OV_Y + OV_H - 40 && x < OV_X + 140) { S.ov = OV_TRACK; return true; }
    return true;
  }
  if (S.retro) return false;
  if (x < 60) {                                       // the rail
    const int p = (y - 4) / 33;
    if (p < 0 || p >= PG_COUNT) return false;
    setPage(p);
    return true;
  }
  const bool tabbed = S.page == PG_LIST || S.page == PG_GRAPHS || S.page == PG_AREA || S.page == PG_IFACES;
  if (tabbed && y >= TAB_Y && y < TAB_Y + TAB_H) {
    if (S.page == PG_LIST && x >= XR - 58) {           // scroll buttons
      const int step = 3;
      if (x < XR - 28) S.scroll[PG_LIST] = S.scroll[PG_LIST] > step ? S.scroll[PG_LIST] - step : 0;
      else if (S.scroll[PG_LIST] + step < kMaxStations) S.scroll[PG_LIST] += step;
      return true;
    }
    const int w = S.page == PG_LIST ? CW - 60 : CW;
    const int t = (x - X0) * 2 / w;
    if (t >= 0 && t < 2) { S.tab[S.page] = (uint8_t)t; S.scroll[S.page] = 0; }
    return true;
  }
  if (S.page == PG_LIST && S.tab[PG_LIST] == 0 && y >= 54) {          // packet -> inspector
    const int i = (y - 54) / 34 + S.scroll[PG_LIST];
    if (i < S.log_count) { S.ov = OV_PACKET; S.ov_idx = (S.log_head - 1 - i + kLogLen) % kLogLen; }
    return true;
  }
  if (S.page == PG_LIST && S.tab[PG_LIST] == 1) {
    if (y >= 50 && y < 68) {                                          // sort / filter chips
      if (x > XR - 90) S.node_filter = (uint8_t)((S.node_filter + 1) % 5);
      else { int cx = X0; for (int i = 0; i < 3; ++i) { const int w = Fb::txtW(kSortName[i], F_SM) + 10;
        if (x >= cx && x < cx + w + 4) S.node_sort = (uint8_t)i; cx += w + 4; } }
      S.scroll[PG_LIST] = 0;
      return true;
    }
    if (y >= NODE_ROW_Y) {                                            // station -> details
      int idx[kMaxStations];
      const int n = nodeList(idx, kMaxStations);
      const int r = (y - NODE_ROW_Y) / NODE_RH + S.scroll[PG_LIST];
      if (r < n) { S.ov = OV_STATION; S.ov_idx = idx[r]; }
      return true;
    }
  }
  if (S.page == PG_STATUS && S.host && x >= ANN_X && y >= ANN_Y && y < ANN_Y + 28) {
    snprintf(S.host_out, sizeof S.host_out, "announce");
    notify(N_INFO, "Asked your computer to announce", S.now_ms);
    return true;
  }
  return false;
}

namespace {

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
    case N_WATCH:     pix = kPopNode; pal = kPopNodePal; snprintf(title, sizeof title, "In range!"); snprintf(body, sizeof body, "%s", S.privacy ? "* * *" : n.text); accent = T.yellow; break;
    case N_INFO:      pix = kPopHostUp; pal = kPopHostUpPal; snprintf(title, sizeof title, "Done"); snprintf(body, sizeof body, "%s", n.text); accent = T.accent; break;
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
  if (S.ov == OV_TRACK && !S.retro) { viewTracker(fb, now); drawNote(fb, now); return; }
  if (S.retro) {
    renderRetro(fb, now);
    if (S.ov == OV_QUICK) viewQuick(fb, now);
    return;
  }
  if (S.idle && !wantWake()) { screensaver(fb, now); return; }
  fb.clear(T.bg);
  static const char* const kHeardTabs[2] = {"Packets", "Nodes"};
  static const char* const kSigTabs[2] = {"Live", "History"};
  static const char* const kAreaTabs[2] = {"Map", "Sky"};
  static const char* const kRadioTabs[2] = {"Interfaces", "Network"};
  TOP = 26;
  switch (S.page) {
    case PG_HEARD:   pageHome(fb, now); break;
    case PG_LIST:
      TOP = 52;
      if (S.tab[PG_LIST] == 0) heardPackets(fb, now); else heardNodes(fb, now);
      TOP = 26;
      tabs(fb, kHeardTabs, 2, S.tab[PG_LIST], CW - 60); scrollButtons(fb);
      break;
    case PG_GRAPHS:
      TOP = 52;
      if (S.tab[PG_GRAPHS] == 0) signalLive(fb, now); else signalHistory(fb, now);
      TOP = 26; tabs(fb, kSigTabs, 2, S.tab[PG_GRAPHS], CW);
      break;
    case PG_CHANNEL: pageChannel(fb, now); break;
    case PG_AREA:
      TOP = 52;
      if (S.tab[PG_AREA] == 0) areaMap(fb, now); else areaSky(fb, now);
      TOP = 26; tabs(fb, kAreaTabs, 2, S.tab[PG_AREA], CW);
      break;
    case PG_IFACES:
      TOP = 52;
      if (S.tab[PG_IFACES] == 0) radiosIfaces(fb, now); else radiosNetwork(fb, now);
      TOP = 26; tabs(fb, kRadioTabs, 2, S.tab[PG_IFACES], CW);
      break;
    default:         pageStatus(fb, now); break;
  }
  rail(fb);
  statusBar(fb, now);
  if (S.ov == OV_STATION) viewStation(fb, now);
  else if (S.ov == OV_PACKET) viewPacket(fb, now);
  drawNote(fb, now);
  if (S.ov == OV_QUICK) viewQuick(fb, now);
  if (settings().touch_debug && S.touch_ms && now - S.touch_ms < 1500) {
    fb.fillCircle(S.touch_x, S.touch_y, 7, T.red);
    fb.fillCircle(S.touch_x, S.touch_y, 3, rgb(0xFFFFFF));
    char b[24]; snprintf(b, sizeof b, "touch %d,%d", S.touch_x, S.touch_y);
    fb.chip(X0 + 2, H - 20, b, T.red, rgb(0xFFFFFF), F_SM);
  }
}

}  // namespace cui
