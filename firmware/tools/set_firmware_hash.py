# After upload: tell the RNode the hash of the firmware we just flashed, so it
# doesn't say "Firmware Corrupt" / "Radio state mismatch". Needs Reticulum
# (py -m pip install rns) for the serial step; the hash itself is computed here.
# Works on Windows (py launcher) and Linux/macOS (python3).
Import("env")
import hashlib, os, shutil, subprocess, time

def _py():
    if os.name == "nt" and shutil.which("py"): return ["py"]
    return ["python3"]

def set_hash(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    fw = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    data = open(fw, "rb").read()
    h = data[-32:].hex()
    if hashlib.sha256(data[:-32]).hexdigest() != h:
        print("ColorUI: firmware.bin has no appended SHA-256; can't register the hash.")
        return
    print("ColorUI: firmware hash " + h)
    if not port:
        print("ColorUI: no upload port known. Register it by hand:  py rnode_sethash.py <PORT> " + h)
        return
    time.sleep(4)
    script = os.path.join(env.subst("$PROJECT_DIR"), "tools", "rnode_sethash.py")
    r = subprocess.run(_py() + [script, port, h])
    if r.returncode != 0:
        print("ColorUI: hash step failed. Run:  py rnode_sethash.py " + port + " " + h)

env.AddPostAction("upload", set_hash)
