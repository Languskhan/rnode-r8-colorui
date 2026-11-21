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
  N_COUNT
};
void notify(Note kind, const char* text, uint32_t now_ms);
// Destination hashes that belong to the user (e.g. their LXMF delivery
// address). A DATA or LINKREQ packet for one of them raises N_MSG. The
// contents stay encrypted -- the RNode only sees the address on the outside.
void setMyDestinations(const uint8_t* hashes16, int n);
void setNotifications(bool on);

void tick(uint32_t now_ms);
void render(Fb& fb, uint32_t now_ms);

// Boot splash scenes (cui_splash.cpp): a different one each boot.
int  splashCount();
void drawSplash(Fb& fb, int scene, const char* version, int progress);

// Host stats frame (CMD 0xA0) -- see docs/HOST_PROTOCOL.md
enum HostMsg : uint8_t { HM_INTERFACES = 1, HM_GPS = 2, HM_TIME = 3, HM_EVENT = 4, HM_MYDEST = 5 };
enum IfKind : uint8_t { IF_LORA = 0, IF_KISS_VU = 1, IF_KISS_HF = 2, IF_TCP = 3, IF_AUTO = 4, IF_OTHER = 5 };

}  // namespace cui
