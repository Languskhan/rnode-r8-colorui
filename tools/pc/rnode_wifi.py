"""Put an ESP32-S3 RNode on your Wi-Fi (station mode) and check it saved.

Same reason as rnode_sethash.py: the board saves settings one byte at a time,
and on the S3's native USB, closing the port resets the board, so a plain
`rnodeconf --ssid ... --psk ...` can leave them half-written. This sends the
SSID, password and mode, keeps the port open while the board saves, then reads
the settings back before closing. The password is never printed.

Close MeshChatX / rnsd first (they hold the port).

Usage: py rnode_wifi.py COM5 "WiFi name" "WiFi password"
       py rnode_wifi.py COM5 --off          (turn Wi-Fi off again)
"""
import sys, time, threading
import RNS
import RNS.Utilities.rnodeconf as rc

ROM = rc.ROM
port = sys.argv[1]
off = len(sys.argv) > 2 and sys.argv[2] == "--off"
if not off:
    ssid, psk = sys.argv[2], sys.argv[3]
    if not (1 <= len(ssid.encode()) <= 32): sys.exit("SSID must be 1-32 bytes")
    if not (8 <= len(psk.encode()) <= 32): sys.exit("Password must be 8-32 bytes for this RNode")
RNS.loglevel = RNS.LOG_WARNING

ser = rc.rnode_open_serial(port)
rn = rc.RNode(ser)
threading.Thread(target=rn.readLoop, daemon=True).start()
time.sleep(1.0)
rn.detect()
time.sleep(1.5)
if not rn.detected: sys.exit("No RNode answered on " + port + " (is MeshChatX still running?)")

if off:
    rn.set_wifi_mode(0x00); print("Wi-Fi off sent.")
else:
    rn.set_wifi_ssid(ssid); time.sleep(3)
    rn.set_wifi_psk(psk);   time.sleep(3)
    rn.set_wifi_mode(0x01)  # station
    print("Sent. Holding the port open while the board saves...")
time.sleep(8)

rn.download_cfg_sector(); time.sleep(1.5)
rn.download_eeprom();     time.sleep(1.5)

def cstr(base):
    b = b""
    for i in range(32):
        x = rn.cfg_sector[base + i]
        if x in (0x00, 0xFF): break
        b += bytes([x])
    return b.decode("utf-8", "replace")

mode = rn.eeprom[ROM.ADDR_CONF_WIFI] if rn.eeprom else None
print("Wi-Fi mode :", {0: "off", 1: "station", 2: "AP"}.get(mode, mode))
if not off and rn.cfg_sector:
    got_ssid, got_psk = cstr(ROM.ADDR_CONF_SSID), cstr(ROM.ADDR_CONF_PSK)
    print("SSID       :", got_ssid)
    print("Password   :", "matches" if got_psk == psk else "DOES NOT MATCH")
    ok = got_ssid == ssid and got_psk == psk and mode == 1
    print("RESULT     :", "OK - saved. It will join Wi-Fi now and after every reboot."
          if ok else "not complete - run the same command again")
try: ser.close()
except Exception: pass
