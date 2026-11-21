#!/usr/bin/env python3
"""Copy core/ and device/ into firmware/src/colorui/ after you edit them.

firmware/ is a ready-to-build PlatformIO project (RNode 1.86 with ColorUI
applied). Its src/colorui/ folder is a copy of core/ and device/; edit those,
then run:  python3 tools/sync_firmware.py
"""
import os, shutil
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dst = os.path.join(root, "firmware", "src", "colorui")
for sub in ("core", "device"):
    d = os.path.join(dst, sub)
    shutil.rmtree(d, ignore_errors=True)
    shutil.copytree(os.path.join(root, sub), d,
                    ignore=shutil.ignore_patterns("*.py", "*.patch", "__pycache__"))
print("synced core/ and device/ into", dst)
