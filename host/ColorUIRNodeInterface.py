# ColorUIRNodeInterface -- a Reticulum external interface for the Cardputer Zero
# (or any rnsd host). It IS a normal RNodeInterface (same config keys, same
# radio behavior) that also sends a small stats frame to a ColorUI RNode every
# 10 s, so the Heltec's screen can list EVERY interface rnsd is running:
# LoRa, VHF/UHF and HF KISS TNCs, TCP backbones, Wi-Fi.
#
# Install:   copy this file to ~/.reticulum/interfaces/ColorUIRNodeInterface.py
# Configure (in ~/.reticulum/config, replacing your RNodeInterface section):
#
#   [[Heltec ColorUI]]
#     type = ColorUIRNodeInterface
#     enabled = yes
#     port = /dev/ttyACM0
#     frequency = 914875000
#     bandwidth = 125000
#     txpower = 22
#     spreadingfactor = 9
#     codingrate = 5
#     # optional:
#     colorui_hf = HF Direwolf          # comma-separated interface names that are HF
#     colorui_tz = -480                 # minutes from UTC for the clock
#     colorui_gpsd = yes                # forward the host's gpsd fix (if the Heltec has none)
#
# A stock RNode simply ignores the 0xA0 frames, so this is safe on any RNode.
# STATUS: written against RNS 1.6.x; not yet run against hardware.
import threading, time, struct, socket, json

from RNS.Interfaces.RNodeInterface import RNodeInterface

CMD_UI_HOST = 0xA0
FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD
IF_LORA, IF_KISS_VU, IF_KISS_HF, IF_TCP, IF_AUTO, IF_OTHER = range(6)


def _kiss(payload):
    out = bytearray([FEND, CMD_UI_HOST])
    for c in payload:
        if c == FEND:   out += bytes([FESC, TFEND])
        elif c == FESC: out += bytes([FESC, TFESC])
        else:           out.append(c)
    out.append(FEND)
    return bytes(out)


class ColorUIRNodeInterface(RNodeInterface):
    def __init__(self, owner, configuration):
        c = configuration
        self._cui_hf = set(x.strip() for x in str(c.get("colorui_hf", "")).split(",") if x.strip())
        self._cui_tz = int(c.get("colorui_tz", -480))
        self._cui_gpsd = str(c.get("colorui_gpsd", "no")).lower() in ("yes", "true", "1")
        # Your own destination hashes (e.g. your LXMF address from Sideband or
        # NomadNet), comma separated. The RNode then pops "Message for you"
        # when a packet addressed to one of them arrives. Contents stay
        # encrypted; only the address on the outside is compared.
        self._cui_mine = []
        for h in str(c.get("colorui_my_destinations", "")).replace("<", "").replace(">", "").split(","):
            h = h.strip()
            if len(h) == 32:
                try: self._cui_mine.append(bytes.fromhex(h))
                except ValueError: pass
        super().__init__(owner, configuration)
        threading.Thread(target=self._cui_loop, daemon=True).start()

    def _kind(self, iface):
        cls = type(iface).__name__
        name = str(getattr(iface, "name", ""))
        if "RNode" in cls:                    return IF_LORA
        if cls in ("KISSInterface", "AX25KISSInterface") or getattr(iface, "kiss_framing", False):
            return IF_KISS_HF if name in self._cui_hf else IF_KISS_VU
        if "TCP" in cls or "I2P" in cls:      return IF_TCP
        if "Auto" in cls or "UDP" in cls:     return IF_AUTO
        return IF_OTHER

    def _interfaces_frame(self):
        import RNS
        ifaces = [i for i in RNS.Transport.interfaces if not getattr(i, "parent_interface", None)][:6]
        b = bytearray(struct.pack("<BBHHHB", 1, 1,
                                  min(len(RNS.Transport.path_table), 65535),
                                  min(len(RNS.Identity.known_destinations), 65535),
                                  0, len(ifaces)))
        for i in ifaces:
            name = str(getattr(i, "name", type(i).__name__)).encode("utf-8")[:19]
            b += struct.pack("<BBB", self._kind(i), 1 if getattr(i, "online", False) else 0, len(name)) + name
            b += struct.pack("<III", int(getattr(i, "bitrate", 0) or 0) & 0xFFFFFFFF,
                             int(getattr(i, "rxb", 0)) & 0xFFFFFFFF, int(getattr(i, "txb", 0)) & 0xFFFFFFFF)
        return bytes(b)

    def _gpsd_fix(self):
        try:
            s = socket.create_connection(("127.0.0.1", 2947), timeout=2)
            s.sendall(b'?WATCH={"enable":true,"json":true}\n')
            buf, end = b"", time.time() + 3
            while time.time() < end:
                buf += s.recv(4096)
                for line in buf.split(b"\n"):
                    try: m = json.loads(line)
                    except Exception: continue
                    if m.get("class") == "TPV" and m.get("mode", 0) >= 2 and "lat" in m:
                        s.close()
                        return m["lat"], m["lon"]
            s.close()
        except Exception:
            pass
        return None

    def _send(self, payload):
        try:
            if self.online and self.serial:
                self.serial.write(_kiss(payload))
        except Exception:
            pass

    def _cui_loop(self):
        time.sleep(5)
        n = 0
        while True:
            if n % 6 == 0:                          # every minute: clock + my addresses
                self._send(struct.pack("<BBIh", 1, 3, int(time.time()), self._cui_tz))
                if self._cui_mine:
                    self._send(bytes([1, 5, len(self._cui_mine[:4])]) + b"".join(self._cui_mine[:4]))
            self._send(self._interfaces_frame())
            if self._cui_gpsd and n % 3 == 0:
                fix = self._gpsd_fix()
                if fix:
                    self._send(struct.pack("<BBiiBI", 1, 2, int(fix[0] * 1e6), int(fix[1] * 1e6), 0, int(time.time())))
            n += 1
            time.sleep(10)


    def colorui_event(self, kind, text=""):
        """Pop a notification on the RNode screen from host code, e.g.
        iface.colorui_event(2, "from Alice") for a message card."""
        t = str(text).encode("utf-8")[:27]
        self._send(bytes([1, 4, int(kind) & 0xFF, len(t)]) + t)


interface_class = ColorUIRNodeInterface
