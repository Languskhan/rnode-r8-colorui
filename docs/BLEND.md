# ColorUI vs. the community fork, and what we blended

| | deuxyeux/RNode_Firmware | RNode ColorUI |
|---|---|---|
| Base | heavily extended RNode | official RNode 1.86 + `apply_colorui.py` |
| V4-R8 color TFT (Expansion Kit V2) | not supported ("R8 environments support the OLED, not the optional TFT") | the whole point |
| Screen | 4 corner icons, Q/S bars, waterfall strip, airtime, banner carousel | Now Heard over pixel art, Last Heard with names, graphs, channel waterfall, area, interfaces, status |
| On-device Reticulum + LXMF messenger | yes (R8: "not validated") | no -- the Cardputer Zero runs rnsd |
| City locator / trip log | no | yes (WADAMESH CityLocator) |
| Host radios on screen | no | yes (ColorUIRNodeInterface.py) |
| Packet analyzer | LoRaMon-Web in a browser (promiscuous mode) | classification built into the screen |
| Flashing | web flasher sets the hash for you | Makefile target sets the hash |

## Taken from the fork (credited)
- R8 board/product/model IDs 0xF8 / 0xCB / 0xC8 and its provisioning steps.
- R8 peripheral pins off GPIO33-37: Vext 40, LED 46, GNSS EN 42, KCT8103L front end, LNA gain 21.
- R8 battery: GPIO1, 2.5 dB attenuation, divider 4.9 x 1.035.
- Idea: signal bars scaled to the spreading factor.
- Idea: waterfall marks for our own TX and for interference.

## Left out, on purpose
- microReticulum node / LXMF messenger: big, not validated on the R8, and duplicates what rnsd on the Zero does.
- OTA, W5500 Ethernet, rotary encoder, BLE keyboard: not on this hardware or not needed.

## Could still be combined later
ColorUI only needs six hooks, so it can be offered to the fork as its R8 TFT screen.
