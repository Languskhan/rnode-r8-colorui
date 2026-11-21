"""Set the RNode's firmware hash and KEEP THE PORT OPEN while it saves.

On the ESP32-S3's native USB, closing the port resets the board. rnodeconf
closes it right after sending the hash, so the board only manages to save a
byte or two of the 32. This sends the hash, waits until the board has
finished saving (it restarts by itself when done), then reads it back.

Usage: py rnode_sethash.py COM5 <64-hex-char hash>
"""
import sys, time, threading
import RNS
import RNS.Utilities.rnodeconf as rc

port, h = sys.argv[1], sys.argv[2].strip().lower()
target = bytes.fromhex(h)
assert len(target) == 32, "hash must be 64 hex characters"
RNS.loglevel = RNS.LOG_WARNING

def connect():
    ser = rc.rnode_open_serial(port)
    rn = rc.RNode(ser)
    threading.Thread(target=rn.readLoop, daemon=True).start()
    time.sleep(1.0)
    rn.detect()
    time.sleep(1.5)
    return ser, rn

def close(ser):
    try: ser.close()
    except Exception: pass

ser, rn = connect()
print("before :", rn.firmware_hash_target.hex() if rn.firmware_hash_target else "(no reply)")
if rn.firmware_hash_target == target:
    print("Already set. Nothing to do.")
    close(ser); sys.exit(0)

rn.set_firmware_hash(target)
print("Sent. Holding the port open while the board saves (up to 60 s)...")
t0 = time.time()
while time.time() - t0 < 60:
    time.sleep(0.5)
    if not ser.is_open:
        break
    try:
        _ = ser.in_waiting          # raises once the board restarts and the port drops
    except Exception:
        print("Board restarted after %.1f s (that means it finished saving)." % (time.time() - t0))
        break
close(ser)

print("Waiting for the board to come back...")
time.sleep(4)
for _ in range(10):
    try:
        ser, rn = connect()
        if rn.firmware_hash_target is None:
            close(ser); time.sleep(3); continue
        break
    except Exception:
        time.sleep(2)
else:
    print("Couldn't reopen the port. Press RST and run this again."); sys.exit(1)

got = rn.firmware_hash_target
print("after  :", got.hex() if got else "(no reply)")
print("actual :", rn.firmware_hash.hex() if rn.firmware_hash else "(no reply)")
print("RESULT :", "OK - hash saved and matches" if got == target and rn.firmware_hash == target
      else "not complete yet - run this same command again (each run continues where it stopped)")
close(ser)
