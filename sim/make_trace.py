#!/usr/bin/env python3
"""Build a simulator trace from REAL Reticulum packets.

Every packet in the trace is made by the actual `rns` / LXMF wire format:
announces are produced by RNS.Destination.announce(send=False), data packets
are encrypted by RNS for their recipient, path requests go to the real plain
destination. Nothing is transmitted -- Reticulum runs with no interfaces.

Also writes demo-places.bin (WADAMESH cities.bin format) with a few simple,
made-up area boundaries for the GPS drive. They are NOT real city limits;
on the device you use a real cities.bin built from Census data.
"""
import os, sys, random, struct, math, tempfile, time
import msgpack, RNS

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "host"))
import colorui_proto as P

random.seed(42)
OUT = sys.argv[1] if len(sys.argv) > 1 else "sim/trace.txt"
PLACES = sys.argv[2] if len(sys.argv) > 2 else "sim/demo-places.bin"

cfg = tempfile.mkdtemp()
open(os.path.join(cfg, "config"), "w").write("[reticulum]\n  enable_transport = No\n  share_instance = No\n[interfaces]\n")
RNS.loglevel = RNS.LOG_CRITICAL
RNS.Reticulum(configdir=cfg, loglevel=RNS.LOG_CRITICAL)

def lxmf_app_data(name):          # LXMF 0.5+ announce format (LXMRouter.get_announce_app_data)
    return msgpack.packb([name.encode("utf-8"), None, [0x01], None])

class Peer:
    def __init__(self, name, aspect=("lxmf", "delivery"), app_data=None, rssi=-95, hops=1, via=None):
        self.id = RNS.Identity()
        self.dest = RNS.Destination(self.id, RNS.Destination.IN, RNS.Destination.SINGLE, *aspect)
        self.name, self.app_data, self.rssi, self.hops, self.via = name, app_data, rssi, hops, via
        self.out = RNS.Destination(self.id, RNS.Destination.OUT, RNS.Destination.SINGLE, *aspect)

    def announce_raw(self):
        # Same bytes RNS.Destination.announce() builds (no ratchet), done by hand
        # because the library's announce path is slow without a running network.
        random_hash = os.urandom(5) + int(time.time()).to_bytes(5, "big")
        pub = self.id.get_public_key()
        app = self.app_data or b""
        signed = self.dest.hash + pub + self.dest.name_hash + random_hash + app
        data = pub + self.dest.name_hash + random_hash + self.id.sign(signed) + app
        flags = (RNS.Destination.SINGLE << 2) | RNS.Packet.ANNOUNCE
        raw = bytearray([flags, self.hops]) + self.dest.hash + b"\x00" + data
        if self.via is not None:          # relayed: HEADER_2 + TRANSPORT, transport id inserted
            raw[0] = raw[0] | 0x40 | 0x10
            raw = raw[:2] + self.via + raw[2:]
        return bytes(raw)

    def data_to_raw(self, size):
        p = RNS.Packet(self.out, os.urandom(size))
        p.pack()
        raw = bytearray(p.raw); raw[1] = self.hops
        return bytes(raw)

roof = os.urandom(16)                       # a transport node's ID ("via")
peers = [
    Peer("Hilltop Sideband", app_data=lxmf_app_data("Hilltop Sideband"), rssi=-94, hops=2, via=roof),
    Peer("Rancho Cordova RN", app_data=lxmf_app_data("Rancho Cordova RN"), rssi=-88, hops=1),
    Peer("SacMesh Info", aspect=("nomadnetwork", "node"), app_data="SacMesh Info".encode(), rssi=-109, hops=3, via=roof),
    Peer("Folsom Prop", aspect=("lxmf", "propagation"), app_data=msgpack.packb([False, 1763600000, True, 256, 10240, [0, 0], {}]), rssi=-101, hops=2, via=roof),
    Peer("Trailhead T-Deck", app_data=lxmf_app_data("Trailhead T-Deck"), rssi=-112, hops=1),
    Peer("KJ6 Mobile", app_data=lxmf_app_data("KJ6 Mobile"), rssi=-79, hops=1),
    Peer("Ridge Repeater", aspect=("rnstransport", "discovery", "interface"), app_data=None, rssi=-90, hops=1),
    Peer("anon", app_data=None, rssi=-104, hops=2),       # announces without a name
]
me = Peer("KO6FPT Zero", app_data=lxmf_app_data("KO6FPT Zero"), hops=0)
pathreq_dest = RNS.Destination(None, RNS.Destination.OUT, RNS.Destination.PLAIN, "rnstransport", "path", "request")

def path_request_raw(target):
    p = RNS.Packet(pathreq_dest, target + os.urandom(10), header_type=RNS.Packet.HEADER_1)
    p.pack(); return p.raw

def link_request_raw(peer):
    raw = bytearray([0x02, peer.hops]) + peer.dest.hash + b"\x00" + os.urandom(64 + 3)
    return bytes(raw)

def proof_raw():
    raw = bytearray([0x03 | (3 << 2), 1]) + os.urandom(16) + b"\x00" + os.urandom(64)
    return bytes(raw)

# ---- demo places (made up, simple shapes) -------------------------------
def place(name, ring_lonlat, kind=0):
    nb = name.encode()
    lats = [y for x, y in ring_lonlat]; lons = [x for x, y in ring_lonlat]
    e6 = lambda v: int(round(v * 1e6))
    b = bytearray(struct.pack("<BB", kind, len(nb)) + nb)
    b += struct.pack("<iiiiH", e6(min(lats)), e6(min(lons)), e6(max(lats)), e6(max(lons)), 1)
    b += struct.pack("<H", len(ring_lonlat))
    for x, y in ring_lonlat: b += struct.pack("<ii", e6(y), e6(x))
    return bytes(b)
recs = [
    place("Fair Oaks",    [(-121.300, 38.620), (-121.235, 38.620), (-121.240, 38.665), (-121.300, 38.668)], 1),
    place("Orangevale",   [(-121.235, 38.640), (-121.180, 38.645), (-121.180, 38.700), (-121.240, 38.700)], 1),
    place("Folsom",       [(-121.180, 38.630), (-121.100, 38.630), (-121.100, 38.720), (-121.180, 38.720)], 0),
    place("Citrus Heights", [(-121.320, 38.668), (-121.240, 38.668), (-121.240, 38.720), (-121.320, 38.720)], 0),
]
open(PLACES, "wb").write(b"WCTY" + struct.pack("<BBH", 1, 0, len(recs)) + b"".join(recs))

# ---- timeline -----------------------------------------------------------
T_END = 31 * 60 * 1000                    # 31 minutes of traffic
UNIX0 = 1795221000                        # 2026-11-20 16:30 PST (00:30 UTC Nov 21)
lines = []
def ev(t, s): lines.append((t, s))

# host: interfaces incl. KISS radios on the Cardputer Zero
def host_frame(t):
    k = t / T_END
    ifs = [
        dict(kind=P.IF_LORA,    up=True,  name="RNode Heltec V4-R8", bitrate=1758,   rxb=int(180000 * k), txb=int(41000 * k)),
        dict(kind=P.IF_KISS_VU, up=True,  name="UV-PRO BT KISS",     bitrate=1200,   rxb=int(22000 * k),  txb=int(3000 * k)),
        dict(kind=P.IF_KISS_HF, up=False, name="HF Direwolf KISS",   bitrate=300,    rxb=0,               txb=0),
        dict(kind=P.IF_TCP,     up=True,  name="Backbone TCP",       bitrate=10000000, rxb=int(2.4e6 * k), txb=int(310000 * k)),
        dict(kind=P.IF_AUTO,    up=True,  name="Home Wi-Fi",         bitrate=10000000, rxb=int(90000 * k), txb=int(60000 * k)),
    ]
    return P.pack_interfaces(ifs, paths=31 + int(9 * k), dests=58 + int(14 * k), lxmf_queue=2)

ev(0, f"H {P.pack_time(UNIX0, -480).hex()}")
for t in range(0, T_END, 10000): ev(t, f"H {host_frame(t).hex()}")

# channel noise once a second, an interference burst at ~22 min
for s in range(0, T_END // 1000):
    intf = 1 if 1688 <= s <= 1700 else 0
    noise = -108 + random.randint(-2, 2) + (12 if intf else 0)
    live = noise + (25 if intf and s % 3 == 0 else 0)
    ev(s * 1000 + 500, f"C {noise} {live} {intf}")

# traffic
t = 2000
while t < T_END:
    r = random.random()
    p = random.choice(peers)
    jitter = lambda base: base + random.randint(-4, 4)
    snr = lambda rs: round(max(-12.0, min(11.0, (rs + 112) / 2.5 + random.uniform(-1.5, 1.5))), 1)
    if r < 0.30:
        rs = jitter(p.rssi); ev(t, f"P 0 {rs} {snr(rs)} {p.announce_raw().hex()}")
    elif r < 0.55:
        rs = jitter(p.rssi); ev(t, f"P 0 {rs} {snr(rs)} {p.data_to_raw(random.randint(40, 300)).hex()}")
    elif r < 0.65:
        rs = jitter(p.rssi); ev(t, f"P 0 {rs} {snr(rs)} {link_request_raw(p).hex()}")
        ev(t + 1800, f"P 0 {rs} {snr(rs)} {proof_raw().hex()}")
    elif r < 0.72:
        rs = jitter(-99); ev(t, f"P 0 {rs} {snr(rs)} {path_request_raw(random.choice(peers).dest.hash).hex()}")
    elif r < 0.85:
        ev(t, f"P 1 0 0 {random.choice(peers).data_to_raw(random.randint(60, 240)).hex()}")
    else:
        ev(t, f"P 1 0 0 {me.announce_raw().hex()}")
    t += random.randint(6000, 26000) if t < 28 * 60000 else random.randint(2500, 7000)

# GPS: parked in Fair Oaks, then a drive east through Orangevale into Folsom
for s in range(0, T_END // 1000, 2):
    if s < 1500: lat, lon = 38.6450, -121.2700
    else:
        f = min(1.0, (s - 1500) / 330.0)
        lat = 38.6450 + 0.020 * f; lon = -121.2700 + 0.110 * f
    ev(s * 1000, f"G {int(lat * 1e6)} {int(lon * 1e6)} {9 + (s // 60) % 3} {UNIX0 + s}")

lines.sort(key=lambda x: x[0])
with open(OUT, "w") as f:
    f.write(f"# radio 914875000 125000 9 5 22\n")
    for tt, s in lines: f.write(f"{tt} {s}\n")
print(f"{len(lines)} events, {sum(1 for _, s in lines if s.startswith('P'))} packets -> {OUT}")

os._exit(0)
