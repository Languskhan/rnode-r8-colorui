"""Capture serial output from the board for a fixed time, reconnecting
whenever the USB port drops (it does on every reboot of an ESP32-S3).
Read-only: it never writes to the board.
Usage: py capture_serial.py COM5 [seconds]
"""
import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 25
end = time.time() + secs
connected = False

with open("serial-log.txt", "wb") as log:
    def note(msg):
        line = f"\n[capture {time.strftime('%H:%M:%S')}] {msg}\n"
        print(line, end="", flush=True)
        log.write(line.encode())
        log.flush()

    note(f"listening on {port} at 115200 for {secs:.0f} s")
    while time.time() < end:
        try:
            s = serial.Serial()
            s.port = port
            s.baudrate = 115200
            s.timeout = 0.2
            s.dtr = False   # don't toggle reset lines when opening
            s.rts = False
            s.open()
            if not connected:
                note("port open")
                connected = True
            while time.time() < end:
                data = s.read(4096)
                if data:
                    sys.stdout.write(data.decode("utf-8", "replace"))
                    sys.stdout.flush()
                    log.write(data)
                    log.flush()
            s.close()
        except (serial.SerialException, OSError) as e:
            if connected:
                note(f"port dropped ({e.__class__.__name__}), waiting for it to come back")
                connected = False
            time.sleep(0.2)
    note("done - output saved to serial-log.txt")
