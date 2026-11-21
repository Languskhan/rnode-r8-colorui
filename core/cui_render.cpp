// Page drawing. Everything is drawn into the 320x240 RGB565 framebuffer;
// the device flushes it to the ST7789 with DMA, the simulator saves PNGs.
#include "cui_state.h"
#include "cui_popup_art.h"
#include "cui_names.h"
#include "cui_art.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace cui {
namespace {

const char* kPageTitle[PG_COUNT] = {"NOW HEARD", "LAST HEARD", "GRAPHS", "CHANNEL", "AREA", "INTERFACES", "STATUS"};

uint16_t kindColor(Kind k) {
  switch (k) {
    case K_ANNOUNCE: case K_PATHRESP: return col::orange;
    case K_DATA: case K_RESOURCE:     return col::cyan;
    case K_LINK: case K_LINKCLOSE: case K_KEEPALIVE: return col::violet;
    case K_PATHREQ:                   return col::yellow;
    default:                          return col::sub;
  }
}

void fmtAgo(uint32_t ms, char* o, size_t n) {
  const uint32_t s = ms / 1000;
  if (s < 60) snprintf(o, n, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(o, n, "%lum", (unsigned long)(s / 60));
  else snprintf(o, n, "%luh", (unsigned long)(s / 3600));
}

void fmtBytes(uint32_t b, char* o, size_t n) {
  if (b < 10000) snprintf(o, n, "%luB", (unsigned long)b);
  else if (b < 10000000) snprintf(o, n, "%luK", (unsigned long)(b / 1024));
  else snprintf(o, n, "%luM", (unsigned long)(b / 1048576));
}

void fmtClock(char* o, size_t n) {
  const uint32_t u = unixNow();
  if (!u) { snprintf(o, n, "--:--"); return; }
  const long t = (long)u + S.tz_min * 60L;
  snprintf(o, n, "%02ld:%02ld", (t / 3600) % 24, (t / 60) % 60);
}

int localHour() {
  const uint32_t u = unixNow();
  if (!u) return 12;
  return (int)((((long)u + S.tz_min * 60L) / 3600) % 24);
}

// Name for a station or log entry, honoring privacy mode.
void displayName(const uint8_t* dest, int station, char* o, size_t n) {
  if (S.privacy) { snprintf(o, n, "########"); return; }
  if (station >= 0 && S.st[station].name[0]) { snprintf(o, n, "%s", S.st[station].name); return; }
  char h[14]; hexShort(dest, h); snprintf(o, n, "%s", h);
}

void fitText(char* s, int max_chars) {
  if ((int)strlen(s) > max_chars && max_chars > 2) { s[max_chars - 2] = '.'; s[max_chars - 1] = '.'; s[max_chars] = 0; }
}

void bars(Fb& fb, int x, int y, int level, int n, uint16_t on) {
  for (int i = 0; i < n; ++i) fb.fill(x + i * 6, y, 5, 3, i < level ? on : col::line);
}

int rssiLevel(int r) { return r > -70 ? 5 : r > -85 ? 4 : r > -100 ? 3 : r > -112 ? 2 : r > -125 ? 1 : 0; }
int snrLevel(float s) { return qualityBars(s); }

void header(Fb& fb, uint32_t now) {
  fb.fill(0, 0, W, 14, col::panel);
  fb.hline(0, 14, W, col::line);
  const bool rx = now - S.last_rx_ms < 500 && S.last_rx_ms;
  const bool tx = now - S.last_tx_ms < 500 && S.last_tx_ms;
  int x = fb.pill(3, 2, "RX", rx ? col::greenbg : col::line, rx ? col::white : col::muted);
  x = fb.pill(x + 2, 2, "TX", tx ? col::txbg : col::line, tx ? col::white : col::muted);
  if (S.demo) fb.pill(x + 4, 2, "DEMO", col::violet, col::bg);
  if (S.privacy) fb.pill(x + (S.demo ? 40 : 4), 2, "PRIV", col::muted, col::bg);
  fb.textCenter(W / 2, 3, kPageTitle[S.page], col::sub);
  char clk[8]; fmtClock(clk, sizeof clk);
  const int xs = W - 3 - Fb::textWidth(clk);
  fb.text(xs, 3, clk, col::sub);
  // host link dot + GPS dot, left of the clock
  fb.fill(xs - 8, 5, 4, 4, S.host ? col::green : col::dim);
  fb.fill(xs - 14, 5, 4, 4, S.gps_fix ? col::cyan : col::dim);
}

void pageDots(Fb& fb) {
  const int total = PG_COUNT * 6 + 6;
  int x = W / 2 - total / 2;
  for (int i = 0; i < PG_COUNT; ++i) {
    if (i == S.page) { fb.fill(x, 234, 10, 3, col::cyan); x += 14; }
    else { fb.fill(x, 234, 3, 3, col::dim); x += 7; }
  }
}

// ---- 8-bit background with animated radio rings --------------------------
void drawArt(Fb& fb, uint32_t now) {
  const int h = localHour();
  const uint8_t* idx; const uint16_t* pal;
  if (h >= 8 && h < 17)                       { idx = kArt_day;    pal = kArtPal_day; }
  else if ((h >= 17 && h < 19) || (h >= 6 && h < 8)) { idx = kArt_sunset; pal = kArtPal_sunset; }
  else                                        { idx = kArt_night;  pal = kArtPal_night; }
  for (int y = 0; y < 60; ++y)
    for (int x = 0; x < 80; ++x) fb.fill(x * 4, y * 4, 4, 4, pal[idx[y * 80 + x]]);
  // rings: expand for 1.5 s after each TX (cyan) or RX (orange)
  const int ax = kArtAntX * 4 + 2, ay = kArtAntY * 4 + 2;
  struct { uint32_t t; uint16_t c; } ev[2] = {{S.last_tx_ms, col::cyan}, {S.last_rx_ms, col::orange}};
  for (auto& e : ev) {
    if (!e.t || now - e.t > 1500) continue;
    const float p = (now - e.t) / 1500.0f;
    for (int ring = 0; ring < 3; ++ring) {
      const float r = 10 + p * 70 + ring * 18;
      for (int a = -60; a <= 60; a += 3) {
        if (((a + 60) / 9) % 2) continue;
        const float rad = a * 3.14159f / 180.f;
        const int px = ax + (int)(r * cosf(rad)), py = ay + (int)(r * sinf(rad));
        fb.fill(px & ~3, py & ~3, 4, 4, e.c);       // snap to the 4x pixel grid
      }
    }
  }
}

// ---- pages --------------------------------------------------------------
void pageHeard(Fb& fb, uint32_t now) {
  drawArt(fb, now);
  fb.shade(0, 15, W, H - 15, 200);
  if (!S.log_count) {
    fb.panelBox(120, 150, 192, 40, col::panel, col::line);
    fb.text(130, 158, "LISTENING...", col::sub);
    fb.text(130, 172, "no packets yet", col::muted);
    return;
  }
  // newest received entry
  int k = -1;
  for (int i = 0; i < S.log_count; ++i) {
    const int j = (S.log_head - 1 - i + kLogLen) % kLogLen;
    if (!S.log[j].tx) { k = j; break; }
  }
  if (k < 0) k = (S.log_head - 1 + kLogLen) % kLogLen;
  const LogEntry& e = S.log[k];
  const int x = 118, y = 116, w = 196, h = 110;
  fb.shade(x, y, w, h, 70);
  fb.rect(x, y, w, h, col::cyan);
  int px = fb.pill(x + 6, y + 6, kindLabel(e.kind), kindColor(e.kind), col::bg);
  if (e.service >= 0) fb.text(px + 4, y + 7, kServices[e.service].label, col::sub);
  char ago[8]; fmtAgo(now - e.t_ms, ago, sizeof ago);
  char meta[24]; snprintf(meta, sizeof meta, "%s %dhop", ago, e.hops);
  fb.textRight(x + w - 6, y + 7, meta, col::sub);
  char name[40]; displayName(e.dest, e.station, name, sizeof name);
  if ((int)strlen(name) <= 13) fb.text(x + 6, y + 22, name, col::white, 2);
  else { fitText(name, 26); fb.text(x + 6, y + 26, name, col::white); }
  char h16[14]; hexShort(e.dest, h16);
  fb.text(x + 6, y + 42, S.privacy ? "<....>" : h16, col::muted);
  if (e.has_via && !S.privacy) {
    char v[24]; char vh[14]; hexShort(e.via, vh);
    snprintf(v, sizeof v, "via %s", vh);
    fb.textRight(x + w - 6, y + 42, v, col::violet);
  }
  // signal tiles
  char b[16];
  fb.panelBox(x + 6, y + 56, 88, 30, col::panel, col::line);
  fb.text(x + 10, y + 59, "RSSI", col::muted);
  snprintf(b, sizeof b, "%d", e.rssi); fb.text(x + 10, y + 68, b, col::text);
  bars(fb, x + 52, y + 70, rssiLevel(e.rssi), 5, col::cyan);
  fb.panelBox(x + 102, y + 56, 88, 30, col::panel, col::line);
  fb.text(x + 106, y + 59, "SNR", col::muted);
  snprintf(b, sizeof b, "%.1f", e.snr); fb.text(x + 106, y + 68, b, col::text);
  bars(fb, x + 148, y + 70, snrLevel(e.snr), 5, col::cyan);
  snprintf(b, sizeof b, "%uB", e.len);
  fb.text(x + 6, y + 94, b, col::sub);
  snprintf(b, sizeof b, "%lums air", (unsigned long)airtimeMsForBytes(e.len));
  fb.text(x + 50, y + 94, b, col::sub);
  snprintf(b, sizeof b, "#%lu", (unsigned long)S.rx_total);
  fb.textRight(x + w - 6, y + 94, b, col::sub);
  // area tag top-left over the sky
  if (S.area[0]) {
    char a[32]; snprintf(a, sizeof a, "%s", S.area); fitText(a, 18);
    fb.shade(4, 20, Fb::textWidth(a) + 8, 13, 90);
    fb.text(8, 23, a, col::yellow);
  }
}

void pageList(Fb& fb, uint32_t now) {
  fb.text(4, 19, "AGO", col::muted); fb.text(34, 19, "TYPE", col::muted);
  fb.text(80, 19, "STATION", col::muted); fb.textRight(262, 19, "HOP", col::muted);
  fb.textRight(316, 19, "RSSI", col::muted);
  fb.hline(0, 29, W, col::line);
  const int rows = 12;
  for (int i = 0; i < rows && i < S.log_count; ++i) {
    const int j = (S.log_head - 1 - i + kLogLen) % kLogLen;
    const LogEntry& e = S.log[j];
    const int y = 32 + i * 16;
    if (i == 0) fb.fill(0, y - 2, W, 15, col::panel);
    char ago[8]; fmtAgo(now - e.t_ms, ago, sizeof ago);
    fb.text(4, y + 2, ago, col::sub);
    fb.pill(32, y, kindLabel(e.kind), kindColor(e.kind), col::bg);
    char name[40];
    if (e.kind == K_PATHREQ) {
      char h[14]; hexShort(e.wanted, h);
      snprintf(name, sizeof name, "who has %s", S.privacy ? "<....>" : h);
    } else if (e.station < 0 && e.name[0] && !S.privacy) snprintf(name, sizeof name, "%s", e.name);
    else displayName(e.dest, e.station, name, sizeof name);
    if (e.tx) { char t[44]; snprintf(t, sizeof t, "> %s", name); strcpy(name, t); }
    fitText(name, 23);
    const bool hashOnly = !S.privacy && !e.name[0] && (e.station < 0 || !S.st[e.station].name[0]) && e.kind != K_PATHREQ;
    fb.text(80, y + 2, name, e.tx ? col::cyan : (hashOnly ? col::muted : col::text));
    char hb[6]; snprintf(hb, sizeof hb, "%d", e.hops);
    fb.textRight(258, y + 2, hb, col::sub);
    if (!e.tx) {
      char r[8]; snprintf(r, sizeof r, "%d", e.rssi);
      fb.textRight(316, y + 2, r, e.rssi > -90 ? col::cyan : (e.rssi > -105 ? col::text : col::orange));
    } else fb.textRight(316, y + 2, "TX", col::orange);
    fb.hline(0, y + 13, W, col::line);
  }
}

void pageGraphs(Fb& fb, uint32_t now) {
  (void)now;
  // signal history
  fb.panelBox(2, 18, 156, 104, col::panel, col::line);
  fb.text(6, 21, "SIGNAL", col::muted);
  fb.text(70, 21, "RSSI", col::cyan); fb.text(108, 21, "SNR", col::orange);
  for (int g = 0; g < 3; ++g) fb.hline(6, 45 + g * 22, 148, col::line);
  for (int pass = 0; pass < 2; ++pass) {
    int lx = -1, ly = 0;
    for (int i = 0; i < S.sig_n; ++i) {
      const int k = (S.sig_head - S.sig_n + i + kSigHist) % kSigHist;
      const int x = 6 + i * 148 / (kSigHist - 1);
      int y = pass == 0 ? 33 + (int)((-60 - S.sig_rssi[k]) * 70 / 70)   // -60..-130 dBm
                        : 103 - (int)((S.sig_snr[k] + 15) * 70 / 30);   // -15..+15 dB
      if (y < 31) y = 31;
      if (y > 110) y = 110;
      if (lx >= 0) { fb.line(lx, ly, x, y, pass ? col::orange : col::cyan); fb.line(lx, ly + 1, x, y + 1, pass ? col::orange : col::cyan); }
      lx = x; ly = y;
    }
  }
  // airtime gauge
  fb.panelBox(162, 18, 156, 104, col::panel, col::line);
  fb.text(166, 21, "TX AIRTIME 1h", col::muted);
  const float pct = airtimePct();
  const float frac = pct / 10.0f > 1 ? 1 : pct / 10.0f;   // full scale = 10 %
  const int cx = 240, cy = 106;
  for (int a = 0; a <= 180; a += 2) {
    const float rad = (180 - a) * 3.14159f / 180.f;
    const bool on = a <= frac * 180;
    for (int r = 40; r < 48; r += 2)
      fb.fill(cx + (int)(r * cosf(rad)), cy - (int)(r * sinf(rad)), 2, 2, on ? (pct > 8 ? col::orange : col::cyan) : col::line);
  }
  char b[24]; snprintf(b, sizeof b, "%.1f%%", pct);
  fb.textCenter(cx, 86, b, col::text, 2);
  fb.textCenter(cx, 110, "of 10% budget", col::sub);
  // packets per minute
  fb.panelBox(2, 126, 156, 104, col::panel, col::line);
  fb.text(6, 129, "PKTS/MIN", col::muted);
  uint16_t mx = 4;
  for (int i = 0; i < kMinuteBins; ++i) if (S.min_rx[i] + S.min_tx[i] > mx) mx = S.min_rx[i] + S.min_tx[i];
  for (int i = 0; i < kMinuteBins; ++i) {
    const int bi = kMinuteBins - 1 - i;                     // oldest on the left
    const int x = 8 + i * 5;
    const int hr = S.min_rx[bi] * 78 / mx, ht = S.min_tx[bi] * 78 / mx;
    fb.fill(x, 220 - hr, 4, hr, col::cyan);
    fb.fill(x, 220 - hr - ht, 4, ht, col::orange);
  }
  snprintf(b, sizeof b, "rx%lu tx%lu", (unsigned long)S.rx_total, (unsigned long)S.tx_total);
  fb.textRight(154, 129, b, col::sub);
  // services seen
  fb.panelBox(162, 126, 156, 104, col::panel, col::line);
  fb.text(166, 129, "WHO'S OUT THERE", col::muted);
  uint32_t total = 1;
  for (int i = 0; i < 8; ++i) total += S.svc_count[i];
  const struct { int idx; const char* l; uint16_t c; } rows[5] = {
    {0, "people LXMF", col::orange}, {2, "nomadnet", col::cyan}, {1, "propagation", col::violet},
    {3, "calls", col::green}, {7, "other", col::sub}};
  for (int i = 0; i < 5; ++i) {
    const int y = 142 + i * 15;
    const uint32_t v = S.svc_count[rows[i].idx];
    fb.text(166, y, rows[i].l, col::sub);
    fb.fill(254, y, (int)(60 * v / total) + 1, 7, rows[i].c);
    char n[8]; snprintf(n, sizeof n, "%lu", (unsigned long)v);
    fb.textRight(314, y, n, col::text);
  }
  snprintf(b, sizeof b, "path req %lu", (unsigned long)S.pathreq_count);
  fb.text(166, 218, b, col::yellow);
}

uint16_t heat(int16_t rssi, int16_t noise) {
  if (rssi <= -200) return rgb(0x0B1A2E);
  const int d = rssi - noise;
  if (d < 4) return rgb(0x123257);
  if (d < 10) return rgb(0x155E8C);
  if (d < 20) return rgb(0x1F8FB8);
  if (d < 30) return rgb(0x3DD6F5);
  if (d < 40) return rgb(0x9DEBFA);
  return rgb(0xFFE08A);
}

void pageChannel(Fb& fb, uint32_t now) {
  (void)now;
  char b[40];
  snprintf(b, sizeof b, "%.3f MHz  SF%u  %luk", S.radio.freq_hz / 1e6, S.radio.sf, (unsigned long)(S.radio.bw_hz / 1000));
  fb.text(4, 19, b, col::cyan);
  snprintf(b, sizeof b, "noise %d", S.noise);
  fb.textRight(316, 19, b, col::sub);
  fb.text(4, 32, "ACTIVITY (1 row = 1 min, newest top)", col::muted);
  for (int r = 0; r < kChanRows; ++r)
    for (int c = 0; c < 60; ++c) {
      // newest row: cells after "now" haven't happened yet
      const int curcol = (int)(S.chan_sec % 60);
      int16_t v = S.chan[r][c];
      if (r == 0 && c > curcol) v = -200;
      const int cx = 10 + c * 5, cy = 44 + r * 22;
      fb.fill(cx, cy, 5, 21, heat(v, S.noise));
      const uint8_t fl = (r == 0 && c > curcol) ? 0 : S.chan_flags[r][c];
      if (fl & 1)                                  // we transmitted: checkerboard
        for (int yy = 0; yy < 21; yy += 2) for (int xx = (yy / 2) & 1; xx < 5; xx += 2) fb.set(cx + xx, cy + yy, col::orange);
      else if (fl & 2)                             // interference: dots
        for (int yy = 1; yy < 21; yy += 4) fb.set(cx + 2, cy + yy, col::red);
    }
  fb.fill(10 + (int)(S.chan_sec % 60) * 5, 42, 5, 2, col::white);
  fb.text(10, 156, ":00", col::muted); fb.textCenter(160, 156, ":30", col::muted); fb.textRight(310, 156, ":59", col::muted);
  // legend
  const uint16_t lg[6] = {rgb(0x0B1A2E), rgb(0x155E8C), rgb(0x1F8FB8), rgb(0x3DD6F5), rgb(0x9DEBFA), rgb(0xFFE08A)};
  for (int i = 0; i < 6; ++i) fb.fill(10 + i * 10, 172, 10, 6, lg[i]);
  fb.text(76, 171, "quiet->strong", col::sub);
  for (int yy = 0; yy < 7; yy += 2) for (int xx = (yy / 2) & 1; xx < 6; xx += 2) fb.set(184 + xx, 171 + yy, col::orange);
  fb.text(194, 171, "TX", col::sub);
  for (int yy = 0; yy < 7; yy += 3) fb.set(230, 171 + yy, col::red);
  fb.text(236, 171, "interf.", col::sub);
  // interference / busy
  fb.panelBox(4, 188, 312, 36, col::panel, S.interference ? col::orange : col::line);
  if (S.interference) fb.text(10, 193, "INTERFERENCE NOW", col::orange);
  else fb.text(10, 193, "channel clear", col::green);
  snprintf(b, sizeof b, "events %lu   relayed pkts %lu", (unsigned long)S.intf_events, (unsigned long)S.relayed_count);
  fb.text(10, 208, b, col::sub);
}

void pageArea(Fb& fb, uint32_t now) {
  (void)now;
  char b[48];
  fb.panelBox(4, 18, 312, 58, col::panel, col::line);
  fb.text(10, 22, S.cities ? "YOU ARE IN" : "NO cities.bin ON SD", col::muted);
  if (S.area[0]) { snprintf(b, sizeof b, "%s", S.area); fitText(b, 20); fb.text(10, 34, b, col::yellow, 2); }
  else fb.text(10, 38, S.gps_fix ? "outside any listed place" : "waiting for GPS fix", col::sub);
  if (S.gps_fix) {
    char g[8]; maidenhead(S.lat, S.lon, g);
    snprintf(b, sizeof b, "%.4f %.4f  %s  %usat", S.lat / 1e6, S.lon / 1e6, g, S.sats);
    fb.text(10, 62, b, col::sub);
  }
  fb.text(4, 82, "PLACES THIS TRIP", col::muted);
  fb.textRight(316, 82, "HEARD", col::muted);
  fb.hline(0, 92, W, col::line);
  for (int i = 0; i < S.visit_n && i < 7; ++i) {
    const AreaVisit& v = S.visits[S.visit_n - 1 - i];
    const int y = 96 + i * 16;
    if (i == 0) fb.fill(0, y - 2, W, 15, col::panel);
    char t[8] = "--:--";
    if (v.enter_unix) { long tt = (long)v.enter_unix + S.tz_min * 60L; snprintf(t, sizeof t, "%02ld:%02ld", (tt / 3600) % 24, (tt / 60) % 60); }
    fb.text(4, y + 2, t, col::sub);
    snprintf(b, sizeof b, "%s", v.name); fitText(b, 24);
    fb.text(46, y + 2, b, i == 0 ? col::yellow : col::text);
    snprintf(b, sizeof b, "%u", v.heard);
    fb.textRight(316, y + 2, b, col::cyan);
    fb.hline(0, y + 13, W, col::line);
  }
  fb.text(4, 220, "position stays on this device", col::muted);
}

const char* ifKindLabel(uint8_t k) {
  switch (k) {
    case IF_LORA: return "LoRa"; case IF_KISS_VU: return "VHF/UHF KISS"; case IF_KISS_HF: return "HF KISS";
    case IF_TCP: return "TCP/IP"; case IF_AUTO: return "Wi-Fi/LAN"; default: return "other";
  }
}
uint16_t ifKindColor(uint8_t k) {
  switch (k) {
    case IF_LORA: return col::cyan; case IF_KISS_VU: return col::orange; case IF_KISS_HF: return col::yellow;
    case IF_TCP: return col::violet; case IF_AUTO: return col::green; default: return col::sub;
  }
}

void pageIfaces(Fb& fb, uint32_t now) {
  (void)now;
  char b[48];
  if (!S.have_host_stats) {
    fb.panelBox(10, 60, 300, 70, col::panel, col::line);
    fb.text(20, 70, S.host ? "HOST CONNECTED" : "WAITING FOR HOST", S.host ? col::green : col::orange);
    fb.text(20, 88, "install the colorui plugin on", col::sub);
    fb.text(20, 100, "the host to list every radio", col::sub);
    fb.text(20, 112, "(LoRa, VHF/UHF, HF, TCP...)", col::sub);
    return;
  }
  for (int i = 0; i < S.if_n; ++i) {
    const Iface& f = S.ifs[i];
    const int y = 18 + i * 32;
    fb.panelBox(4, y, 312, 30, col::panel, col::line);
    fb.fill(5, y + 1, 3, 28, ifKindColor(f.kind));
    fb.fill(14, y + 6, 6, 6, f.up ? col::green : col::red);
    snprintf(b, sizeof b, "%s", f.name); fitText(b, 20);
    fb.text(26, y + 5, b, col::text);
    fb.textRight(312, y + 5, ifKindLabel(f.kind), ifKindColor(f.kind));
    char br[16], rx[12], tx[12];
    if (f.bitrate >= 1000000) snprintf(br, sizeof br, "%luM", (unsigned long)(f.bitrate / 1000000));
    else if (f.bitrate >= 1000) snprintf(br, sizeof br, "%.1fk", f.bitrate / 1000.0);
    else snprintf(br, sizeof br, "%lu", (unsigned long)f.bitrate);
    fmtBytes(f.rxb, rx, sizeof rx); fmtBytes(f.txb, tx, sizeof tx);
    snprintf(b, sizeof b, "%sbps  rx %s  tx %s", br, rx, tx);
    fb.text(26, y + 18, b, col::sub);
  }
  const int y = 18 + S.if_n * 32 + 4;
  snprintf(b, sizeof b, "paths %u  known %u  lxmf queue %u", S.paths, S.dests, S.lxmf_q);
  if (y < 224) fb.text(6, y, b, col::sub);
}

void tile(Fb& fb, int x, int y, const char* k, const char* v, const char* u) {
  fb.panelBox(x, y, 102, 30, col::panel, col::line);
  fb.text(x + 4, y + 4, k, col::muted);
  int e = fb.text(x + 4, y + 16, v, col::text);
  fb.text(e + 3, y + 16, u, col::sub);
}

void pageStatus(Fb& fb, uint32_t now) {
  char a[24], b[48];
  fb.panelBox(4, 18, 312, 16, S.host ? rgb(0x12261C) : col::panel, S.host ? col::greenbg : col::line);
  fb.fill(10, 23, 6, 6, S.host ? col::green : col::orange);
  fb.text(22, 22, S.host ? "host connected" : "waiting for host", col::text);
  snprintf(a, sizeof a, "%.3f", S.radio.freq_hz / 1e6); tile(fb, 4, 38, "FREQ", a, "MHz");
  snprintf(a, sizeof a, "%lu", (unsigned long)(S.radio.bw_hz / 1000)); tile(fb, 109, 38, "BANDWIDTH", a, "kHz");
  snprintf(a, sizeof a, "%u", S.radio.sf); snprintf(b, sizeof b, "4/%u", S.radio.cr); tile(fb, 214, 38, "SF / CR", a, b);
  snprintf(a, sizeof a, "%d", S.radio.txp); tile(fb, 4, 72, "TX POWER", a, "dBm");
  snprintf(a, sizeof a, "%.1f%%", airtimePct()); tile(fb, 109, 72, "AIRTIME", a, "");
  if (S.battery >= 0) snprintf(a, sizeof a, "%d", S.battery); else snprintf(a, sizeof a, "USB");
  tile(fb, 214, 72, "BATTERY", a, S.battery >= 0 ? "%" : "");
  const uint32_t up = (now - S.boot_ms) / 1000;
  struct { const char* k; char v[40]; } rows[7];
  rows[0].k = "Board";    snprintf(rows[0].v, 40, "%s", S.board);
  rows[1].k = "Firmware"; snprintf(rows[1].v, 40, "%s", S.fw);
  rows[2].k = "Noise floor"; snprintf(rows[2].v, 40, "%d dBm", S.noise);
  rows[3].k = "Packets"; snprintf(rows[3].v, 40, "rx %lu / tx %lu", (unsigned long)S.rx_total, (unsigned long)S.tx_total);
  rows[4].k = "GPS";      if (S.gps_fix) snprintf(rows[4].v, 40, "fix, %u sats", S.sats); else snprintf(rows[4].v, 40, "no fix");
  rows[5].k = "SD card";  snprintf(rows[5].v, 40, "%s%s", S.sd ? "ok" : "none", S.cities ? ", cities.bin" : "");
  rows[6].k = "Uptime";   snprintf(rows[6].v, 40, "%lud %02lu:%02lu", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24), (unsigned long)(up / 60 % 60));
  for (int i = 0; i < 7; ++i) {
    const int y = 108 + i * 17;
    fb.text(8, y, rows[i].k, col::sub);
    fb.textRight(312, y, rows[i].v, col::text);
    fb.hline(4, y + 12, 312, col::line);
  }
}

// ---- notifications ---------------------------------------------------------
// Palette art at 2x with transparency (index 0) and clipping.
void blit2x(Fb& fb, int x0, int y0, const uint8_t* pix, const uint16_t* pal, int w, int h) {
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint8_t i = pix[y * w + x];
      if (!i) continue;
      const uint16_t c = pal[i];
      fb.set(x0 + 2 * x, y0 + 2 * y, c); fb.set(x0 + 2 * x + 1, y0 + 2 * y, c);
      fb.set(x0 + 2 * x, y0 + 2 * y + 1, c); fb.set(x0 + 2 * x + 1, y0 + 2 * y + 1, c);
    }
}

void upcase(char* d, const char* s, size_t n) {
  size_t i = 0;
  for (; s[i] && i + 1 < n; ++i) d[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
  d[i] = 0;
}

// eased slide: 0 -> 256 over 220 ms after the note appears
int slideIn(uint32_t now) {
  const uint32_t t = now - S.ncur_start;
  if (t >= 220) return 256;
  const int k = (int)(t * 256 / 220);
  return 256 - ((256 - k) * (256 - k) >> 8);
}

void noteCard(Fb& fb, uint32_t now) {
  const NoteItem& n = S.ncur;
  const int w = 252, h = 140, x = (W - w) / 2;
  const int y = 30 - (h + 40) * (256 - slideIn(now)) / 256;
  fb.shade(0, 0, W, H, 150);                                  // dim the page behind
  fb.panelBox(x, y, w, h, col::panel, col::yellow);
  fb.rect(x + 2, y + 2, w - 4, h - 4, col::line);
  const bool area = n.kind == N_AREA;
  blit2x(fb, x + 6, y + 6, area ? kPopWelcome : kPopMessage, area ? kPopWelcomePal : kPopMessagePal, kPopCardW, kPopCardH);
  char up[28];
  if (area) {
    upcase(up, S.privacy ? "a new area" : n.text, sizeof up);
    // city-limits sign: "ENTERING" + the name, painted onto the green sign
    const int sx = x + 6 + 2 * 58, sw = 2 * 56, sy = y + 6 + 2 * 8;
    fb.textCenter(sx + sw / 2, sy + 6, "ENTERING", col::white, 1, 6);
    const int adv = Fb::textWidth(up, 1, 7) <= sw - 8 ? 7 : 6;
    char fit[28]; snprintf(fit, sizeof fit, "%s", up);
    while (Fb::textWidth(fit, 1, adv) > sw - 6 && strlen(fit) > 3) fit[strlen(fit) - 1] = 0;
    fb.textCenter(sx + sw / 2, sy + 22, fit, col::white, 1, adv);
    char line[40]; snprintf(line, sizeof line, "WELCOME TO %s", up);
    const int ladv = Fb::textWidth(line, 1, 8) <= w - 16 ? 8 : 7;
    fb.textCenter(W / 2, y + 108, line, col::yellow, 1, ladv);
    char sub[40];
    if (S.visit_n > 1) snprintf(sub, sizeof sub, "Area #%d - %u nodes last area", S.visit_id, (unsigned)S.visits[S.visit_n - 2].heard);
    else snprintf(sub, sizeof sub, "First area this trip");
    if (Fb::textWidth(sub) > w - 16) snprintf(sub, sizeof sub, "Area #%d this trip", S.visit_id);
    fb.textCenter(W / 2, y + 122, sub, col::sub);
  } else {
    char line[40];
    if (n.count > 1) snprintf(line, sizeof line, "%u MESSAGES FOR YOU", (unsigned)n.count);
    else snprintf(line, sizeof line, "MESSAGE FOR YOU");
    fb.textCenter(W / 2, y + 108, line, col::yellow, 1, 8);
    fb.textCenter(W / 2, y + 122, n.text[0] ? n.text : "Encrypted: open your app", col::sub);
  }
}

void noteToast(Fb& fb, uint32_t now) {
  const NoteItem& n = S.ncur;
  const int w = 212, h = 48;
  const int x = W - w - 6 + (w + 12) * (256 - slideIn(now)) / 256, y = 26;
  fb.panelBox(x, y, w, h, col::panel, col::cyan);
  const uint8_t* pix = kPopNode; const uint16_t* pal = kPopNodePal;
  const char* title = "NEW NODE";
  char tbuf[28];
  char body[40] = {0};
  uint16_t accent = col::cyan;
  switch (n.kind) {
    case N_NODE:
      if (n.count > 1) { snprintf(tbuf, sizeof tbuf, "%u NEW NODES", (unsigned)n.count); title = tbuf; }
      snprintf(body, sizeof body, "%s", S.privacy ? "########" : n.text);
      break;
    case N_HOST_UP:   pix = kPopHostUp; pal = kPopHostUpPal; title = "HOST LINKED"; snprintf(body, sizeof body, "Reticulum online"); accent = col::green; break;
    case N_HOST_LOST: pix = kPopHostLost; pal = kPopHostLostPal; title = "HOST LOST"; snprintf(body, sizeof body, "Computer went quiet"); accent = col::red; break;
    case N_GPS_FIX:   pix = kPopGpsFix; pal = kPopGpsFixPal; title = "GPS FIX"; snprintf(body, sizeof body, "%s", n.text); accent = col::green; break;
    case N_GPS_LOST:  pix = kPopGpsLost; pal = kPopGpsLostPal; title = "GPS LOST"; snprintf(body, sizeof body, "City locator paused"); accent = col::orange; break;
    case N_INTF:      pix = kPopIntf; pal = kPopIntfPal; title = "INTERFERENCE"; snprintf(body, sizeof body, "Noise spike on channel"); accent = col::yellow; break;
    case N_BATT:      pix = kPopBatt; pal = kPopBattPal; title = "BATTERY LOW"; snprintf(body, sizeof body, "%s", n.text); accent = col::red; break;
    default: break;
  }
  fb.rect(x, y, w, h, accent);
  blit2x(fb, x + 4, y + 4, pix, pal, kPopIconW, kPopIconH);
  fb.text(x + 50, y + 10, title, accent, 1, 7);
  while (Fb::textWidth(body) > w - 56 && strlen(body) > 3) body[strlen(body) - 1] = 0;
  fb.text(x + 50, y + 26, body, col::text, 1, 7);
}

void drawNote(Fb& fb, uint32_t now) {
  if (!S.ncur.kind || now >= S.ncur_until) return;
  if (S.ncur.kind == N_AREA || S.ncur.kind == N_MSG) noteCard(fb, now);
  else noteToast(fb, now);
}

}  // namespace

void render(Fb& fb, uint32_t now) {
  S.now_ms = now;
  fb.clear(col::bg);
  switch (S.page) {
    case PG_HEARD:   pageHeard(fb, now); break;
    case PG_LIST:    pageList(fb, now); break;
    case PG_GRAPHS:  pageGraphs(fb, now); break;
    case PG_CHANNEL: pageChannel(fb, now); break;
    case PG_AREA:    pageArea(fb, now); break;
    case PG_IFACES:  pageIfaces(fb, now); break;
    default:         pageStatus(fb, now); break;
  }
  header(fb, now);
  pageDots(fb);
  drawNote(fb, now);
}

}  // namespace cui
