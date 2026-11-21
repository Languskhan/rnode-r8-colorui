// RNode ColorUI -- public API of the display core.
//
// The core is single-threaded plain C++. On the device, the radio side
// (RNode firmware, core 1) only pushes events into a queue; the UI task on
// core 0 drains the queue into these calls and renders. In the PC simulator
// the same calls are made from a recorded trace.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "cui_fb.h"

namespace cui {

struct RadioParams {
  uint32_t freq_hz = 0, bw_hz = 0;
  uint8_t  sf = 0, cr = 0;
  int8_t   txp = 0;
};

enum Page : int { PG_HEARD, PG_LIST, PG_GRAPHS, PG_CHANNEL, PG_AREA, PG_IFACES, PG_STATUS, PG_COUNT };

void begin(const char* board_name, const char* fw_version);
void setRadio(const RadioParams& p);
void setTzOffsetMinutes(int minutes);           // local time for the clock / day-night art

// A Reticulum packet received (tx=false) or sent (tx=true) on LoRa.
// d/len = payload without RNode's 1-byte LoRa header.
void onPacket(const uint8_t* d, size_t len, int16_t rssi, float snr, bool tx, uint32_t now_ms);
// Once a second from the radio side: current noise floor and interference flag.
void onChannel(int16_t noise_dbm, int16_t live_rssi, bool interference, uint32_t now_ms);
void onGps(int32_t lat_e6, int32_t lon_e6, uint8_t sats, uint32_t unix_time, uint32_t now_ms);
void onGpsLost(uint32_t now_ms);
// Payload of a CMD_UI_HOST (0xA0) KISS frame from the host plugin.
void onHostFrame(const uint8_t* d, size_t len, uint32_t now_ms);
void onHostLink(bool connected, uint32_t now_ms);
void onBattery(int percent);                     // -1 = no battery / unknown
void onSdCard(bool present);
// Offline place boundaries (WADAMESH cities.bin format). Caller keeps data alive.
bool loadCities(const uint8_t* data, size_t len);

void nextPage();
void prevPage();
void setPage(int p);
int  page();
void setDemo(bool on);                           // shows a DEMO tag in the header
void togglePrivacy();                            // blur names/hashes
bool privacy();

// Sound cues for the buzzer (device plays them; the sim ignores them).
enum Beep : uint8_t { BEEP_NONE = 0, BEEP_NEW_AREA = 1, BEEP_NEW_STATION = 2, BEEP_MESSAGE = 3 };
uint8_t takeBeep();                              // returns and clears the pending cue
void setBeeps(bool new_area, bool new_station);

// Graphic notifications (cards + toasts). The core raises most of them
// itself; the host or the device can raise them too. Per-packet RX/TX never
// pops anything: that traffic is shown live on the pages instead.
enum Note : uint8_t {
  N_NONE = 0,
  N_AREA,        // card: "Welcome to <city>"      (text = city)
  N_MSG,         // card: something arrived for you (text = optional detail)
  N_NODE,        // toast: first announce from a new node (text = name)
  N_HOST_UP,     // toast: computer running Reticulum connected
  N_HOST_LOST,   // toast: computer went quiet for 30 s
  N_GPS_FIX,     // toast: GPS fix acquired
  N_GPS_LOST,    // toast: GPS fix lost
  N_INTF,        // toast: interference detected on the channel
  N_BATT,        // toast: battery low
  N_ECHO,        // toast: another node repeated YOUR announce (text = who)
  N_AIRTIME,     // toast: TX airtime near the 10 %/hour budget
  N_COUNT
};
void notify(Note kind, const char* text, uint32_t now_ms);
// Destination hashes that belong to the user (e.g. their LXMF delivery
// address). A DATA or LINKREQ packet for one of them raises N_MSG. The
// contents stay encrypted -- the RNode only sees the address on the outside.
void setMyDestinations(const uint8_t* hashes16, int n);
void setNotifications(bool on);

// How the host computer is connected to the RNode (shown in the status bar).
enum Link : uint8_t { LINK_NONE = 0, LINK_USB, LINK_WIFI, LINK_BT };
void setLink(uint8_t link, const char* ip);       // ip only for LINK_WIFI (may be null)
// Theme: "night" (default), "soft", "amber", or "retro" (the 0.6 pixel look).
void setThemeName(const char* name);
void setClock12(bool on);
// Touch: a tap at (x,y) in screen pixels. Taps on the left rail pick a page;
// returns true if the tap was used.
bool onTap(int x, int y);

// ---- SD card files (read once at boot by the device; plain text) ----------
// names.txt: one "<hash hex, 8-32 chars>  <nickname>" per line, # = comment.
// Matches destination hashes AND relay (transport) IDs by prefix.
int  loadNames(const char* text, size_t len);    // returns how many were loaded
const char* nickname(const uint8_t* hash16);     // nullptr if none
// settings.ini: "key = value" lines, see docs/SD_CARD.md.
struct Settings {
  char     brand[28]     = "";       // splash title ("" = firmware default)
  char     theme[12]     = "night";
  int      tz_min        = -480;     // minutes from UTC (also: tz = -8)
  bool     clock12       = false;
  bool     beep_area = true, beep_node = false, beep_msg = true, beep_echo = true;
  bool     notes         = true;
  int      idle_min      = 5;        // screensaver after this many minutes untouched (0 = never)
  int      brightness    = 100;      // percent
  int      dim           = 15;       // percent while the screensaver runs
  int      start_page    = 0;
  bool     privacy       = false;
  bool     touch_flip    = false;    // flip tap Y if rail taps land on the wrong page
  bool     log           = true;     // write /logs/*.csv on the SD card
  bool     sd_live       = true;     // keep the SD mounted (map tiles, logs); off = read at boot only
  uint8_t  my_dest[4][16];           // my_lxmf = hash[,hash...]
  int      my_n          = 0;
};
void applySettings(const char* text, size_t len);   // parse + apply to the core
const Settings& settings();

// Coverage: every packet heard with a GPS fix becomes a sample (for the
// Area page plot and the SD log).
struct CovSample { int32_t lat, lon; int16_t rssi; int8_t snr; uint8_t hops; uint32_t unix; };
int  coverageCount();
const CovSample& coverage(int i);                // 0 = oldest

// Screensaver: the device says when the screen has been idle; while idle the
// core draws a dim clock. wantWake() is true while a card notification shows.
void setIdle(bool idle);
bool idle();
bool wantWake();

// Map under the Area page's coverage plot (device: offline tiles from SD).
typedef bool (*MapPainter)(Fb& fb, int x, int y, int w, int h, int32_t lat_e6, int32_t lon_e6);
void setMapPainter(MapPainter p);

void tick(uint32_t now_ms);
void render(Fb& fb, uint32_t now_ms);

// Boot splash scenes (cui_splash.cpp): a different one each boot.
int  splashCount();
void drawSplash(Fb& fb, int scene, const char* version, int progress);
void drawSplashStrip(Fb& fb, const char* caption, const char* version, int progress);
void setBrand(const char* brand);                // settings.ini brand = ... ("" = default)

// CSV lines for packets logged after *last_ms (updated). For /logs on the SD.
// Columns: unix,local_time,dir,type,name,hash,hops,rssi,snr,lat,lon,area
size_t heardCsv(uint32_t* last_ms, char* buf, size_t cap);
const char* heardCsvHeader();

// Host stats frame (CMD 0xA0) -- see docs/HOST_PROTOCOL.md
enum HostMsg : uint8_t { HM_INTERFACES = 1, HM_GPS = 2, HM_TIME = 3, HM_EVENT = 4, HM_MYDEST = 5 };
enum IfKind : uint8_t { IF_LORA = 0, IF_KISS_VU = 1, IF_KISS_HF = 2, IF_TCP = 3, IF_AUTO = 4, IF_OTHER = 5 };

}  // namespace cui
