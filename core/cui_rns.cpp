#include "cui_rns.h"
#include "cui_names.h"
#include <string.h>
#include <stdio.h>

namespace cui {
namespace {

// Copy a UTF-8-ish name, keeping printable ASCII and folding other bytes to '?'
// (the 8x8 font is ASCII only), stopping at maxlen.
void copyName(const uint8_t* s, size_t n, char* out, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; i < n && o + 1 < cap; ++i) {
    uint8_t c = s[i];
    if (c >= 0x20 && c < 0x7F) out[o++] = (char)c;
    else if (c >= 0xC0) out[o++] = '?';      // start of a multi-byte char
    // continuation bytes (0x80..0xBF) and control chars are dropped
  }
  out[o] = 0;
}

// Read a msgpack str/bin header at p; returns payload pointer and length, or nullptr.
const uint8_t* mpStrOrBin(const uint8_t* p, const uint8_t* end, size_t& n) {
  if (p >= end) return nullptr;
  uint8_t t = *p;
  if (t >= 0xA0 && t <= 0xBF) { n = t & 0x1F; p += 1; }
  else if ((t == 0xD9 || t == 0xC4) && p + 2 <= end) { n = p[1]; p += 2; }
  else if ((t == 0xDA || t == 0xC5) && p + 3 <= end) { n = (size_t)(p[1] << 8 | p[2]); p += 3; }
  else return nullptr;
  if (p + n > end) return nullptr;
  return p;
}

// LXMF 0.5+: msgpack array whose first item is the display name (bin/str/nil).
// Older LXMF and NomadNet: the app data is the raw UTF-8 name.
void nameFromAppData(const uint8_t* a, size_t n, int service, char* out, size_t cap) {
  out[0] = 0;
  if (n == 0) return;
  if ((a[0] >= 0x90 && a[0] <= 0x9F) || a[0] == 0xDC) {
    const uint8_t* p = a + (a[0] == 0xDC ? 3 : 1);
    size_t len = 0;
    const uint8_t* s = mpStrOrBin(p, a + n, len);
    if (s) copyName(s, len, out, cap);
    return;
  }
  if (service == 1) return;  // propagation node: app data is a settings list, no name
  // raw text: accept only if it looks like text
  for (size_t i = 0; i < n && i < 64; ++i)
    if (a[i] < 0x09) return;
  copyName(a, n, out, cap);
}

}  // namespace

bool parseRns(const uint8_t* d, size_t len, RnsInfo& r) {
  r = RnsInfo();
  if (len < 19) return false;
  const uint8_t flags = d[0];
  r.ifac = flags & 0x80;
  if (r.ifac) { r.valid = true; r.kind = K_IFAC; return true; }
  r.header2 = (flags >> 6) & 1;
  const bool ctxflag = (flags >> 5) & 1;
  r.dtype = (flags >> 2) & 3;
  r.ptype = flags & 3;
  r.hops = d[1];
  size_t o = 2;
  if (r.header2) {
    if (len < 35) return false;
    memcpy(r.transport_id, d + o, 16); o += 16;
  }
  memcpy(r.dest, d + o, 16); o += 16;
  r.context = d[o++];
  const uint8_t* data = d + o;
  const size_t dlen = len - o;
  r.valid = true;

  switch (r.ptype) {
    case P_ANNOUNCE: {
      r.kind = (r.context == 0x0B) ? K_PATHRESP : K_ANNOUNCE;   // PATH_RESPONSE context
      const size_t fixed = 64 + 10 + 10 + (ctxflag ? 32 : 0) + 64;
      if (dlen < 84) break;
      memcpy(r.name_hash, data + 64, 10);
      for (int i = 0; i < kServiceCount; ++i)
        if (memcmp(kServices[i].hash, r.name_hash, 10) == 0) { r.service = i; break; }
      if (dlen > fixed) nameFromAppData(data + fixed, dlen - fixed, r.service, r.name, sizeof(r.name));
      break;
    }
    case P_LINKREQ: r.kind = K_LINK; break;
    case P_PROOF:   r.kind = K_PROOF; break;
    default:
      if (r.dtype == D_PLAIN && memcmp(r.dest, kDest_PATHREQ, 16) == 0) {
        r.kind = K_PATHREQ;
        if (dlen >= 16) memcpy(r.wanted, data, 16);
      } else if (r.context == 0xFA) r.kind = K_KEEPALIVE;
      else if (r.context == 0xFC) r.kind = K_LINKCLOSE;
      else if (r.context >= 0x01 && r.context <= 0x07) r.kind = K_RESOURCE;
      else r.kind = K_DATA;
  }
  return true;
}

const char* kindLabel(Kind k) {
  switch (k) {
    case K_ANNOUNCE: return "ANN";  case K_DATA: return "DATA"; case K_LINK: return "LINK";
    case K_PROOF: return "PRF";     case K_PATHREQ: return "PATH?"; case K_PATHRESP: return "PATH";
    case K_KEEPALIVE: return "KA";  case K_RESOURCE: return "FILE"; case K_LINKCLOSE: return "CLOSE";
    case K_IFAC: return "IFAC";     default: return "?";
  }
}

const char* contextLabel(uint8_t c) {
  switch (c) {
    case 0x00: return "none"; case 0x01: return "resource"; case 0x02: return "resource adv";
    case 0x03: return "resource req"; case 0x05: return "resource proof"; case 0x08: return "cache req";
    case 0x09: return "request"; case 0x0A: return "response"; case 0x0B: return "path response";
    case 0x0C: return "command"; case 0x0E: return "channel"; case 0xFA: return "keepalive";
    case 0xFB: return "link identify"; case 0xFC: return "link close"; case 0xFD: return "link proof";
    case 0xFE: return "link rtt"; case 0xFF: return "link req proof"; default: return "other";
  }
}

void hexShort(const uint8_t* h, char* out) {
  snprintf(out, 14, "<%02x%02x..%02x%02x>", h[0], h[1], h[14], h[15]);
}

}  // namespace cui
