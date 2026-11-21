# Changelog

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
