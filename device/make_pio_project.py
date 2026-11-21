#!/usr/bin/env python3
"""Make a ready-to-build PlatformIO project for the Heltec V4-R8 + Expansion Kit V2.

    python3 device/make_pio_project.py <upstream RNode_Firmware checkout> <out dir>

Result: <out dir>/platformio.ini + src/ (official RNode 1.86 with ColorUI applied).
Then:   cd <out dir> && pio run -e heltec_v4_r8_colorui -t upload
The upload step also registers the firmware hash with rnodeconf if it is installed.
"""
import os, shutil, subprocess, sys

up, out = sys.argv[1], sys.argv[2]
here = os.path.dirname(os.path.abspath(__file__))
work = out + ".work"
shutil.rmtree(work, ignore_errors=True)
shutil.copytree(up, work, ignore=shutil.ignore_patterns(".git"))
subprocess.check_call([sys.executable, os.path.join(here, "apply_colorui.py"), work])

shutil.rmtree(out, ignore_errors=True)
os.makedirs(os.path.join(out, "src"))
for f in os.listdir(work):
    if f.endswith((".ino", ".h", ".cpp", ".c")):
        shutil.copy(os.path.join(work, f), os.path.join(out, "src", f))
shutil.copytree(os.path.join(work, "src", "colorui"), os.path.join(out, "src", "colorui"))
ino = os.path.join(out, "src", "RNode_Firmware.ino")
s = open(ino).read().replace('#include "src/colorui/device/colorui_hooks.h"', '#include "colorui/device/colorui_hooks.h"')
open(ino, "w").write(s)
os.makedirs(os.path.join(out, "tools"))
shutil.copy(os.path.join(work, "partition_hashes"), os.path.join(out, "tools", "partition_hashes"))
shutil.copy(os.path.join(work, "LICENSE"), os.path.join(out, "LICENSE"))
shutil.rmtree(work)

# platformio.ini and the upload helpers come from the repo's firmware/ folder,
# so a regenerated project matches the one that is checked in.
tmpl = os.path.join(os.path.dirname(here), "firmware")
shutil.copy(os.path.join(tmpl, "platformio.ini"), os.path.join(out, "platformio.ini"))
shutil.copy(os.path.join(tmpl, "partitions_16mb.csv"), os.path.join(out, "partitions_16mb.csv"))
for f in ("set_firmware_hash.py", "rnode_sethash.py", "finish_setup.sh"):
    shutil.copy(os.path.join(tmpl, "tools", f), os.path.join(out, "tools", f))
os.chmod(os.path.join(out, "tools", "finish_setup.sh"), 0o755)
print("PlatformIO project ready in", out)
