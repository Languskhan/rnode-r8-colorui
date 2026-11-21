#!/usr/bin/env python3
"""Apply RNode ColorUI to an upstream RNode_Firmware checkout (tested against 1.86).

    git clone https://github.com/markqvist/RNode_Firmware
    python3 rnode-colorui/device/apply_colorui.py RNode_Firmware

What it does (every edit is checked; it stops if upstream code moved):
  1. Adds the Heltec WiFi LoRa 32 V4-R8 as its own board (BOARD 0xF8, PRODUCT
     0xCB, MODEL 0xC8 -- the same IDs the community fork deuxyeux/RNode_Firmware
     uses, so the two firmwares agree). The R8 shares the V4's radio wiring but
     GPIO33-37 are its octal PSRAM, so it gets its own peripheral pins:
     Vext 40, LED 46, KCT8103L front end, LNA gain 21, battery on GPIO1 with no
     control pin (upstream V4 drives GPIO35/36/37, which would hit the PSRAM).
  2. Turns off upstream's OLED code on ColorUI builds (we drive the TFT).
  3. Adds the six ColorUI hooks (RX, TX, host frame, channel, radio, battery).
  4. Copies core/ and device/ into src/colorui/ and adds Makefile targets.

R8 pin values: community fork docs (Documentation/HELTEC_V4_R8.md) and the
WADAMESH V4-R8 build; both GPLv3, credited in README.
"""
import os, re, shutil, sys

root = sys.argv[1] if len(sys.argv) > 1 else "RNode_Firmware"
here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(here)


def edit(path, old, new, count=1, regex=False):
    p = os.path.join(root, path)
    s = open(p).read()
    if regex:
        s2, n = re.subn(old, new, s)
    else:
        n = s.count(old)
        s2 = s.replace(old, new)
    if n < count:
        sys.exit(f"apply_colorui: expected text not found in {path}:\n  {old[:90]!r}\n(upstream changed? edit by hand)")
    open(p, "w").write(s2)
    return n


# ---- 1. board, product and the R8 variant ---------------------------------
edit("Boards.h", "  #define BOARD_HELTEC32_V4   0x3F\n",
     "  #define BOARD_HELTEC32_V4   0x3F\n"
     "  #define PRODUCT_H32_V4_R8   0xCB  // ColorUI / community fork: Heltec V4-R8 (octal PSRAM)\n"
     "  #define BOARD_HELTEC32_V4_R8 0xF8\n")

# every V4 code path also applies to the R8 ...
n = 0
for f in sorted(os.listdir(root)):
    p = os.path.join(root, f)
    if not f.endswith((".h", ".cpp", ".ino")) or not os.path.isfile(p):
        continue
    s = open(p).read()
    s, k = re.subn(r"BOARD_MODEL\s*==\s*BOARD_HELTEC32_V4\b(?!_R8)",
                   "(BOARD_MODEL == BOARD_HELTEC32_V4 || BOARD_MODEL == BOARD_HELTEC32_V4_R8)", s)
    # "not a V4" checks must also exclude the R8 (e.g. the boot-time wait for a
    # USB host, which would otherwise hang the screen until a computer connects)
    s, k2 = re.subn(r"BOARD_MODEL\s*!=\s*BOARD_HELTEC32_V4\b(?!_R8)",
                    "(BOARD_MODEL != BOARD_HELTEC32_V4 && BOARD_MODEL != BOARD_HELTEC32_V4_R8)", s)
    k += k2
    open(p, "w").write(s)
    n += k
if n < 5:
    sys.exit("apply_colorui: fewer V4 code paths than expected")

# ... except the pins that collide with the R8's PSRAM
edit("Boards.h", "      #define Vext GPIO_NUM_36\n      #define LORA_PA_MODEL LORA_PA_UNKNOWN;\n",
     "      #if BOARD_MODEL == BOARD_HELTEC32_V4_R8\n"
     "        #define Vext GPIO_NUM_40                 // R8: GPIO36 is PSRAM\n"
     "        #define LORA_PA_MODEL LORA_PA_KCT8103L;\n"
     "      #else\n"
     "        #define Vext GPIO_NUM_36\n"
     "        #define LORA_PA_MODEL LORA_PA_UNKNOWN;\n"
     "      #endif\n")
edit("Boards.h", "        const int pin_led_rx = 35;\n        const int pin_led_tx = 35;\n      #endif\n\n      #define MODEM SX1262\n      #define HAS_TCXO true\n      const int pin_tcxo_enable = -1;\n      #define HAS_BUSY true\n      #define DIO2_AS_RF_SWITCH true\n      #define LNA_GD_THRSHLD (-109)\n      #define LNA_GD_LIMIT   (-89)\n\n      #define LORA_LNA_GAIN  17\n",
     "        #if BOARD_MODEL == BOARD_HELTEC32_V4_R8\n"
     "          const int pin_led_rx = 46;             // R8: GPIO35 is PSRAM\n"
     "          const int pin_led_tx = 46;\n"
     "        #else\n"
     "          const int pin_led_rx = 35;\n"
     "          const int pin_led_tx = 35;\n"
     "        #endif\n      #endif\n\n      #define MODEM SX1262\n      #define HAS_TCXO true\n      const int pin_tcxo_enable = -1;\n      #define HAS_BUSY true\n      #define DIO2_AS_RF_SWITCH true\n      #define LNA_GD_THRSHLD (-109)\n      #define LNA_GD_LIMIT   (-89)\n\n"
     "      #if BOARD_MODEL == BOARD_HELTEC32_V4_R8\n"
     "        #define LORA_LNA_GAIN  21\n"
     "      #else\n"
     "        #define LORA_LNA_GAIN  17\n"
     "      #endif\n")
edit("Boards.h", "      #define HAS_DISPLAY true\n      #define HAS_BLUETOOTH false\n      #define HAS_BLE true\n      #define HAS_WIFI true\n      #define HAS_PMU true\n",
     "      #if defined(COLORUI)\n"
     "        #define HAS_DISPLAY false                // ColorUI drives the expansion TFT itself\n"
     "      #else\n"
     "        #define HAS_DISPLAY true\n"
     "      #endif\n"
     "      #define HAS_BLUETOOTH false\n      #define HAS_BLE true\n      #define HAS_WIFI true\n      #define HAS_PMU true\n")

# battery: R8 measures on GPIO1 with no control pin (GPIO37 is PSRAM)
edit("Power.h", "  const uint8_t pin_vbat = 1;\n  const uint8_t pin_ctrl = 37;\n",
     "  const uint8_t pin_vbat = 1;\n"
     "  #if BOARD_MODEL == BOARD_HELTEC32_V4_R8\n"
     "    const uint8_t pin_ctrl = 0xFF;                // none: GPIO37 is PSRAM on the R8\n"
     "  #else\n"
     "    const uint8_t pin_ctrl = 37;\n"
     "  #endif\n")
edit("Power.h", "      float battery_measurement = (float)(analogRead(pin_vbat)) * 0.00418;\n",
     "      #if BOARD_MODEL == BOARD_HELTEC32_V4_R8\n"
     "        // R8: 2.5 dB attenuation, divider 4.9 x 1.035 (community fork calibration)\n"
     "        analogSetPinAttenuation(pin_vbat, ADC_2_5db);\n"
     "        float battery_measurement = (float)analogReadMilliVolts(pin_vbat) / 1000.0 * 4.9 * 1.035;\n"
     "      #else\n"
     "        float battery_measurement = (float)(analogRead(pin_vbat)) * 0.00418;\n"
     "      #endif\n")
# the V4 battery-enable path must not drive a PSRAM pin on the R8
edit("Power.h", "  #elif (BOARD_MODEL == BOARD_HELTEC32_V4 || BOARD_MODEL == BOARD_HELTEC32_V4_R8)\n    pinMode(pin_ctrl,OUTPUT);\n    digitalWrite(pin_ctrl, HIGH);\n    return true;",
     "  #elif (BOARD_MODEL == BOARD_HELTEC32_V4 || BOARD_MODEL == BOARD_HELTEC32_V4_R8)\n    if (pin_ctrl != 0xFF) { pinMode(pin_ctrl,OUTPUT); digitalWrite(pin_ctrl, HIGH); }\n    return true;")

# product IDs the firmware accepts in its EEPROM
edit("Utilities.h", "rval == PRODUCT_H32_V4 ||", "rval == PRODUCT_H32_V4 || rval == PRODUCT_H32_V4_R8 ||")

# ---- 3. ColorUI hooks in the main sketch ----------------------------------
ino = "RNode_Firmware.ino"
edit(ino, "void setup() {",
     "#if defined(COLORUI)\n"
     "  #include \"src/colorui/device/colorui_hooks.h\"\n"
     "  uint8_t  colorui_buf[256];\n"
     "  uint16_t colorui_len = 0;\n"
     "  #if BOARD_MODEL == BOARD_HELTEC32_V4_R8\n"
     "    static_assert(pin_led_rx < 33 || pin_led_rx > 37, \"R8: GPIO33-37 are octal PSRAM\");\n"
     "  #endif\n"
     "#endif\n\nvoid setup() {")
s = open(os.path.join(root, ino)).read()
i = s.index("void setup() {"); j = s.index("\nvoid lora_receive()", i); k = s.rindex("\n}", i, j)
s = s[:k] + "\n\n  #if defined(COLORUI)\n    colorui_begin();\n  #endif" + s[k:]
open(os.path.join(root, ino), "w").write(s)
edit(ino, "        kiss_indicate_stat_rssi();\n        kiss_indicate_stat_snr();\n        kiss_write_packet();\n      }\n\n      airtime_lock = false;",
     "        kiss_indicate_stat_rssi();\n        kiss_indicate_stat_snr();\n        kiss_write_packet();\n"
     "        #if defined(COLORUI)\n          colorui_on_rx(pbuf, host_write_len, last_rssi, (float)last_snr_raw * 0.25f);\n        #endif\n"
     "      }\n\n      airtime_lock = false;")
edit(ino, "void transmit(uint16_t size) {\n  if (radio_online) {",
     "void transmit(uint16_t size) {\n  #if defined(COLORUI)\n    if (radio_online) colorui_on_tx(tbuf, size);\n  #endif\n  if (radio_online) {")
edit(ino, "  } else if (sbyte == FEND) {\n    IN_FRAME = true;\n    command = CMD_UNKNOWN;\n    frame_len = 0;",
     "  #if defined(COLORUI)\n  } else if (IN_FRAME && sbyte == FEND && command == CMD_UI_HOST) {\n"
     "    colorui_on_host_cmd(colorui_buf, colorui_len);\n    colorui_on_host_seen();\n"
     "    IN_FRAME = true;              // this FEND may also open the next frame\n"
     "    command = CMD_UNKNOWN;\n    frame_len = 0;\n  #endif\n"
     "  } else if (sbyte == FEND) {\n    IN_FRAME = true;\n    command = CMD_UNKNOWN;\n    frame_len = 0;")
edit(ino, "    if (frame_len == 0 && command == CMD_UNKNOWN) {\n        command = sbyte;",
     "    if (frame_len == 0 && command == CMD_UNKNOWN) {\n        command = sbyte;\n"
     "        #if defined(COLORUI)\n          if (command == CMD_UI_HOST) colorui_len = 0;\n          colorui_on_host_seen();\n        #endif\n"
     "    #if defined(COLORUI)\n    } else if (command == CMD_UI_HOST) {\n"
     "        if (sbyte == FESC) { ESCAPE = true; }\n        else {\n"
     "          if (ESCAPE) { if (sbyte == TFEND) sbyte = FEND; if (sbyte == TFESC) sbyte = FESC; ESCAPE = false; }\n"
     "          if (colorui_len < sizeof(colorui_buf)) colorui_buf[colorui_len++] = sbyte;\n        }\n    #endif")
edit(ino, "    tx_queue_handler();",
     "    tx_queue_handler();\n\n"
     "    #if defined(COLORUI)\n"
     "      colorui_on_channel(noise_floor, current_rssi, interference_detected);\n"
     "      {\n"
     "        uint8_t colorui_out[24];\n"
     "        const size_t colorui_out_n = colorui_take_host_out(colorui_out, sizeof colorui_out);\n"
     "        if (colorui_out_n) {\n"
     "          serial_write(FEND); serial_write(CMD_UI_DEVICE);\n"
     "          for (size_t i = 0; i < colorui_out_n; i++) escaped_serial_write(colorui_out[i]);\n"
     "          serial_write(FEND);\n"
     "        }\n"
     "      }\n"
     "      static uint32_t colorui_slow_ms = 0;\n"
     "      if (millis() - colorui_slow_ms > 2000) {\n"
     "        colorui_slow_ms = millis();\n"
     "        colorui_set_radio(lora_freq, lora_bw, lora_sf, lora_cr, lora_txp);\n"
     "        {\n"
     "          uint8_t colorui_lk = 1; const char* colorui_ipp = nullptr;\n"
     "          #if HAS_WIFI\n"
     "            static char colorui_ip[16];\n"
     "            if (wifi_is_connected()) { snprintf(colorui_ip, sizeof colorui_ip, \"%s\", wr_device_ip.toString().c_str()); colorui_ipp = colorui_ip; }\n"
     "            if (wr_state == WR_STATE_CONNECTED) colorui_lk = 2;\n"
     "          #endif\n"
     "          #if HAS_BLUETOOTH || HAS_BLE == true\n"
     "            if (bt_state == BT_STATE_CONNECTED) colorui_lk = 3;\n"
     "          #endif\n"
     "          colorui_set_link(colorui_lk, colorui_ipp);\n"
     "        }\n"
     "        #if HAS_PMU\n          colorui_set_battery(battery_ready ? (int)battery_percent : -1);\n        #endif\n"
     "      }\n"
     "    #endif")

# ---- 3b. PlatformIO's .ino converter misses this indented definition ----
s_ino = open(os.path.join(root, ino)).read()
if "void update_csma_parameters();" not in s_ino:
    s_ino = s_ino.replace('#include "Utilities.h"\n', '#include "Utilities.h"\n\n// Forward declaration: PlatformIO\'s .ino converter skips this indented definition.\n#if MCU_VARIANT == MCU_ESP32 || MCU_VARIANT == MCU_NRF52\n  void update_csma_parameters();\n#endif\n', 1)
    open(os.path.join(root, ino), "w").write(s_ino)

# ---- 3c. Wi-Fi updater: register the new image's hash ----
s_ino = open(os.path.join(root, ino)).read()
if "colorui_store_fw_hash" not in s_ino.split("colorui_hooks.h")[-1]:
    s_ino = s_ino.rstrip() + "\n" + "\n#if defined(COLORUI)\n  // Called by the ColorUI Wi-Fi updater after it writes a new app image:\n  // registers that image's hash so the RNode accepts it after the restart.\n  void colorui_store_fw_hash(const uint8_t* hash32) {\n    memcpy(dev_firmware_hash_target, hash32, DEV_HASH_LEN);\n    device_save_firmware_hash();\n  }\n#endif\n"
    open(os.path.join(root, ino), "w").write(s_ino)

# ---- 4. sources and build targets ------------------------------------------
dst = os.path.join(root, "src", "colorui")
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(os.path.join(repo, "core"), os.path.join(dst, "core"))
os.makedirs(os.path.join(dst, "device"))
for f in ("colorui_hooks.h", "colorui_device.cpp", "colorui_demo.h", "colorui_web.h", "colorui_web.cpp", "colorui_web_page.h"):
    shutil.copy(os.path.join(here, f), os.path.join(dst, "device", f))

mk = os.path.join(root, "Makefile")
open(mk, "a").write('''
# ---- RNode ColorUI: Heltec V4-R8 + Expansion Kit V2 ----
# needs: arduino-cli lib install LovyanGFX ; chsc6x (github.com/Quency-D/chsc6x) in your libraries folder
firmware-heltec32_v4_r8_colorui:
\tarduino-cli compile --log --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M" -e --build-property "build.partitions=no_ota" --build-property "upload.maximum_size=2097152" --build-property "compiler.cpp.extra_flags=\\"-DBOARD_MODEL=0xF8\\" \\"-DCOLORUI\\""

upload-heltec32_v4_r8_colorui:
\tarduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3
\t@sleep 1
\trnodeconf /dev/ttyACM0 --firmware-hash $$(./partition_hashes ./build/esp32.esp32.esp32s3/RNode_Firmware.ino.bin)
''')
print("ColorUI applied to", root)
print("next: make firmware-heltec32_v4_r8_colorui && make upload-heltec32_v4_r8_colorui")
print("first time on a blank board: rnodeconf --key ; rnodeconf /dev/ttyACM0 --rom --product cb --model c8 --hwrev 1")
