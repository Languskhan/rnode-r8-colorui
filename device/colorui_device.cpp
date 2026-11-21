// ColorUI device glue for the Heltec WiFi LoRa 32 V4-R8 + Expansion Kit V2
// (2.8" ST7789 240x320, CHSC6x touch, micro-SD, L76K GNSS, buzzer).
//
// STATUS: written against pin maps from the WADAMESH V4-R8 build
// (variants/heltec_v4, LGFXDisplay.cpp, HeltecV4CapTouch.cpp) and the
// community RNode fork's documented R8 pins (deuxyeux/RNode_Firmware,
// Documentation/HELTEC_V4_R8.md), on upstream RNode 1.86. Not yet compiled
// or run on hardware -- expect a first bring-up pass.
//
// R8 rule: GPIO33-37 belong to the octal PSRAM. Nothing here may touch them.
//
// Threads: RNode's loop() runs on core 1. Everything here runs in one
// FreeRTOS task pinned to core 0. The two sides only share a queue.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <chsc6x.h>        // Quency-D/chsc6x, same library WADAMESH uses
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "colorui_hooks.h"
#include "../core/cui.h"
#include "colorui_demo.h"
#include <Preferences.h>

#ifndef COLORUI_VERSION
#define COLORUI_VERSION "0.7"
#endif


// ---- pins (Expansion Kit V2, from WADAMESH's hardware-probed V4-R8 env) ----
#define CUI_TFT_SCLK 16
#define CUI_TFT_MOSI 15
#define CUI_TFT_MISO 45     // shared with the micro-SD
#define CUI_TFT_DC   48
#define CUI_TFT_CS   47
#define CUI_TFT_RST  21
#define CUI_TFT_BL   44
#define CUI_SD_CS     3     // strapping pin: park HIGH before the display starts
#define CUI_VEXT_EN  40     // active LOW
#define CUI_GPS_EN   42     // active LOW
#define CUI_GPS_RXD  39     // we receive on 39 (module TX)
#define CUI_GPS_TXD  38
#define CUI_BTN       0     // PRG: only read at boot (DEMO prompt); otherwise upstream RNode owns it
#define CUI_BUZZER    4     // PAM8904 piezo driver on the Expansion Kit V2
#define CUI_TOUCH_SDA 17    // CHSC6x shares the board I2C
#define CUI_TOUCH_SCL 18
#define CUI_TOUCH_INT 43

static_assert(CUI_TFT_MISO < 33 || CUI_TFT_MISO > 37, "GPIO33-37 are PSRAM on the R8");
static_assert(CUI_BUZZER < 33 || CUI_BUZZER > 37, "GPIO33-37 are PSRAM on the R8");

class CuiLcd : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
 public:
  CuiLcd() {
    auto b = bus_.config();
    // SPI3: the SX1262 radio owns SPI2/FSPI (Arduino's global SPI, pins 9/10/11) on the
    // other core. Sharing SPI2 hung pushImage and tripped the task watchdog (v0.4).
    b.spi_host = SPI3_HOST; b.spi_mode = 0;
    b.freq_write = 40000000; b.freq_read = 16000000;
    b.spi_3wire = false; b.use_lock = true; b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = CUI_TFT_SCLK; b.pin_mosi = CUI_TFT_MOSI; b.pin_miso = CUI_TFT_MISO; b.pin_dc = CUI_TFT_DC;
    bus_.config(b); panel_.setBus(&bus_);
    auto p = panel_.config();
    p.pin_cs = CUI_TFT_CS; p.pin_rst = CUI_TFT_RST; p.pin_busy = -1;
    p.panel_width = 240; p.panel_height = 320;
    // bus_shared: the micro-SD (CS 3) sits on the same SPI3 pins and is read
    // between frames (map tiles, logs). Same approach as WADAMESH on this board.
    p.readable = false; p.invert = true; p.rgb_order = false; p.bus_shared = true;
    panel_.config(p);
    setPanel(&panel_);
  }
};

namespace {

enum EvType : uint8_t { EV_RX, EV_TX, EV_HOST, EV_HOSTSEEN, EV_CHAN, EV_RADIO, EV_BATT, EV_LINK };
struct Ev {
  EvType type;
  uint16_t len;
  int16_t rssi, aux;
  float snr;
  uint32_t t_ms;
  uint8_t data[512];
};

CuiLcd lcd;
QueueHandle_t q = nullptr;
uint16_t* fbmem = nullptr;
uint8_t* cities = nullptr;
volatile bool started = false;

void push(EvType t, const uint8_t* d, uint16_t len, int16_t rssi = 0, float snr = 0, int16_t aux = 0) {
  if (!started || !q) return;
  static Ev e;                       // only the radio core calls push(); copied into the queue
  e.type = t; e.len = len > sizeof(e.data) ? sizeof(e.data) : len;
  e.rssi = rssi; e.snr = snr; e.aux = aux; e.t_ms = millis();
  if (d && e.len) memcpy(e.data, d, e.len);
  xQueueSend(q, &e, 0);              // never block the radio: drop if the UI is behind
}

// --- tiny NMEA reader (RMC + GGA) ---
char nmea[100]; int nmea_n = 0;
int32_t g_lat = 0, g_lon = 0; uint8_t g_sats = 0; uint32_t g_unix = 0; bool g_fix = false;

int32_t nmeaDeg(const char* v, const char* hemi) {
  if (!*v) return 0;
  double raw = atof(v); int deg = (int)(raw / 100); double min = raw - deg * 100;
  double dd = deg + min / 60.0; if (*hemi == 'S' || *hemi == 'W') dd = -dd;
  return (int32_t)(dd * 1e6);
}
uint32_t civilToUnix(int y, int m, int d, int hh, int mm, int ss) {
  y -= m <= 2; const int era = y / 400; const unsigned yoe = y - era * 400;
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)((era * 146097 + (int)doe - 719468) * 86400L + hh * 3600 + mm * 60 + ss);
}
void nmeaLine(char* s) {
  char* f[20]; int n = 0; f[n++] = s;
  for (char* p = s; *p && n < 20; ++p) if (*p == ',' || *p == '*') { *p = 0; f[n++] = p + 1; }
  if (n > 9 && strstr(f[0], "RMC")) {
    g_fix = f[2][0] == 'A';
    if (g_fix) { g_lat = nmeaDeg(f[3], f[4]); g_lon = nmeaDeg(f[5], f[6]); }
    if (strlen(f[1]) >= 6 && strlen(f[9]) == 6) {
      auto two = [](const char* c) { return (c[0] - '0') * 10 + (c[1] - '0'); };
      g_unix = civilToUnix(2000 + two(f[9] + 4), two(f[9] + 2), two(f[9]), two(f[1]), two(f[1] + 2), two(f[1] + 4));
    }
  } else if (n > 7 && strstr(f[0], "GGA")) {
    g_sats = (uint8_t)atoi(f[7]);
  }
}
void pollGps() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || nmea_n >= (int)sizeof(nmea) - 1) { nmea[nmea_n] = 0; if (nmea[0] == '$') nmeaLine(nmea); nmea_n = 0; }
    else if (c != '\r') nmea[nmea_n++] = c;
  }
}

// ---- touch (CHSC6x): swipe left/right = page, long press = privacy ----
chsc6x* tp = nullptr;
void touchBegin() {
  Wire.begin(CUI_TOUCH_SDA, CUI_TOUCH_SCL, 100000);
  Wire.setTimeOut(20);
  static const uint8_t addrs[] = {0x2E, 0x15, 0x14};   // ACK probe, as in WADAMESH
  uint8_t found = 0;
  for (uint8_t a : addrs) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) { found = a; break; } }
  if (!found) return;
  static chsc6x inst(&Wire, CUI_TOUCH_SDA, CUI_TOUCH_SCL, -1, -1);
  inst._i2c_addr = found;
  tp = &inst;
  tp->chsc6x_init();
  Wire.begin(CUI_TOUCH_SDA, CUI_TOUCH_SCL, 100000);
  Wire.setTimeOut(20);
}
uint32_t g_last_touch = 0;
void pollTouch(uint32_t now) {
  static bool down = false, waking = false; static uint32_t t0 = 0; static int sx = 0, lx = 0, sy = 0; static uint8_t miss = 0;
  if (!tp) return;
  uint16_t x = 0, y = 0;
  const bool on = tp->chsc6x_read_touch_info(&x, &y) == 0;
  if (!on && down && miss < 2) { ++miss; return; }       // ignore one flaky read mid-gesture
  if (on) miss = 0;
  // panel is portrait 240x320, UI is rotation 1 (landscape): landscape x = raw y,
  // landscape y = 239 - raw x (settings.ini touch_flip = on if rail taps land wrong)
  const int ux = y;
  const int uy = cui::settings().touch_flip ? (int)x : 239 - (int)x;
  if (on) g_last_touch = now;
  if (on && !down) { down = true; t0 = now; sx = lx = ux; sy = uy; waking = cui::idle(); }
  else if (on) lx = ux;
  else if (down) {
    down = false;
    if (waking) return;                                 // first touch only wakes the screen
    const int dx = lx - sx;
    if (dx > 40) cui::prevPage(); else if (dx < -40) cui::nextPage();
    else if (now - t0 > 1000) cui::togglePrivacy();
    else if (!cui::onTap(sx, sy)) cui::nextPage();      // rail tap = that page, else next page
  }
}

void playBeep(uint8_t b) {
  if (b == cui::BEEP_NEW_AREA)    { tone(CUI_BUZZER, 2400, 70); delay(90); tone(CUI_BUZZER, 3200, 90); }
  else if (b == cui::BEEP_NEW_STATION) tone(CUI_BUZZER, 2800, 40);
  else if (b == cui::BEEP_MESSAGE) { tone(CUI_BUZZER, 2600, 50); delay(70); tone(CUI_BUZZER, 3100, 50); delay(70); tone(CUI_BUZZER, 3600, 70); }
}

// ---- DEMO mode: replays real Reticulum packets, a GPS drive and host stats ----
// Lets you check the screen, touch, buzzer and SD with no Reticulum set up.
bool demo = false;
void demoTick(uint32_t now) {
  static uint32_t next_pkt = 0, next_gps = 0, next_host = 0;
  static int pi = 0, gi = 0;
  if (now >= next_pkt) {
    const DemoPkt& p = kDemoPkts[pi];
    cui::onPacket(p.data, p.len, p.tx ? 0 : (int16_t)(p.rssi + (int)(esp_random() % 7) - 3), p.snr4 / 4.0f, p.tx, now);
    pi = (pi + 1) % kDemoPktCount;
    next_pkt = now + 2500 + esp_random() % 4000;
  }
  if (now >= next_gps) {
    static uint32_t fake_unix = 1795221000;           // 16:30 local if the GPS has no time yet
    fake_unix += 4;
    cui::onGps(kDemoRoute[gi][0], kDemoRoute[gi][1], 9, g_unix ? g_unix : fake_unix, now);
    gi = (gi + 1) % kDemoRouteLen;
    next_gps = now + 4000;
  }
  if (now >= next_host) { cui::onHostFrame(kDemoHost, sizeof kDemoHost, now); next_host = now + 10000; }
  // DEMO: a "message for you" card every ~45 s so you can see it
  static uint32_t next_msg = 0;
  static int mi = 0;
  if (!next_msg) next_msg = now + 20000;
  if (now >= next_msg) {
    static const char* const kFrom[] = {"", "from Fair Oaks Relay", "Link request"};
    cui::notify(cui::N_MSG, kFrom[mi++ % 3], now);
    next_msg = now + 45000;
  }
  cui::onChannel(-108 + (int)(esp_random() % 5) - 2, -108, false, now);
}

// ---- SD card ---------------------------------------------------------------
// The card shares the LCD's SPI3 pins (16/45/15, CS 3). It is mounted once
// before the LCD starts and stays mounted; every SD access happens in the UI
// task between frames, never while a frame is being pushed. sd_live = off in
// settings.ini goes back to the 0.6 way (read at boot, then let go).
SPIClass sd_spi(HSPI);                 // HSPI = SPI3 on the ESP32-S3
bool sd_ok = false, sd_live = false;
char g_splash_files[16][40];
int  g_splash_n = 0;
uint8_t g_map_zoom = 0;                // 0 = no tiles on the card
char g_map_ext[5] = "jpg";

bool sdReadAll(const char* path, uint8_t** out, size_t* n, size_t max) {
  File f = SD.open(path);
  if (!f) return false;
  const size_t len = f.size();
  bool ok = false;
  if (len > 0 && len <= max) {
    *out = (uint8_t*)ps_malloc(len + 1);
    if (*out && f.read(*out, len) == len) { (*out)[len] = 0; *n = len; ok = true; }
  }
  f.close();
  return ok;
}

void sdBootFiles() {
  uint8_t* buf = nullptr; size_t n = 0;
  if (sdReadAll("/cities.bin", &buf, &n, 4000000)) { cities = buf; cui::loadCities(cities, n); }
  if (sdReadAll("/settings.ini", &buf, &n, 16384)) { cui::applySettings((const char*)buf, n); free(buf); }
  if (sdReadAll("/names.txt", &buf, &n, 32768)) { cui::loadNames((const char*)buf, n); free(buf); }
  // your own splash pictures
  File d = SD.open("/splash");
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f && g_splash_n < 16; f = d.openNextFile()) {
      const char* nm = f.name();
      const size_t L = strlen(nm);
      if (!f.isDirectory() && L > 4 && L < 30 && (!strcasecmp(nm + L - 4, ".bmp")))
        snprintf(g_splash_files[g_splash_n++], 40, "/splash/%s", nm);
      f.close();
    }
    d.close();
  }
  // offline map tiles: /tiles/<z>/<x>/<y>.jpg|png (WADAMESH / OSM layout)
  const int want = 14;
  for (int dz = 0; dz <= 4 && !g_map_zoom; ++dz)
    for (int z : {want + dz, want - dz}) {
      if (z < 8 || z > 18) continue;
      char p[16]; snprintf(p, sizeof p, "/tiles/%d", z);
      if (SD.exists(p)) { g_map_zoom = (uint8_t)z; break; }
    }
}

void sdBegin() {
  pinMode(CUI_TFT_CS, OUTPUT); digitalWrite(CUI_TFT_CS, HIGH);   // LCD deselected
  sd_spi.begin(CUI_TFT_SCLK, CUI_TFT_MISO, CUI_TFT_MOSI, CUI_SD_CS);
  if (!SD.begin(CUI_SD_CS, sd_spi, 20000000)) { cui::onSdCard(false); sd_spi.end(); return; }
  sd_ok = true;
  cui::onSdCard(true);
  sdBootFiles();
  sd_live = cui::settings().sd_live;
  if (!sd_live) { SD.end(); sd_spi.end(); }
}

// Load a 320x240 BMP (24-bit, or 16-bit RGB565) into the framebuffer.
bool loadBmp(const char* path, uint16_t* fb) {
  File f = SD.open(path);
  if (!f) return false;
  uint8_t h[54];
  bool ok = false;
  if (f.read(h, 54) == 54 && h[0] == 'B' && h[1] == 'M') {
    const uint32_t off = h[10] | h[11] << 8 | h[12] << 16 | (uint32_t)h[13] << 24;
    const int32_t w = (int32_t)(h[18] | h[19] << 8 | h[20] << 16 | (uint32_t)h[21] << 24);
    int32_t hh = (int32_t)(h[22] | h[23] << 8 | h[24] << 16 | (uint32_t)h[25] << 24);
    const int bpp = h[28] | h[29] << 8;
    const bool topdown = hh < 0; if (topdown) hh = -hh;
    if (w == cui::W && hh == cui::H && (bpp == 24 || bpp == 16)) {
      const int rowb = ((w * bpp / 8) + 3) & ~3;
      static uint8_t row[cui::W * 3 + 4];
      f.seek(off);
      ok = true;
      for (int r = 0; r < hh && ok; ++r) {
        if (f.read(row, rowb) != rowb) { ok = false; break; }
        uint16_t* dst = fb + (topdown ? r : hh - 1 - r) * cui::W;
        for (int x = 0; x < w; ++x) {
          if (bpp == 24) { const uint8_t b = row[x * 3], g = row[x * 3 + 1], rr = row[x * 3 + 2];
            dst[x] = (uint16_t)(((rr & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
          else dst[x] = (uint16_t)(row[x * 2] | row[x * 2 + 1] << 8);
        }
      }
    }
  }
  f.close();
  return ok;
}

// Boot splash: the built-in scenes plus your /splash/*.bmp pictures, a
// different one every boot (the last index is kept in NVS). Tap the screen or
// press PRG within 3 s to start DEMO mode.
int nextSplashScene(int total) {
  Preferences prefs;
  int s = 0;
  if (prefs.begin("colorui", false)) {
    s = (prefs.getUChar("splash", 255) + 1) % total;
    prefs.putUChar("splash", (uint8_t)s);
    prefs.end();
  } else {
    s = esp_random() % total;
  }
  return s;
}

bool askDemo() {
  cui::Fb fb(fbmem);
  const int builtin = cui::splashCount();
  const int sc = nextSplashScene(builtin + g_splash_n);
  bool drawn = false;
  if (sc >= builtin && sd_ok) {
    if (!sd_live) { sd_spi.begin(CUI_TFT_SCLK, CUI_TFT_MISO, CUI_TFT_MOSI, CUI_SD_CS); SD.begin(CUI_SD_CS, sd_spi, 20000000); }
    drawn = loadBmp(g_splash_files[sc - builtin], fbmem);
    if (!sd_live) { SD.end(); sd_spi.end(); }
    if (drawn) cui::drawSplashStrip(fb, nullptr, "ColorUI " COLORUI_VERSION, 0);
  }
  if (!drawn) cui::drawSplash(fb, sc % builtin, "ColorUI " COLORUI_VERSION, 0);
  lcd.startWrite();
  lcd.pushImage(0, 0, cui::W, cui::H, (const lgfx::rgb565_t*)fbmem);
  lcd.endWrite();
  const uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    uint16_t x, y;
    if (digitalRead(CUI_BTN) == LOW) return true;
    if (tp && tp->chsc6x_read_touch_info(&x, &y) == 0) return true;
    // countdown bar: same spot and colours as drawSplashStrip() draws it
    lcd.fillRect(8, 230, (millis() - t0) * (cui::W - 16) / 3000, 4, cui::col::cyan);
    delay(20);
  }
  return false;
}

// ---- offline map (Area page) -------------------------------------------------
// Tiles are decoded into a PSRAM sprite, copied into a cached map image the
// size of the map card, and only rebuilt when you have moved ~40 px.
LGFX_Sprite* g_tile = nullptr;
uint16_t* g_map = nullptr;
int g_map_w = 0, g_map_h = 0;
double g_map_px = 0, g_map_py = 0;     // world-pixel center of the cached image
uint32_t g_map_built = 0;

void worldPx(int32_t lat_e6, int32_t lon_e6, int z, double* px, double* py) {
  const double n = 256.0 * (1u << z);
  const double lat = lat_e6 / 1e6 * M_PI / 180.0;
  *px = (lon_e6 / 1e6 + 180.0) / 360.0 * n;
  *py = (1.0 - log(tan(lat) + 1.0 / cos(lat)) / M_PI) / 2.0 * n;
}

bool buildMap(int w, int h, double cx, double cy) {
  if (!g_map || g_map_w != w || g_map_h != h) {
    free(g_map); g_map = (uint16_t*)ps_malloc((size_t)w * h * 2); g_map_w = w; g_map_h = h;
    if (!g_map) return false;
  }
  if (!g_tile) {
    g_tile = new LGFX_Sprite();
    g_tile->setPsram(true); g_tile->setColorDepth(16);
    if (!g_tile->createSprite(256, 256)) { delete g_tile; g_tile = nullptr; return false; }
  }
  for (int i = 0; i < w * h; ++i) g_map[i] = cui::T.card2;
  const int z = g_map_zoom;
  const double x0 = cx - w / 2.0, y0 = cy - h / 2.0;
  bool any = false;
  for (int ty = (int)floor(y0 / 256); ty <= (int)floor((y0 + h) / 256); ++ty)
    for (int tx = (int)floor(x0 / 256); tx <= (int)floor((x0 + w) / 256); ++tx) {
      char p[48];
      snprintf(p, sizeof p, "/tiles/%d/%d/%d.%s", z, tx, ty, g_map_ext);
      bool ok = false;
      if (SD.exists(p)) ok = !strcmp(g_map_ext, "png") ? g_tile->drawPngFile(SD, p, 0, 0) : g_tile->drawJpgFile(SD, p, 0, 0);
      else {
        snprintf(p, sizeof p, "/tiles/%d/%d/%d.png", z, tx, ty);
        if (SD.exists(p)) { ok = g_tile->drawPngFile(SD, p, 0, 0); if (ok) strcpy(g_map_ext, "png"); }
      }
      if (!ok) continue;
      any = true;
      const uint16_t* src = (const uint16_t*)g_tile->getBuffer();   // byte-swapped RGB565
      for (int j = 0; j < 256; ++j) {
        const int my = (int)(ty * 256.0 + j - y0);
        if (my < 0 || my >= h) continue;
        for (int i = 0; i < 256; ++i) {
          const int mx = (int)(tx * 256.0 + i - x0);
          if (mx < 0 || mx >= w) continue;
          const uint16_t v = src[j * 256 + i];
          uint16_t c = (uint16_t)((v >> 8) | (v << 8));
          // darken the map so the coverage dots and the theme stay readable
          const int r = ((c >> 11) & 31) * 150 >> 8, g = ((c >> 5) & 63) * 150 >> 8, b = (c & 31) * 150 >> 8;
          g_map[my * w + mx] = (uint16_t)((r << 11) | (g << 5) | b);
        }
      }
    }
  g_map_px = cx; g_map_py = cy;
  return any;
}

bool mapPainter(cui::Fb& fb, int x, int y, int w, int h, int32_t lat, int32_t lon) {
  if (!sd_live || !g_map_zoom) return false;
  double cx, cy; worldPx(lat, lon, g_map_zoom, &cx, &cy);
  static bool have = false;
  const bool far = !have || fabs(cx - g_map_px) > 40 || fabs(cy - g_map_py) > 25 || g_map_w != w || g_map_h != h;
  if (far && millis() - g_map_built > 3000) { have = buildMap(w, h, cx, cy); g_map_built = millis(); }
  if (!have) return false;
  const int ox = (int)(cx - g_map_px), oy = (int)(cy - g_map_py);   // shift by how far we moved
  for (int j = 0; j < h; ++j) {
    const int sy = j + oy;
    if (sy < 0 || sy >= h) continue;
    for (int i = 0; i < w; ++i) {
      const int sx = i + ox;
      if (sx < 0 || sx >= w) continue;
      fb.set(x + i, y + j, g_map[sy * w + sx]);
    }
  }
  return true;
}

// ---- logs: /logs/heard-YYYY-MM-DD.csv (every packet, with your position) ----
void flushLogs() {
  static uint32_t last_ms = 0;
  static char* buf = nullptr;
  if (!sd_live || !cui::settings().log) return;
  if (!buf) buf = (char*)ps_malloc(16384);
  if (!buf) return;
  const size_t n = cui::heardCsv(&last_ms, buf, 16384);
  if (!n) return;
  char path[40] = "/logs/heard.csv";
  const uint32_t u = g_unix;
  if (u) {
    const long t = (long)u + cui::settings().tz_min * 60L;
    long z = t / 86400 + 719468; const long era = z / 146097, doe = z - era * 146097;
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    const int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);
    const int yr = (int)(yoe + era * 400 + (m <= 2));
    snprintf(path, sizeof path, "/logs/heard-%04d-%02d-%02d.csv", yr, m, d);
  }
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  const bool fresh = !SD.exists(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) return;
  if (fresh) f.print(cui::heardCsvHeader());
  f.write((const uint8_t*)buf, n);
  f.close();
}

// ---- backlight: brightness + screensaver dimming (LEDC on GPIO44) ----
void backlight(int pct) {
  static int last = -1;
  if (pct == last) return;
  last = pct;
  ledcWrite(6, pct * 255 / 100);
}

void uiTask(void*) {
  static Ev e;
  cui::Fb fb(fbmem);
  uint32_t last_draw = 0, last_gps = 0;
  for (;;) {
    while (xQueueReceive(q, &e, 0) == pdTRUE) {
      switch (e.type) {
        case EV_RX: cui::onPacket(e.data, e.len, e.rssi, e.snr, false, e.t_ms); break;
        case EV_TX: cui::onPacket(e.data, e.len, 0, 0, true, e.t_ms); break;
        case EV_HOST: cui::onHostFrame(e.data, e.len, e.t_ms); break;
        case EV_HOSTSEEN: cui::onHostLink(true, e.t_ms); break;
        case EV_CHAN: cui::onChannel(e.rssi, e.aux, e.snr != 0, e.t_ms); break;
        case EV_RADIO: if (!demo) { cui::RadioParams p; memcpy(&p, e.data, sizeof p); cui::setRadio(p); } break;
        case EV_BATT: cui::onBattery(e.rssi); break;
        case EV_LINK: cui::setLink(e.data[0], e.len > 1 ? (const char*)e.data + 1 : nullptr); break;
      }
    }
    const uint32_t now = millis();
    pollGps();
    if (!demo && now - last_gps > 2000) {
      last_gps = now;
      if (g_fix) cui::onGps(g_lat, g_lon, g_sats, g_unix, now); else cui::onGpsLost(now);
    }
    // The PRG button stays upstream RNode's: short = Bluetooth on/off, 0.7 s = sleep,
    // 5 s = BLE pairing, 10 s = console. ColorUI pages are on the touchscreen.
    pollTouch(now);
    if (demo) demoTick(now);
    cui::tick(now);
    // screensaver + backlight
    const cui::Settings& st = cui::settings();
    const bool idle = st.idle_min > 0 && now - g_last_touch > (uint32_t)st.idle_min * 60000UL;
    cui::setIdle(idle);
    backlight(idle && !cui::wantWake() ? st.dim : st.brightness);
    static uint32_t last_log = 0;
    if (now - last_log > 30000) { last_log = now; flushLogs(); }
    if (uint8_t b = cui::takeBeep()) playBeep(b);
    if (now - last_draw >= 100) {               // 10 fps
      last_draw = now;
      cui::render(fb, now);
      // Frame lives in PSRAM, so use the plain (non-DMA) push: ~31 ms per frame at
      // 40 MHz, all on this core. Our buffer is native-order RGB565 = lgfx::rgb565_t.
      for (int band = 0; band < 4; ++band) {       // 4 strips of 60 rows, yield between
        lcd.startWrite();
        lcd.pushImage(0, band * 60, cui::W, 60, (const lgfx::rgb565_t*)(fbmem + band * 60 * cui::W));
        lcd.endWrite();
        taskYIELD();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

void colorui_begin() {
  pinMode(CUI_SD_CS, OUTPUT); digitalWrite(CUI_SD_CS, HIGH);
  pinMode(CUI_VEXT_EN, OUTPUT); digitalWrite(CUI_VEXT_EN, LOW);
  pinMode(CUI_GPS_EN, OUTPUT); digitalWrite(CUI_GPS_EN, LOW);
  pinMode(CUI_BTN, INPUT_PULLUP);
  Serial1.begin(9600, SERIAL_8N1, CUI_GPS_RXD, CUI_GPS_TXD);
  fbmem = (uint16_t*)ps_malloc(cui::W * cui::H * 2);
  q = xQueueCreate(24, sizeof(Ev));
  cui::begin("Heltec V4-R8", "RNode 1.86 + ColorUI " COLORUI_VERSION);
  cui::setTzOffsetMinutes(-480);                 // settings.ini tz, or the host's TIME frame, override this
  // 1) SD first: mount it on SPI3 (TFT held deselected) and read settings,
  //    names, cities, splash list; it stays mounted unless sd_live = off
  sdBegin();
  if (const char* b = cui::settings().brand) cui::setBrand(b);
  // 2) the LCD joins SPI3 (bus_shared)
  lcd.init();
  lcd.setRotation(1);                            // landscape 320x240
  ledcSetup(6, 20000, 8); ledcAttachPin(CUI_TFT_BL, 6);
  backlight(cui::settings().brightness);
  cui::setMapPainter(mapPainter);
  g_last_touch = millis();
  touchBegin();
  pinMode(CUI_BUZZER, OUTPUT);
  demo = askDemo();
  if (demo) {
    while (digitalRead(CUI_BTN) == LOW) delay(10);   // don't count the press as a page change
    cui::setDemo(true);
    cui::loadCities(kDemoPlaces, sizeof kDemoPlaces); // demo areas replace any cities.bin
    cui::RadioParams rp; rp.freq_hz = 914875000; rp.bw_hz = 125000; rp.sf = 9; rp.cr = 5; rp.txp = 22;
    cui::setRadio(rp);
  }
  started = true;
  xTaskCreatePinnedToCore(uiTask, "colorui", 8192, nullptr, 1, nullptr, 0);
}

void colorui_on_rx(const uint8_t* d, uint16_t len, int rssi, float snr) { push(EV_RX, d, len, (int16_t)rssi, snr); }
void colorui_on_tx(const uint8_t* d, uint16_t len) { push(EV_TX, d, len); }
void colorui_on_host_cmd(const uint8_t* d, uint16_t len) { push(EV_HOST, d, len); }
void colorui_on_host_seen() {
  static uint32_t last = 0;
  if (millis() - last > 5000) { last = millis(); push(EV_HOSTSEEN, nullptr, 0); }
}
void colorui_on_channel(int noise, int current, bool intf) {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  push(EV_CHAN, nullptr, 0, (int16_t)noise, intf ? 1.0f : 0.0f, (int16_t)current);
}
void colorui_set_radio(uint32_t freq, uint32_t bw, uint8_t sf, uint8_t cr, int8_t txp) {
  cui::RadioParams p; p.freq_hz = freq; p.bw_hz = bw; p.sf = sf; p.cr = cr; p.txp = txp;
  push(EV_RADIO, (const uint8_t*)&p, sizeof p);
}
void colorui_set_battery(int percent) {
  static int last = -2;
  if (percent == last) return;
  last = percent;
  push(EV_BATT, nullptr, 0, (int16_t)percent);
}
void colorui_set_link(uint8_t link, const char* ip) {
  static uint8_t last = 255; static char last_ip[16] = "";
  const char* p = ip ? ip : "";
  if (link == last && !strcmp(p, last_ip)) return;
  last = link; snprintf(last_ip, sizeof last_ip, "%s", p);
  uint8_t d[17]; d[0] = link; size_t n = strlen(last_ip);
  memcpy(d + 1, last_ip, n); d[1 + n] = 0;
  push(EV_LINK, d, (uint16_t)(n + 2));
}
