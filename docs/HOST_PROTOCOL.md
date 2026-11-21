# Host -> ColorUI protocol (KISS command 0xA0)

The host sends ordinary KISS frames on the RNode's USB serial line:
`FEND 0xA0 <payload, KISS-escaped> FEND`. Upstream RNode firmware has no
command 0xA0, so a stock RNode ignores these frames. Nothing goes on air.

All integers little-endian. Payload:

| bytes | field |
|---|---|
| 0 | version = 1 |
| 1 | type |

**type 1, INTERFACES** (every ~10 s): `u16 paths, u16 known_destinations,
u16 lxmf_queue, u8 count`, then per interface (max 6):
`u8 kind, u8 flags (bit0 = up), u8 name_len, name[name_len], u32 bitrate,
u32 rx_bytes, u32 tx_bytes`.
kind: 0 LoRa, 1 VHF/UHF KISS, 2 HF KISS, 3 TCP/IP, 4 Wi-Fi/LAN, 5 other.

**type 2, GPS** (optional): `i32 lat_e6, i32 lon_e6, u8 sats, u32 unix_time`.
Used only when the Heltec's own GNSS has no fix.

**type 3, TIME** (every ~60 s): `u32 unix_time, i16 tz_offset_minutes`.

Reference packers: `host/colorui_proto.py`; parser: `cui::onHostFrame` in
`core/cui_core.cpp`.

## Type 4 EVENT (v0.6)
`u8 note_kind, u8 text_len, text (<= 27 bytes)` -- pops a notification.
Kinds: 1 area, 2 message, 3 new node, 4 host up, 5 host lost, 6 GPS fix,
7 GPS lost, 8 interference, 9 battery low. See docs/NOTIFICATIONS.md.

## Type 5 MYDEST (v0.6)
`u8 count (<= 4), count x 16-byte destination hashes` -- the user's own
addresses. A DATA or LINKREQ packet for one of them raises "Message for you".
The plugin sends this every minute from `colorui_my_destinations`.
