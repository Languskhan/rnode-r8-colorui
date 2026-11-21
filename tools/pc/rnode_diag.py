"""Read-only RNode diagnostic: asks the board for its firmware hashes and
device hash, and tries to switch the radio on, then prints what it reports.
Writes nothing to the board's EEPROM or flash.
Usage: py rnode_diag.py COM5
"""
import sys, time, threading
import RNS
import RNS.Utilities.rnodeconf as rc

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
RNS.loglevel = RNS.LOG_WARNING
ser = rc.rnode_open_serial(port)
rnode = rc.RNode(ser)
threading.Thread(target=rnode.readLoop, daemon=True).start()
time.sleep(1.0)
rnode.detect()
time.sleep(1.5)

def hx(b):
    return b.hex() if b else "(no reply)"

print("detected          :", rnode.detected)
print("firmware version  :", getattr(rnode, "version", None))
print("target fw hash    :", hx(rnode.firmware_hash_target))
print("actual fw hash    :", hx(rnode.firmware_hash))
match = rnode.firmware_hash_target and rnode.firmware_hash and rnode.firmware_hash_target == rnode.firmware_hash
print("hashes match      :", bool(match))
dh = rnode.device_hash
print("device hash       :", hx(dh))
print("bluetooth started :", "no (device hash is all zeros)" if dh and not any(dh) else ("yes" if dh else "unknown"))

# Try to bring the radio up with the same settings as the Reticulum config
rnode.frequency, rnode.bandwidth, rnode.txpower, rnode.sf, rnode.cr = 914875000, 125000, 17, 9, 5
rnode.state = 0x01
try:
    rnode.initRadio()
except Exception as e:
    print("initRadio error   :", e)
time.sleep(2.0)
print("radio state       :", {0: "OFF", 1: "ON"}.get(rnode.r_state, rnode.r_state))
print("radio lock        :", getattr(rnode, "r_lock", None))
print("reported freq/bw  :", rnode.r_frequency, rnode.r_bandwidth, "sf", rnode.r_sf, "txp", rnode.r_txpower)
try:
    rnode.disconnect()
except Exception:
    pass
