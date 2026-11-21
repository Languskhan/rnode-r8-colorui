// Reticulum packet header reader for the display.
//
// Reads ONLY the parts of a Reticulum packet that travel in the clear:
// flags, hop count, transport ID, destination hash, context byte, and
// (for announces) the name hash and app data. Message contents are
// encrypted end-to-end by Reticulum and are never touched.
//
// Wire layout (RNS 1.x, Packet.pack):
//   [flags][hops][transport_id(16) if HEADER_2][dest_hash(16)][context][data...]
//   flags = ifac<<7 | header_type<<6 | context_flag<<5 | transport_type<<4
//         | dest_type<<2 | packet_type
// Announce data:
//   public_key(64) name_hash(10) random_hash(10) [ratchet(32) if context_flag]
//   signature(64) [app_data...]
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace cui {

enum PType : uint8_t { P_DATA = 0, P_ANNOUNCE = 1, P_LINKREQ = 2, P_PROOF = 3 };
enum DType : uint8_t { D_SINGLE = 0, D_GROUP = 1, D_PLAIN = 2, D_LINK = 3 };

enum Kind : uint8_t {          // what the display calls the packet
  K_ANNOUNCE, K_DATA, K_LINK, K_PROOF, K_PATHREQ, K_PATHRESP,
  K_KEEPALIVE, K_RESOURCE, K_LINKCLOSE, K_IFAC, K_UNKNOWN
};

struct RnsInfo {
  bool     valid = false;
  bool     ifac = false;           // interface access code: header is masked, can't read
  uint8_t  ptype = 0, dtype = 0, hops = 0, context = 0;
  bool     header2 = false;        // carries a transport ID (it was relayed)
  uint8_t  transport_id[16] = {0};
  uint8_t  dest[16] = {0};
  Kind     kind = K_UNKNOWN;
  // announce only
  int      service = -1;           // index into kServices, -1 = unknown app
  uint8_t  name_hash[10] = {0};
  char     name[33] = {0};         // display/node name from app data, UTF-8, may be empty
  // path request only
  uint8_t  wanted[16] = {0};
};

// Parse one Reticulum packet (RNode's own 1-byte LoRa header already removed).
bool parseRns(const uint8_t* d, size_t len, RnsInfo& out);

const char* kindLabel(Kind k);     // "ANN", "DATA", ...
const char* contextLabel(uint8_t ctx);
void hexShort(const uint8_t* h, char* out);  // "<a3f9..7c21>" style, 13 chars + NUL

}  // namespace cui
