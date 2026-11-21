# After upload: tell the RNode the hash of the firmware we just flashed, so it
# doesn't show "Firmware Corrupt". Needs Reticulum (pip install rns).
# Works on Windows (py launcher) and Linux/macOS (python3).
Import("env")
import os, shutil, subprocess, time

def _py():
    if os.name == "nt" and shutil.which("py"): return ["py"]
    return ["python3"]

def set_hash(source, target, env):
    port = env.subst("$UPLOAD_PORT")
    if not port:
        print("ColorUI: no upload port known; register the hash by hand (see README.md).")
        return
    time.sleep(3)
    fw = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    h = subprocess.run(_py() + ["tools/partition_hashes", fw], capture_output=True, text=True).stdout.strip()
    if not h:
        print("ColorUI: couldn't hash the firmware (is rns installed? py -m pip install rns)")
        return
    print("ColorUI: firmware hash " + h)
    # Not "rnodeconf --firmware-hash": on the S3's native USB, rnodeconf closes the
    # port right after sending, which resets the board before it saves the hash.
    r = subprocess.run(_py() + ["tools/rnode_sethash.py", port, h])
    if r.returncode != 0:
        print("ColorUI: rnodeconf failed; if the board isn't provisioned yet, run the --rom step first (README.md).")

env.AddPostAction("upload", set_hash)
