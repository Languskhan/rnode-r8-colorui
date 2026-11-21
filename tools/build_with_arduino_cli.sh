#!/bin/sh
# How the release was built (arduino-cli + esp32 core 2.0.17); paths are for that build machine.
set -e
rm -rf /tmp/build && mkdir -p /tmp/build && cp -r /home/claude/rnode_up /tmp/build/RNode_Firmware && rm -rf /tmp/build/RNode_Firmware/.git
python3 /home/claude/rnode-colorui/device/apply_colorui.py /tmp/build/RNode_Firmware > /dev/null
CT=/tmp/ctags/ctags
cd /tmp/build/RNode_Firmware
/tmp/arduino-cli --config-file /tmp/acli.yaml compile --build-path /tmp/bpath --fqbn "espressif:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M" -e \
 --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" \
 --build-property "runtime.tools.xtensa-esp32s3-elf-gcc.path=/tmp/tc/xtensa-esp32s3-elf" \
 --build-property "runtime.tools.esptool_py.path=/tmp/et/esptool" --build-property "runtime.tools.ctags.path=/tmp/ctags" \
 --build-property "compiler.cpp.extra_flags=-DBOARD_MODEL=0xF8 -DCOLORUI -fstack-usage" > /tmp/compile.log 2>&1
