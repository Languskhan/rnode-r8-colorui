// PC simulator: runs the real ColorUI core over a recorded trace of real
// Reticulum packets and writes screenshots + animation frames (PPM).
//
//   sim <trace.txt> <places.bin> <outdir>
//
// Script (times in seconds of trace time):
//   * "shots/": one picture of every page late in the run
//   * "frames/": 5 fps animation of the last ~75 s, auto-flipping pages
#include "cui.h"
#include "cui_state.h"   // sim peeks at the popup timer for one screenshot
#include "cui_rns.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

using namespace cui;

static uint16_t fbuf[W * H];

static void savePPM(const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); exit(1); }
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; ++i) {
    const uint16_t p = fbuf[i];
    uint8_t rgbv[3] = {(uint8_t)(((p >> 11) & 31) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
    fwrite(rgbv, 1, 3, f);
  }
  fclose(f);
}

static std::vector<uint8_t> unhex(const char* s) {
  std::vector<uint8_t> v;
  while (s[0] && s[1] && s[0] != '\n') { unsigned b; sscanf(s, "%2x", &b); v.push_back((uint8_t)b); s += 2; }
  return v;
}

struct Ev { uint32_t t; std::string line; };

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: sim trace places outdir\n"); return 1; }
  std::string out = argv[3];
  FILE* pf = fopen(argv[2], "rb");
  static std::vector<uint8_t> places;
  if (pf) { fseek(pf, 0, SEEK_END); places.resize(ftell(pf)); fseek(pf, 0, SEEK_SET);
            if (fread(places.data(), 1, places.size(), pf) != places.size()) return 1; fclose(pf); }

  begin("Heltec V4-R8 (sim)", "RNode 1.86 + ColorUI 0.7");
  const char* theme = getenv("THEME");
  if (theme) setThemeName(theme);
  setLink(LINK_WIFI, "10.0.0.5");
  static const char kNames[] =
    "# names.txt example\n"
    "a1b2c3d4e5f60718  Hilltop Repeater\n"
    "<0056ee98b2d9348e>  !KJ6 Mobile (Dad)\n";
  loadNames(kNames, sizeof kNames - 1);
  RadioParams rp; rp.freq_hz = 914875000; rp.bw_hz = 125000; rp.sf = 9; rp.cr = 5; rp.txp = 22;
  setRadio(rp);
  onBattery(87);
  onSdCard(true);
  if (!places.empty()) loadCities(places.data(), places.size());

  std::vector<Ev> evs;
  FILE* tf = fopen(argv[1], "r");
  char buf[4096];
  while (fgets(buf, sizeof buf, tf)) {
    if (buf[0] == '#') continue;
    Ev e; e.t = (uint32_t)strtoul(buf, nullptr, 10);
    e.line = strchr(buf, ' ') + 1;
    evs.push_back(e);
  }
  fclose(tf);

  const uint32_t T0 = 1000;              // device "millis" at trace time 0
  const uint32_t end = evs.back().t + 2000;
  size_t ei = 0;
  Fb fb(fbuf);
  int frame = 0;
  bool shotHeard = false, shotPopup = false;
  uint32_t lastRx = 0;

  for (uint32_t t = 0; t <= end; t += 100) {
    const uint32_t now = T0 + t;
    while (ei < evs.size() && evs[ei].t <= t) {
      const char* l = evs[ei].line.c_str();
      const uint32_t en = T0 + evs[ei].t;
      if (l[0] == 'P') {
        int tx, rssi; float snr; char hex[2048];
        sscanf(l + 2, "%d %d %f %2047s", &tx, &rssi, &snr, hex);
        auto d = unhex(hex);
        onPacket(d.data(), d.size(), (int16_t)rssi, snr, tx, en);
        if (!tx) lastRx = t;
        // Our own first announce: remember it as "my" LXMF address, and 2 min
        // later hear it come back through a relay (the "you were heard" path).
        static std::vector<uint8_t> mine; static uint32_t echo_at = 0;
        RnsInfo ri;
        if (tx && mine.empty() && parseRns(d.data(), d.size(), ri) && ri.kind == K_ANNOUNCE && ri.service == 0) {
          mine = d; setMyDestinations(ri.dest, 1); echo_at = t + 120000;
        }
        if (echo_at && t >= echo_at && !mine.empty()) {
          std::vector<uint8_t> e;
          e.push_back((uint8_t)(mine[0] | 0x40 | 0x10)); e.push_back(2);
          const uint8_t relay[16] = {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18, 9, 9, 9, 9, 9, 9, 9, 9};
          e.insert(e.end(), relay, relay + 16);
          e.insert(e.end(), mine.begin() + 2, mine.end());
          onPacket(e.data(), e.size(), -101, 2.5f, false, en + 1);
          echo_at = 0;
        }
      } else if (l[0] == 'C') {
        int n, live, intf; sscanf(l + 2, "%d %d %d", &n, &live, &intf);
        onChannel((int16_t)n, (int16_t)live, intf, en);
      } else if (l[0] == 'G') {
        long la, lo; unsigned sats; unsigned long ux;
        sscanf(l + 2, "%ld %ld %u %lu", &la, &lo, &sats, &ux);
        onGps((int32_t)la, (int32_t)lo, (uint8_t)sats, (uint32_t)ux, en);
      } else if (l[0] == 'H') {
        auto d = unhex(l + 2);
        onHostFrame(d.data(), d.size(), en);
      }
      ++ei;
    }
    tick(now);

    // --- screenshots ---
    if (!shotPopup && cui::S.ncur.kind == N_AREA && now > cui::S.ncur_start + 400 && strstr(cui::S.ncur.text, "Folsom")) {
      const int keep = page(); setPage(PG_HEARD); render(fb, now);
      savePPM(out + "/shots/0-popup-new-area.ppm"); setPage(keep); shotPopup = true;
    }
    if (!shotHeard && t >= 1740000 && lastRx && t >= lastRx + 300 && t < lastRx + 400) {   // rings mid-flight
      setPage(PG_HEARD); render(fb, now); savePPM(out + "/shots/1-home.ppm"); shotHeard = true;
    }
    if (t == 1780000) {
      const char* names[] = {"", "2-heard", "3-signal", "4-channel", "", "6-radios", "7-status"};
      for (int p = 1; p < PG_COUNT; ++p) {
        if (p == PG_AREA) continue;
        setPage(p); render(fb, now); savePPM(out + "/shots/" + names[p] + ".ppm");
      }
      setPage(PG_HEARD);
    }
    if (t == end - 1000) { setPage(PG_AREA); render(fb, now); savePPM(out + "/shots/5-area.ppm"); }

    // --- animation: last ~75 s at 5 fps, flipping pages every 5 s after the popup ---
    if (t >= end - 77000 && t % 200 == 0) {
      const uint32_t rel = t - (end - 77000);
      int pg = PG_HEARD;
      if (rel > 40000) pg = ((rel - 40000) / 5000 + 1) % PG_COUNT;
      setPage(pg);
      render(fb, now);
      char fn[64]; snprintf(fn, sizeof fn, "/frames/%04d.ppm", frame++);
      savePPM(out + fn);
    }
  }
  // --- one picture of every notification over the live Heard page ---
  {
    struct { Note k; const char* t; int extra; const char* file; } shots[] = {
      {N_AREA, "Folsom", 0, "n1-welcome"},       {N_MSG, "", 0, "n2-message"},
      {N_MSG, "from Alice (via host)", 2, "n3-message-x3"}, {N_NODE, "Field Cardputer", 0, "n4-new-node"},
      {N_NODE, "Mesa Relay", 2, "n5-new-nodes"}, {N_HOST_UP, "", 0, "n6-host-up"},
      {N_HOST_LOST, "", 0, "n7-host-lost"},      {N_GPS_FIX, "9 satellites", 0, "n8-gps-fix"},
      {N_GPS_LOST, "", 0, "n9-gps-lost"},        {N_INTF, "", 0, "na-interference"},
      {N_BATT, "12% left", 0, "nb-battery"},
    };
    uint32_t now = T0 + end + 100000;
    setPage(PG_HEARD);
    for (auto& sh : shots) {
      now += 200000;                                  // clear cool-downs between shots
      cui::S.nq_n = 0; cui::S.ncur.kind = 0;
      notify(sh.k, sh.t, now);
      for (int i = 0; i < sh.extra; ++i) notify(sh.k, sh.t, now + 50);
      tick(now);
      render(fb, now + 600);
      savePPM(out + "/shots/" + sh.file + ".ppm");
    }
    // the slide-in, mid-animation, for one card and one toast
    now += 200000; cui::S.ncur.kind = 0; notify(N_AREA, "Orangevale", now); tick(now); render(fb, now + 90);
    savePPM(out + "/shots/n0-welcome-sliding.ppm");
  }
  {   // new notifications + screensaver
    uint32_t now = T0 + end + 4000000;
    cui::S.nq_n = 0; cui::S.ncur.kind = 0;
    notify(N_ECHO, "Hilltop Repeater, 2 hops", now); tick(now); render(fb, now + 600);
    savePPM(out + "/shots/nc-echo.ppm");
    now += 900000; cui::S.ncur.kind = 0; cui::S.nq_n = 0;
    notify(N_AIRTIME, "8.4% of the 10% budget", now); tick(now); render(fb, now + 600);
    savePPM(out + "/shots/nd-airtime.ppm");
    now += 900000; cui::S.ncur.kind = 0; cui::S.nq_n = 0; cui::S.msg_unread = 2;
    setIdle(true); render(fb, now); savePPM(out + "/shots/8-screensaver.ppm"); setIdle(false);
  }
  {   // 0.8 views
    uint32_t now = T0 + end + 9000000;
    cui::S.nq_n = 0; cui::S.ncur.kind = 0; cui::S.msg_unread = 0;
    tick(now);
    // a sky full of satellites
    SatInfo sv[12] = {{2,72,40,42,'G'},{5,48,120,38,'G'},{12,30,200,31,'G'},{15,15,300,22,'G'},{18,60,260,40,'G'},
                      {24,22,80,27,'G'},{29,8,160,15,'G'},{65,55,330,36,'R'},{71,35,20,29,'R'},{3,40,230,33,'E'},
                      {11,12,110,0,'E'},{20,66,150,41,'B'}};
    onSats(sv, 12, 9, 0.9f);
    // a few days of history
    {
      const uint32_t u = cui::unixNow();
      for (int k = 0; k < 24; ++k) { HourBin& h = cui::S.hours[(u / 3600 - k) % 24]; h.hour = u / 3600 - k;
        h.pkts = (uint16_t)(20 + 60 * (1 + sin(k / 3.0)) + (k % 5) * 7); h.nodes = (uint16_t)(3 + k % 7);
        h.noise_sum = (-110 + (k % 6)) * 100; h.noise_n = 100; }
      for (int k = 0; k < 7; ++k) { DayBin& d = cui::S.days[(u / 86400 - k) % 7]; d.day = u / 86400 - k; d.nodes = (uint16_t)(6 + (k * 5) % 11); d.pkts = 900 + k * 40; }
    }
    struct { int page, tab; const char* f; } t8[] = {
      {PG_LIST, 0, "a1-heard-packets"}, {PG_LIST, 1, "a2-heard-nodes"}, {PG_GRAPHS, 1, "a3-signal-history"},
      {PG_AREA, 1, "a4-area-sky"}, {PG_IFACES, 1, "a5-radios-network"}, {PG_STATUS, 0, "a6-status-announce"}};
    onHostLink(true, now);
    for (auto& x : t8) { setPage(x.page); cui::S.tab[x.page] = (uint8_t)x.tab; render(fb, now); savePPM(out + "/shots/" + x.f + ".ppm"); }
    // tap a station on the Nodes tab -> details; then Track; packet inspector; quick panel
    setPage(PG_LIST); cui::S.tab[PG_LIST] = 1;
    onTap(150, 72 + 33 + 10); render(fb, now); savePPM(out + "/shots/a7-station.ppm");
    onTap(80, 26 + 210 - 20); render(fb, now); savePPM(out + "/shots/a8-tracker.ppm");
    cui::S.ov = 0; cui::S.tab[PG_LIST] = 0;
    onTap(150, 54 + 10); render(fb, now); savePPM(out + "/shots/a9-packet.ppm");
    cui::S.ov = 0; setPage(PG_HEARD);
    openQuick(); render(fb, now + 400); savePPM(out + "/shots/b1-quick.ppm");
    cui::S.ov = 0;
    cui::S.ncur.kind = 0; cui::S.nq_n = 0; notify(N_WATCH, "KJ6 Mobile (Dad)", now + 1000); tick(now + 1000); render(fb, now + 1600);
    savePPM(out + "/shots/b2-watch.ppm");
  }
  fprintf(stderr, "frames=%d\n", frame);
  return 0;
}
