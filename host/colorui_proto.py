"""ColorUI host protocol (CMD_UI_HOST = 0xA0 KISS frames from host to RNode).

The RNode firmware ignores this command unless it is a ColorUI build, so it
is always safe to send. All integers are little-endian.

  byte 0   version (1)
  byte 1   message type
  type 1 INTERFACES:
           u16 paths, u16 known_destinations, u16 lxmf_queue, u8 count,
           then per interface: u8 kind, u8 flags(bit0=up), u8 name_len, name,
                               u32 bitrate, u32 rx_bytes, u32 tx_bytes
  type 2 GPS:  i32 lat_e6, i32 lon_e6, u8 sats, u32 unix_time
  type 3 TIME: u32 unix_time, i16 tz_offset_minutes
  type 4 EVENT: u8 note_kind, u8 text_len, text (<= 27 bytes)
           note kinds: 1 area, 2 message, 3 new node, 4 host up, 5 host lost,
                       6 GPS fix, 7 GPS lost, 8 interference, 9 battery low
  type 5 MYDEST: u8 count (<= 4), then count x 16-byte destination hashes.
           A DATA or LINKREQ packet for one of these raises "Message for you".
"""
import struct

CMD_UI_HOST = 0xA0
FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD

IF_LORA, IF_KISS_VU, IF_KISS_HF, IF_TCP, IF_AUTO, IF_OTHER = range(6)


def pack_interfaces(ifaces, paths=0, dests=0, lxmf_queue=0):
    """ifaces: list of dicts with kind, up, name, bitrate, rxb, txb."""
    b = bytearray(struct.pack("<BBHHHB", 1, 1, paths & 0xFFFF, dests & 0xFFFF, lxmf_queue & 0xFFFF, min(len(ifaces), 6)))
    for f in ifaces[:6]:
        name = f["name"].encode("utf-8")[:19]
        b += struct.pack("<BBB", f["kind"], 1 if f["up"] else 0, len(name)) + name
        b += struct.pack("<III", int(f["bitrate"]) & 0xFFFFFFFF, int(f["rxb"]) & 0xFFFFFFFF, int(f["txb"]) & 0xFFFFFFFF)
    return bytes(b)


def pack_gps(lat, lon, sats, unix_time):
    return struct.pack("<BBiiBI", 1, 2, int(lat * 1e6), int(lon * 1e6), sats, int(unix_time))


def pack_time(unix_time, tz_minutes):
    return struct.pack("<BBIh", 1, 3, int(unix_time), int(tz_minutes))


NOTE_AREA, NOTE_MSG, NOTE_NODE, NOTE_HOST_UP, NOTE_HOST_LOST, NOTE_GPS_FIX, NOTE_GPS_LOST, NOTE_INTF, NOTE_BATT = range(1, 10)


def pack_event(kind, text=""):
    t = str(text).encode("utf-8")[:27]
    return bytes([1, 4, kind & 0xFF, len(t)]) + t


def pack_mydest(hashes):
    hashes = [bytes.fromhex(h) if isinstance(h, str) else bytes(h) for h in hashes][:4]
    return bytes([1, 5, len(hashes)]) + b"".join(hashes)


def kiss_frame(payload, command=CMD_UI_HOST):
    out = bytearray([FEND, command])
    for c in payload:
        if c == FEND:   out += bytes([FESC, TFEND])
        elif c == FESC: out += bytes([FESC, TFESC])
        else:           out.append(c)
    out.append(FEND)
    return bytes(out)
