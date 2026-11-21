# Changelog

## 0.8
- Page tabs: Heard (Packets / Nodes), Signal (Live / History), Area (Map / Sky), Radios (Interfaces / Network)
- Nodes list with sort and filter chips; station details; signal tracker with pitch beeps
- Packet inspector (clear-text header fields)
- Quick panel (pull down): sounds, privacy, screen off, theme, brightness; saved to settings.ini
- Watch list (`!` in names.txt) with "In range!" notes
- GPS sky view (GSV), network view (relays and their stations), 24 h / 7 day history saved to SD
- Announce button (asks the ColorUI host plugin to announce; plugin handles KISS 0xA1)
- Firmware update over Wi-Fi from the web page (two 6 MB app slots; self-registered hash)
- Touch default matches the R8 + Expansion Kit V2 as measured; taps use the release point

## 0.7
- Modern UI: Inter fonts, Tabler icons, rounded cards, status bar, tap-to-jump icon rail,
  new Home page; themes night / soft / amber / retro (0.6 look kept)
- "You were heard!" when another node repeats your announce
- Coverage map (GPS + signal per packet) on the Area page, over offline map tiles
- QR code of your LXMF address on the Status page
- Connection type (USB / Wi-Fi with IP / Bluetooth) in the status bar
- Clock screensaver with backlight dimming; airtime warning at 8 %
- SD card: settings.ini, names.txt, /splash/*.bmp, /tiles map, /logs CSV; card stays
  mounted on the shared SPI3 bus (sd_live = off restores read-at-boot)
- Wi-Fi file manager for the SD card (browser page on port 80, rnode-r8.local, optional password)
- 6 MB app partition (16 MB layout); settings area unchanged

## 0.6 (first public source)
- 12 rotating boot splashes (6 comics, 6 RF-physics scenes with real LoRa chirp patterns)
- Graphic notifications: message for you, welcome to <city>, new node, host link,
  GPS, interference, low battery (cards + toasts, priorities and cooldowns)
- Host protocol frames 4 (event) and 5 (my destinations)
- Splash title set at build time with `COLORUI_BRAND`
- `rnode_sethash.py`: registers the firmware hash without the ESP32-S3 port-close reset
- GitHub Actions build + release

## 0.5
- LCD moved to SPI3 (fixes the task-watchdog reset from sharing SPI2 with the radio)

## 0.4
- In-place state reset (fixes the 0.3 stack-canary boot loop)

## 0.2-0.3
- V4-R8 board definition with PSRAM-safe pins; first compile
