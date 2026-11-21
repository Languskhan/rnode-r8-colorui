#!/bin/sh
# Usage: tools/finish_setup.sh <serial port> [--first-time]
#   port: Linux /dev/ttyACM0, macOS /dev/cu.usbmodemXXXX, Windows COM5
#   --first-time: give a blank board its V4-R8 identity (never use --autoinstall on an R8)
# Always: registers the hash of the firmware you just flashed.
set -e
PORT="$1"
[ -z "$PORT" ] && { echo "usage: $0 <serial port> [--first-time]"; exit 1; }
if [ "$2" = "--first-time" ]; then
  rnodeconf --key || true
  rnodeconf "$PORT" --rom --product cb --model c8 --hwrev 1
fi
# rnode_sethash.py keeps the port open while the board saves (rnodeconf --firmware-hash
# closes it too early on the ESP32-S3 and the hash ends up as ff ff ff ...).
python3 tools/rnode_sethash.py "$PORT" "$(python3 tools/partition_hashes .pio/build/heltec_v4_r8_colorui/firmware.bin)"
rnodeconf "$PORT" --info
