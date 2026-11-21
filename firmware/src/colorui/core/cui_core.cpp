#include "cui_state.h"
#include "cui_names.h"
#include "city/CityLocator.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <new>

namespace cui {

State S;

namespace {

inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

void rollMinutes(uint32_t now_ms) {
  const uint32_t m = now_ms / 60000;
  if (S.min_epoch == 0) { S.min_epoch = m; return; }
  while (S.min_epoch < m) {
    memmove(S.min_rx + 1, S.min_rx, sizeof(uint16_t) * (kMinuteBins - 1));
    memmove(S.min_tx + 1, S.min_tx, sizeof(uint16_t) * (kMinuteBins - 1));
    S.min_rx[0] = S.min_tx[0] = 0;
    ++S.min_epoch;
  }
}

void rollChannel(uint32_t now_ms) {
  const uint32_t sec = now_ms / 1000;
  if (S.chan_sec == 0) { S.chan_sec = sec; return; }
  while (S.chan_sec < sec) {
    ++S.chan_sec;
    const int col = (int)(S.chan_sec % 60);
    if (col == 0) {                       // new minute row at the top
      memmove(&S.chan[1][0], &S.chan[0][0], sizeof(int16_t) * 60 * (kChanRows - 1));
      memmove(&S.chan_flags[1][0], &S.chan_flags[0][0], 60 * (kChanRows - 1));
      for (int i = 0; i < 60; ++i) { S.chan[0][i] = -200; S.chan_flags[0][i] = 0; }
    }
    S.chan[0][col] = -200;
    S.chan_flags[0][col] = 0;
  }
}

int findStation(const uint8_t* dest) {
  for (int i = 0; i < kMaxStations; ++i)
    if (S.st[i].used && memcmp(S.st[i].dest, dest, 16) == 0) return i;
  return -1;
}

int upsertStation(const uint8_t* dest, uint32_t now_ms) {
  int i = findStation(dest);
  if (i >= 0) return i;
  int victim = 0;
  for (int k = 0; k < kMaxStations; ++k) {
    if (!S.st[k].used) { victim = k; break; }
    if (S.st[k].last_ms < S.st[victim].last_ms) victim = k;
  }
  Station& s = S.st[victim];
  s = Station();
  s.used = true;
  memcpy(s.dest, dest, 16);
  s.first_ms = now_ms;
  strncpy(s.area, S.area, sizeof(s.area) - 1);
  if (S.beep_station && !S.beep) S.beep = BEEP_NEW_STATION;
  return victim;
}

void enterArea(const char* name, uint32_t now_ms) {
  strncpy(S.area, name ? name : "", sizeof(S.area) - 1);
  S.area[sizeof(S.area) - 1] = 0;
  if (!name || !*name) return;
  if (S.visit_n == kMaxAreas) {
    memmove(&S.visits[0], &S.visits[1], sizeof(AreaVisit) * (kMaxAreas - 1));
    S.visit_n--;
  }
  S.visit_id++;
  AreaVisit& v = S.visits[S.visit_n++];
  memset(&v, 0, sizeof(v));
  strncpy(v.name, name, sizeof(v.name) - 1);
  v.enter_unix = unixNow();
  notify(N_AREA, name, now_ms);
  if (S.beep_area) S.beep = BEEP_NEW_AREA;
}

}  // namespace

uint32_t unixNow() {
  if (!S.unix_base) return 0;
  return S.unix_base + (S.now_ms - S.unix_base_ms) / 1000;
}

// ---- notifications --------------------------------------------------------
namespace {
struct NoteRule { uint16_t show_ms; uint16_t cooldown_s; uint8_t prio; };
// prio: 2 = card (always shown, jumps the queue), 1 = toast, 0 = toast that is
// dropped (not queued) while its cool-down runs.
const NoteRule kRules[N_COUNT] = {
  {0, 0, 0},
  {4500, 0, 2},     // N_AREA
  {3500, 15, 2},    // N_MSG     (repeats within 15 s fold into "x2", "x3")
  {2200, 3, 1},     // N_NODE    (bursts fold into "3 NEW NODES")
  {2000, 10, 0},    // N_HOST_UP
  {2500, 10, 0},    // N_HOST_LOST
  {2000, 60, 0},    // N_GPS_FIX
  {2000, 60, 0},    // N_GPS_LOST
  {2000, 60, 0},    // N_INTF
  {3000, 0, 1},     // N_BATT
};

void setText(NoteItem& n, const char* t) {
  strncpy(n.text, t ? t : "", sizeof(n.text) - 1);
  n.text[sizeof(n.text) - 1] = 0;
}
}  // namespace

void notify(Note kind, const char* text, uint32_t now_ms) {
  if (!S.notes_on || kind <= N_NONE || kind >= N_COUNT) return;
  const NoteRule& r = kRules[kind];
  const bool cooling = S.n_last[kind] && now_ms - S.n_last[kind] < (uint32_t)r.cooldown_s * 1000;
  S.n_last[kind] = now_ms ? now_ms : 1;
  // fold into the one on screen, or one already waiting
  if (S.ncur.kind == kind && now_ms < S.ncur_until) {
    if (S.ncur.count < 99) S.ncur.count++;
    setText(S.ncur, text);
    if (S.ncur_until < now_ms + 1500) S.ncur_until = now_ms + 1500;
    return;
  }
  for (int i = 0; i < S.nq_n; ++i)
    if (S.nq[i].kind == kind) { if (S.nq[i].count < 99) S.nq[i].count++; setText(S.nq[i], text); return; }
  if (cooling && r.prio == 0) return;
  NoteItem n; n.kind = kind; n.count = 1; setText(n, text);
  if (r.prio == 2) {
    // cards jump ahead of toasts; a toast on screen gets cut short
    if (S.ncur.kind && kRules[S.ncur.kind].prio < 2 && S.ncur_until > now_ms + 300) S.ncur_until = now_ms + 300;
    int at = 0;
    while (at < S.nq_n && kRules[S.nq[at].kind].prio == 2) ++at;
    if (S.nq_n == kNoteQ) S.nq_n--;                 // drop the newest toast
    for (int i = S.nq_n; i > at; --i) S.nq[i] = S.nq[i - 1];
    S.nq[at] = n; S.nq_n++;
  } else if (S.nq_n < kNoteQ) {
    S.nq[S.nq_n++] = n;
  }
  if (kind == N_MSG && S.beep_msg) S.beep = BEEP_MESSAGE;
}

void setMyDestinations(const uint8_t* h, int n) {
  if (n > kMyDests) n = kMyDests;
  if (n < 0) n = 0;
  memcpy(S.my_dest, h, (size_t)n * 16);
  S.my_n = n;
}

void setNotifications(bool on) { S.notes_on = on; if (!on) { S.nq_n = 0; S.ncur.kind = 0; } }

static void tickNotes(uint32_t now_ms) {
  if (S.ncur.kind && now_ms >= S.ncur_until) S.ncur.kind = 0;
  if (!S.ncur.kind && S.nq_n) {
    S.ncur = S.nq[0];
    for (int i = 1; i < S.nq_n; ++i) S.nq[i - 1] = S.nq[i];
    S.nq_n--;
    S.ncur_start = now_ms;
    S.ncur_until = now_ms + kRules[S.ncur.kind].show_ms;
  }
}

static bool isMine(const uint8_t* dest) {
  for (int i = 0; i < S.my_n; ++i) if (memcmp(S.my_dest[i], dest, 16) == 0) return true;
  return false;
}

uint32_t airtimeMsForBytes(uint16_t len) {
  // Semtech LoRa time-on-air (explicit header, CRC on, 18-symbol preamble like RNode).
  const RadioParams& r = S.radio;
  if (!r.sf || !r.bw_hz) return 0;
  const double tsym = (double)(1u << r.sf) / (double)r.bw_hz * 1000.0;
  const int de = (tsym > 16.0) ? 1 : 0;
  const int cr = r.cr ? r.cr - 4 : 1;
  double n = ceil((8.0 * (len + 1) - 4.0 * r.sf + 28 + 16) / (4.0 * (r.sf - 2 * de))) * (cr + 4);
  if (n < 0) n = 0;
  return (uint32_t)((18 + 4.25) * tsym + (8 + n) * tsym);
}

float airtimePct() {
  uint32_t sum = 0;
  for (int i = 0; i < S.air_n; ++i) {
    const int k = (S.air_head - 1 - i + kAirtimeLog) % kAirtimeLog;
    if (S.now_ms - S.air_t[k] > 3600000UL) break;
    sum += S.air_ms[k];
  }
  uint32_t window = S.now_ms - S.boot_ms;
  if (window > 3600000UL) window = 3600000UL;
  if (window < 60000UL) window = 60000UL;
  return 100.0f * (float)sum / (float)window;
}

int qualityBars(float snr) {
  // LoRa can decode below the noise: roughly -7.5 dB at SF7, 2.5 dB lower per SF step
  // (the same idea as the community fork's SF-scaled Q bars). 0 bars = at the floor,
  // 5 bars = 15 dB or more above it.
  const int sf = S.radio.sf ? S.radio.sf : 9;
  const float floor_db = -7.5f - 2.5f * (sf - 7);
  const float m = snr - floor_db;
  if (m <= 0) return 0;
  const int b = 1 + (int)(m / 3.75f);
  return b > 5 ? 5 : b;
}

uint8_t takeBeep() { const uint8_t b = S.beep; S.beep = 0; return b; }
void setBeeps(bool a, bool st) { S.beep_area = a; S.beep_station = st; }

void maidenhead(int32_t lat_e6, int32_t lon_e6, char* o) {
  double lon = lon_e6 / 1e6 + 180.0, lat = lat_e6 / 1e6 + 90.0;
  o[0] = 'A' + (int)(lon / 20); o[1] = 'A' + (int)(lat / 10);
  lon = fmod(lon, 20); lat = fmod(lat, 10);
  o[2] = '0' + (int)(lon / 2); o[3] = '0' + (int)(lat / 1);
  lon = fmod(lon, 2); lat = fmod(lat, 1);
  o[4] = 'a' + (int)(lon * 12); o[5] = 'a' + (int)(lat * 24);
  o[6] = 0;
}

void begin(const char* board, const char* fw) {
  // Re-construct in place: `S = State()` would build an ~18 KB temporary on the
  // caller's stack (loopTask has 8 KB) and crash with a stack-canary panic.
  S.~State();
  new (&S) State();
  S.board = board; S.fw = fw;
  for (int r = 0; r < kChanRows; ++r) for (int i = 0; i < 60; ++i) { S.chan[r][i] = -200; S.chan_flags[r][i] = 0; }
}

void setRadio(const RadioParams& p) { S.radio = p; }
void setTzOffsetMinutes(int m) { S.tz_min = m; }

void onPacket(const uint8_t* d, size_t len, int16_t rssi, float snr, bool tx, uint32_t now_ms) {
  S.now_ms = now_ms;
  if (!S.boot_ms) S.boot_ms = now_ms;
  rollMinutes(now_ms);
  rollChannel(now_ms);

  RnsInfo r;
  parseRns(d, len, r);

  LogEntry& e = S.log[S.log_head];
  memset(&e, 0, sizeof(e));
  e.t_ms = now_ms; e.tx = tx; e.len = (uint16_t)len;
  e.kind = r.valid ? r.kind : K_UNKNOWN;
  e.hops = r.hops; e.rssi = tx ? 0 : rssi; e.snr = tx ? 0 : snr;
  e.service = r.service; e.station = -1;
  e.has_via = r.header2;
  memcpy(e.dest, r.dest, 16);
  memcpy(e.via, r.transport_id, 16);
  memcpy(e.wanted, r.wanted, 16);
  strncpy(e.name, r.name, sizeof(e.name) - 1);

  if (tx) {
    S.tx_total++; S.last_tx_ms = now_ms; S.min_tx[0]++;
    S.chan_flags[0][S.chan_sec % 60] |= 1;
    S.air_t[S.air_head] = now_ms;
    S.air_ms[S.air_head] = (uint16_t)airtimeMsForBytes((uint16_t)len);
    S.air_head = (S.air_head + 1) % kAirtimeLog;
    if (S.air_n < kAirtimeLog) S.air_n++;
  } else {
    S.rx_total++; S.last_rx_ms = now_ms; S.min_rx[0]++;
    S.sig_rssi[S.sig_head] = rssi;
    S.sig_snr[S.sig_head] = (int8_t)(snr < -30 ? -30 : (snr > 30 ? 30 : snr));
    S.sig_head = (S.sig_head + 1) % kSigHist;
    if (S.sig_n < kSigHist) S.sig_n++;
    const int col = (int)(S.chan_sec % 60);
    if (rssi > S.chan[0][col]) S.chan[0][col] = rssi;
    if (r.header2) S.relayed_count++;
  }

  if (r.valid && (r.kind == K_ANNOUNCE || r.kind == K_PATHRESP) && !tx) {
    const int i = upsertStation(r.dest, now_ms);
    Station& s = S.st[i];
    if (r.name[0]) strncpy(s.name, r.name, sizeof(s.name) - 1);
    s.service = r.service;
    s.hops = r.hops; s.rssi = rssi; s.snr = snr; s.last_ms = now_ms; s.count++;
    s.has_via = r.header2;
    if (r.header2) memcpy(s.via, r.transport_id, 16);
    e.station = i;
    S.svc_count[(r.service >= 0 && r.service < 7) ? r.service : 7]++;
    if (s.count == 1) {                                 // first time we hear this one
      char h[16];
      if (!s.name[0]) hexShort(s.dest, h);
      notify(N_NODE, s.name[0] ? s.name : h, now_ms);
    }
  } else if (r.valid) {
    e.station = findStation(r.dest);   // data/link to someone who announced earlier
    if (e.station >= 0 && !tx) {
      Station& s = S.st[e.station];
      s.last_ms = now_ms; s.rssi = rssi; s.snr = snr; s.count++;
    }
    if (r.kind == K_PATHREQ) S.pathreq_count++;
  }

  if (!tx && r.valid && S.my_n && (r.ptype == P_DATA || r.ptype == P_LINKREQ) && isMine(r.dest))
    notify(N_MSG, r.ptype == P_LINKREQ ? "Link request" : "", now_ms);

  if (!tx && e.station >= 0 && S.visit_n > 0 && S.st[e.station].visit != S.visit_id) {
    S.st[e.station].visit = S.visit_id;
    S.visits[S.visit_n - 1].heard++;
  }
  S.log_head = (S.log_head + 1) % kLogLen;
  if (S.log_count < kLogLen) S.log_count++;
}

void onChannel(int16_t noise, int16_t live_rssi, bool intf, uint32_t now_ms) {
  S.now_ms = now_ms;
  rollChannel(now_ms);
  S.noise = noise;
  if (intf && !S.interference) { S.intf_events++; notify(N_INTF, "", now_ms); }
  S.interference = intf;
  const int col = (int)(S.chan_sec % 60);
  if (live_rssi > S.chan[0][col]) S.chan[0][col] = live_rssi;
  if (intf) S.chan_flags[0][col] |= 2;
}

void onGps(int32_t lat, int32_t lon, uint8_t sats, uint32_t unix_time, uint32_t now_ms) {
  S.now_ms = now_ms;
  if (!S.gps_fix) { char t[16]; snprintf(t, sizeof t, "%u satellites", (unsigned)sats); notify(N_GPS_FIX, t, now_ms); }
  S.gps_fix = true; S.lat = lat; S.lon = lon; S.sats = sats;
  if (unix_time) { S.unix_base = unix_time; S.unix_base_ms = now_ms; }
}

void onGpsLost(uint32_t now_ms) {
  S.now_ms = now_ms;
  if (S.gps_fix) notify(N_GPS_LOST, "", now_ms);
  S.gps_fix = false;
}

void onHostLink(bool c, uint32_t now_ms) {
  if (c && !S.host) notify(N_HOST_UP, "", now_ms);
  S.host = c; S.host_ms = now_ms;
}
void onBattery(int p) {
  S.battery = p;
  if (p >= 0 && p <= 15 && !S.batt_low) { char t[16]; snprintf(t, sizeof t, "%d%% left", p); notify(N_BATT, t, S.now_ms); S.batt_low = true; }
  if (p > 20) S.batt_low = false;
}
void onSdCard(bool p) { S.sd = p; }

void onHostFrame(const uint8_t* d, size_t len, uint32_t now_ms) {
  if (len < 2 || d[0] != 1) return;            // version 1 only
  if (!S.host) notify(N_HOST_UP, "", now_ms);
  S.host = true; S.host_ms = now_ms;
  const uint8_t type = d[1];
  if (type == HM_INTERFACES && len >= 9) {
    S.paths = rd16(d + 2); S.dests = rd16(d + 4); S.lxmf_q = rd16(d + 6);
    int n = d[8];
    size_t o = 9;
    S.if_n = 0;
    for (int i = 0; i < n && S.if_n < kMaxIfaces; ++i) {
      if (o + 3 > len) break;
      Iface& f = S.ifs[S.if_n];
      f.kind = d[o]; f.up = d[o + 1] & 1;
      const uint8_t nl = d[o + 2]; o += 3;
      if (o + nl + 12 > len) break;
      const size_t cp = nl < sizeof(f.name) - 1 ? nl : sizeof(f.name) - 1;
      memcpy(f.name, d + o, cp); f.name[cp] = 0; o += nl;
      f.bitrate = rd32(d + o); f.rxb = rd32(d + o + 4); f.txb = rd32(d + o + 8); o += 12;
      S.if_n++;
    }
    S.have_host_stats = true;
  } else if (type == HM_GPS && len >= 15) {
    if (!S.gps_fix || S.sats == 0)       // own GNSS wins when it has a fix
      onGps((int32_t)rd32(d + 2), (int32_t)rd32(d + 6), d[10], rd32(d + 11), now_ms);
  } else if (type == HM_TIME && len >= 8) {
    S.unix_base = rd32(d + 2); S.unix_base_ms = now_ms;
    S.tz_min = (int16_t)rd16(d + 6);
  } else if (type == HM_EVENT && len >= 4) {
    // u8 note kind, u8 text_len, text -- e.g. the host's LXMF client saying
    // "message from Alice" (it can read the message; the RNode can't)
    const uint8_t k = d[2], tl = d[3];
    char t[28] = {0};
    const size_t cp = (size_t)tl < len - 4 ? tl : len - 4;
    memcpy(t, d + 4, cp < sizeof(t) - 1 ? cp : sizeof(t) - 1);
    if (k > N_NONE && k < N_COUNT) notify((Note)k, t, now_ms);
  } else if (type == HM_MYDEST && len >= 3) {
    const int n = d[2];
    if (len >= (size_t)(3 + n * 16)) setMyDestinations(d + 3, n);
  }
}

bool loadCities(const uint8_t* data, size_t len) {
  S.cities = city::load(data, len);
  return S.cities;
}

void nextPage() { S.page = (S.page + 1) % PG_COUNT; }
void prevPage() { S.page = (S.page + PG_COUNT - 1) % PG_COUNT; }
void setPage(int p) { if (p >= 0 && p < PG_COUNT) S.page = p; }
int  page() { return S.page; }
void setDemo(bool on) { S.demo = on; }
void togglePrivacy() { S.privacy = !S.privacy; }
bool privacy() { return S.privacy; }

void tick(uint32_t now_ms) {
  S.now_ms = now_ms;
  if (!S.boot_ms) S.boot_ms = now_ms;
  rollMinutes(now_ms);
  rollChannel(now_ms);
  if (S.host && now_ms - S.host_ms > 30000) {                // no host frame for 30 s
    S.host = false;
    notify(N_HOST_LOST, "", now_ms);
  }
  tickNotes(now_ms);
  // City locator, same rules as WADAMESH: check every 10 s, accept a new
  // place only after two checks in a row agree (no flicker on boundaries).
  if (S.cities && S.gps_fix && now_ms - S.last_lookup_ms >= 10000) {
    S.last_lookup_ms = now_ms;
    const char* n = city::lookup(S.lat, S.lon);
    const char* name = n ? n : "";
    if (strcmp(name, S.area) == 0) { S.cand_hits = 0; return; }
    if (strcmp(name, S.cand) == 0) S.cand_hits++;
    else { strncpy(S.cand, name, sizeof(S.cand) - 1); S.cand[sizeof(S.cand) - 1] = 0; S.cand_hits = 1; }
    if (S.cand_hits >= 2) { enterArea(name, now_ms); S.cand_hits = 0; }
  }
}

}  // namespace cui
