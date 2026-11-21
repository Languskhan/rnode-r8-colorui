#!/usr/bin/env python3
"""Turn the simulator's PPM output into 2x PNG screenshots and an animated GIF."""
import glob, os, sys
from PIL import Image
out = sys.argv[1] if len(sys.argv) > 1 else "sim/out"
os.makedirs(f"{out}/png", exist_ok=True)
for f in glob.glob(f"{out}/shots/*.ppm"):
    Image.open(f).convert("RGB").resize((640, 480), Image.NEAREST).save(f"{out}/png/{os.path.basename(f)[:-4]}.png")
fs = sorted(glob.glob(f"{out}/frames/*.ppm"))
if fs:
    frames = [Image.open(f).convert("RGB").resize((480, 360), Image.NEAREST)
              .quantize(colors=96, method=Image.MEDIANCUT, dither=Image.Dither.NONE) for f in fs]
    frames[0].save(f"{out}/colorui-sim.gif", save_all=True, append_images=frames[1:], duration=200, loop=0, optimize=True, disposal=1)
print("done:", out)
