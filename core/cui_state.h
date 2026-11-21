// Internal state shared by cui_core.cpp (logic) and cui_render.cpp (drawing).
#pragma once
#include "cui.h"
#include "cui_rns.h"

namespace cui {

constexpr int kMaxStations = 64;
constexpr int kLogLen      = 64;
constexpr int kSigHist     = 48;
constexpr int kMinuteBins  = 30;
constexpr int kChanRows    = 5;     // minutes of channel waterfall
constexpr int kAirtimeLog  = 256;
constexpr int kMaxAreas    = 8;
constexpr int kMaxIfaces   = 6;
constexpr int kNoteQ       = 4;
constexpr int kMyDests     = 4;
constexpr int kSats        = 32;

// overlays (views opened by a tap) and per-page tabs
enum Overlay : uint8_t { OV_NONE = 0, OV_STATION, OV_PACKET, OV_TRACK, OV_QUICK };
struct HourBin { uint32_t hour; uint16_t pkts, nodes; int32_t noise_sum; uint16_t noise_n; };
struct DayBin  { uint32_t day;  uint32_t pkts; uint16_t nodes; };

struct Station {
  bool     used = false;
  uint8_t  dest[16];
  char     name[33];
  int      service = -1;
  uint8_t  hops = 0;
  int16_t  rssi = 0;
  float    snr = 0;
  uint32_t last_ms = 0, first_ms = 0, count = 0;
  bool     has_via = false;
  uint8_t  via[16];
  char     area[28];                 // where you were when first heard
  int      visit = -1;               // last area visit it was counted in
  int8_t   hist[16];                 // RSSI of the last packets (dBm, clamped)
  uint8_t  hist_n = 0, hist_head = 0;
  uint32_t watch_ms = 0;             // last time a watch-list note fired for it
};

struct LogEntry {
  uint32_t t_ms;
  Kind     kind;
  bool     tx;
  uint8_t  dest[16];
  uint8_t  hops;
  int16_t  rssi;
  float    snr;
  uint16_t len;
  int      service;
  int      station;                  // index into stations (name source), -1 = none
  bool     has_via;
  uint8_t  via[16];
  uint8_t  wanted[16];
  char     name[24];                 // name carried by this packet itself (announces)
  uint8_t  flags, context, ptype, dtype;
};

struct AreaVisit {
  char     name[28];
  uint32_t enter_unix;
  uint16_t heard;                    // stations heard while there
};

struct Iface {
  uint8_t  kind;
  bool     up;
  char     name[20];
  uint32_t bitrate, rxb, txb;
};

struct NoteItem {
  uint8_t kind = 0;
  uint8_t count = 0;
  char    text[28] = {0};
};

struct State {
  // identity
  const char* board = "";
  const char* fw = "";
  RadioParams radio;
  int  tz_min = -480;
  int  page = PG_HEARD;
  bool privacy = false;
  bool demo = false;
  uint32_t boot_ms = 0, now_ms = 0;

  // traffic
  Station  st[kMaxStations];
  LogEntry log[kLogLen];
  int      log_head = 0, log_count = 0;
  uint32_t rx_total = 0, tx_total = 0;
  uint32_t last_rx_ms = 0, last_tx_ms = 0;
  int16_t  sig_rssi[kSigHist];
  int8_t   sig_snr[kSigHist];
  int      sig_n = 0, sig_head = 0;
  uint16_t min_rx[kMinuteBins], min_tx[kMinuteBins];
  uint32_t min_epoch = 0;            // minute number of bin 0's "now"
  uint32_t air_t[kAirtimeLog];
  uint16_t air_ms[kAirtimeLog];
  int      air_head = 0, air_n = 0;
  uint32_t svc_count[8] = {0};       // announces per service (+ index 7 = unknown)
  uint32_t pathreq_count = 0;
  uint32_t relayed_count = 0;

  // channel
  int16_t  noise = -120;
  bool     interference = false;
  uint32_t intf_events = 0;
  int16_t  chan[kChanRows][60];       // per-second peak RSSI, -200 = quiet
  uint8_t  chan_flags[kChanRows][60]; // bit0 = we transmitted, bit1 = interference
  uint32_t chan_sec = 0;              // absolute second of the newest cell

  // gps / area
  bool     gps_fix = false;
  int32_t  lat = 0, lon = 0;
  uint8_t  sats = 0;
  uint32_t unix_base = 0, unix_base_ms = 0;
  bool     cities = false;
  char     area[28] = {0};
  char     cand[28] = {0};
  int      cand_hits = 0;
  uint32_t last_lookup_ms = 0;
  AreaVisit visits[kMaxAreas];
  int      visit_n = 0;
  int      visit_id = 0;             // increments on every new area
  uint32_t popup_until = 0;
  char     popup[40] = {0};

  // host
  bool     host = false;
  uint32_t host_ms = 0;
  Iface    ifs[kMaxIfaces];
  int      if_n = 0;
  uint16_t paths = 0, dests = 0, lxmf_q = 0;
  bool     have_host_stats = false;
  int      battery = -1;
  bool     sd = false;
  uint8_t  beep = 0;                 // pending sound cue for the device (Beep enum)
  bool     beep_area = true, beep_station = false, beep_msg = true;

  // notifications
  bool     notes_on = true;
  NoteItem nq[kNoteQ];
  int      nq_n = 0;
  NoteItem ncur;
  uint32_t ncur_start = 0, ncur_until = 0;
  uint32_t n_last[16] = {0};          // last time each Note kind was raised
  uint8_t  my_dest[kMyDests][16];
  int      my_n = 0;
  bool     batt_low = false;

  // look and connection
  bool     retro = false;            // theme=retro: the 0.6 pixel-art pages
  bool     clock12 = false;
  uint8_t  link = 0;                 // LINK_* : how the host reaches the RNode
  char     ip[16] = {0};             // Wi-Fi address when link == LINK_WIFI
  char     web[40] = {0};            // file manager URL
  uint32_t msg_unread = 0;           // N_MSG count since the Heard page was last opened
  bool     idle = false;
  // your announce heard coming back through another node
  uint32_t echo_count = 0, echo_ms = 0;
  uint8_t  echo_hops = 0;
  uint8_t  echo_via[16] = {0};
  uint32_t last_air_check = 0;
  int16_t  touch_x = 0, touch_y = 0;
  uint32_t touch_ms = 0;
  // 0.8
  uint8_t  tab[PG_COUNT] = {0};
  uint8_t  scroll[PG_COUNT] = {0};
  uint8_t  ov = OV_NONE;
  int      ov_idx = -1;              // station index (OV_STATION/OV_TRACK) or log slot (OV_PACKET)
  uint32_t ov_ms = 0;
  uint8_t  node_sort = 0;            // 0 recent, 1 signal, 2 hops
  uint8_t  node_filter = 0;          // 0 all, 1 people, 2 nomadnet, 3 propagation, 4 calls
  uint16_t tone = 0;
  char     host_out[24] = {0};
  SatInfo  sats_v[kSats];
  int      sats_n = 0, sats_used = 0;
  float    hdop = 0;
  HourBin  hours[24];
  DayBin   days[7];
  bool     settings_dirty = false;
  bool     force_idle = false;
};

extern State S;

void renderRetro(Fb& fb, uint32_t now);   // cui_render_retro.cpp

uint32_t unixNow();                  // 0 if no time yet
float    airtimePct();
int      qualityBars(float snr);    // 0..5, scaled to the current spreading factor               // TX airtime over the last hour
uint32_t airtimeMsForBytes(uint16_t len);
void     maidenhead(int32_t lat_e6, int32_t lon_e6, char* out);  // 6-char grid square

}  // namespace cui
