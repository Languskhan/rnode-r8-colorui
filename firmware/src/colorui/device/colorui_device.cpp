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
#define COLORUI_VERSION "0.6"
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
    p.readable = false; p.invert = true; p.rgb_order = false; p.bus_shared = false;  // bus is ours alone after boot
    panel_.config(p);
    setPanel(&panel_);
  }
};

namespace {

enum EvType : uint8_t { EV_RX, EV_TX, EV_HOST, EV_HOSTSEEN, EV_CHAN, EV_RADIO, EV_BATT };
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
void pollTouch(uint32_t now) {
  static bool down = false; static uint32_t t0 = 0; static int sx = 0, lx = 0; static uint8_t miss = 0;
  if (!tp) return;
  uint16_t x = 0, y = 0;
  const bool on = tp->chsc6x_read_touch_info(&x, &y) == 0;
  if (!on && down && miss < 2) { ++miss; return; }       // ignore one flaky read mid-gesture
  if (on) miss = 0;
  // panel is portrait 240x320, UI is rotation 1 (landscape): landscape x = raw y
  const int ux = y;
  if (on && !down) { down = true; t0 = now; sx = lx = ux; }
  else if (on) lx = ux;
  else if (down) {
    down = false;
    const int dx = lx - sx;
    if (dx > 40) cui::prevPage(); else if (dx < -40) cui::nextPage();
    else if (now - t0 > 1000) cui::togglePrivacy();
    else cui::nextPage();                               // plain tap = next page
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

// Boot splash: one of the cui_splash scenes, a different one every boot
// (the last index is kept in NVS). Tap the screen or press PRG within 3 s
// to start DEMO mode.
int nextSplashScene() {
  Preferences prefs;
  int s = 0;
  if (prefs.begin("colorui", false)) {
    s = (prefs.getUChar("splash", 255) + 1) % cui::splashCount();
    prefs.putUChar("splash", (uint8_t)s);
    prefs.end();
  } else {
    s = esp_random() % cui::splashCount();
  }
  return s;
}

bool askDemo() {
  cui::Fb fb(fbmem);
  cui::drawSplash(fb, nextSplashScene(), "ColorUI " COLORUI_VERSION, 0);
  lcd.startWrite();
  lcd.pushImage(0, 0, cui::W, cui::H, (const lgfx::rgb565_t*)fbmem);
  lcd.endWrite();
  const uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    uint16_t x, y;
    if (digitalRead(CUI_BTN) == LOW) return true;
    if (tp && tp->chsc6x_read_touch_info(&x, &y) == 0) return true;
    // countdown bar: same spot and colours as drawSplash() draws it
    lcd.fillRect(8, 230, (millis() - t0) * (cui::W - 16) / 3000, 4, cui::col::cyan);
    delay(20);
  }
  return false;
}

// SD card: read cities.bin once at boot on its own SPIClass (HSPI = SPI3 on the S3,
// TFT/SD pins 16/45/15, CS 3), then release the bus before the LCD takes SPI3.
// Never touches the global SPI object -- that one belongs to the radio.
void loadCitiesFromSd() {
  SPIClass sdspi(HSPI);
  sdspi.begin(CUI_TFT_SCLK, CUI_TFT_MISO, CUI_TFT_MOSI, CUI_SD_CS);
  if (!SD.begin(CUI_SD_CS, sdspi, 4000000)) { cui::onSdCard(false); sdspi.end(); return; }
  cui::onSdCard(true);
  File f = SD.open("/cities.bin");
  if (f) {
    const size_t n = f.size();
    if (n > 0 && n <= 4000000) {
      cities = (uint8_t*)ps_malloc(n);
      if (cities && f.read(cities, n) == n) cui::loadCities(cities, n);
    }
    f.close();
  }
  SD.end();
  sdspi.end();
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
  pinMode(CUI_TFT_BL, OUTPUT); digitalWrite(CUI_TFT_BL, HIGH);
  Serial1.begin(9600, SERIAL_8N1, CUI_GPS_RXD, CUI_GPS_TXD);
  fbmem = (uint16_t*)ps_malloc(cui::W * cui::H * 2);
  q = xQueueCreate(24, sizeof(Ev));
  cui::begin("Heltec V4-R8", "RNode 1.86 + ColorUI " COLORUI_VERSION);
  cui::setTzOffsetMinutes(-480);                 // the host can override with a TIME frame
  // 1) SD first, while SPI3 is still free (TFT held deselected), then release it
  pinMode(CUI_TFT_CS, OUTPUT); digitalWrite(CUI_TFT_CS, HIGH);
  loadCitiesFromSd();
  // 2) the LCD takes SPI3 for good
  lcd.init();
  lcd.setRotation(1);                            // landscape 320x240
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
  cui::setBeeps(true, false);                    // chirp on new area; new-station chirp off by default
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
