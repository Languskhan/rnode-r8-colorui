# What goes on the SD card

Format the card **FAT32** (Windows only offers FAT32 up to 32 GB; on a bigger
card make a 31 GB FAT32 partition, see the README). Everything is optional.

```
/cities.bin            city locator (WADAMESH format, max 4 MB)
/settings.ini          your settings (sd-card-example/settings.ini)
/names.txt             nicknames for nodes (sd-card-example/names.txt)
/splash/*.bmp          your own boot pictures: 320x240, 24-bit or 16-bit BMP
/tiles/<z>/<x>/<y>.jpg offline map (OSM "slippy" layout, .jpg or .png; same as WADAMESH)
/logs/                 written by the RNode: heard-YYYY-MM-DD.csv, stats.bin (History tab)
```

The card is read when the board starts. After changing a file, press RST.

## cities.bin
Download your state from the WADAMESH city-locator release
(github.com/Languskhan/wadamesh/releases), rename it `cities.bin`.

## settings.ini
`key = value` lines; see the example for every key. The most useful:
`brand`, `theme` (night / soft / amber / retro), `tz`, `clock`, `my_lxmf`,
`screensaver_minutes`, `brightness`.

`my_lxmf` is your LXMF address. With it the RNode shows **Message for you**
when a packet for that address goes by, shows **You were heard!** when
another node repeats your announce, and puts a QR code of the address on the
Status page so friends can scan it into their app.

## names.txt
`<hash> <nickname>` per line. Put `!` before the nickname to add it to your **watch
list**: `0056ee98  !KJ6 Mobile (Dad)` pops "In range!" (and chirps) when that node is
heard after 30 minutes away. Matches destination hashes and relay IDs by
prefix (8+ hex characters), so "via a1b2c3d4" becomes "via Hilltop Repeater".

## /splash
Any number of 320x240 BMP pictures join the 12 built-in scenes in the
rotation, one per boot.

## /tiles (offline map)
The Area page draws the map under your coverage dots. Tiles are standard
256x256 web-map tiles in `/tiles/<zoom>/<x>/<y>.jpg` (or `.png`). The RNode
uses zoom 14 if present, otherwise the nearest zoom it finds (10-18). A
WADAMESH map card works as-is. Respect the tile provider's terms when you
download tiles.

## /logs
Every packet heard or sent, every 30 s, as CSV:
`unix,local_time,dir,type,name,hash,hops,rssi,snr,lat,lon,area`.
Open it in a spreadsheet, or load lat/lon/rssi into a map tool to see your
coverage. Turn off with `log = off`.

## Over Wi-Fi (no card reader needed)
When the RNode is on your Wi-Fi (`tools/pc/rnode_wifi.py`), open
**http://<its IP>/** (shown on the Status page, also `http://rnode-r8.local/`)
in any browser on the same network:

- **Files**: browse the card, download logs, delete, make folders, upload files
  or a whole folder (a tile folder keeps its `<z>/<x>/<y>` layout).
- **Update**: pick a `firmware.bin` to update the RNode over Wi-Fi (set a
  `web_password` first if others use your network; `web_update = off` disables it).
- **Settings** and **Nicknames**: edit `settings.ini` / `names.txt` in the browser;
  saving restarts the RNode so it reads them.

Set `web_password = ...` in settings.ini to require a login (user `rnode`).
It is plain HTTP on your own network: fine at home, don't expose port 80 to
the internet. `web = off` turns it off. The screen pauses while files copy.

## If the screen glitches with a card inserted
The card and the screen share one SPI bus. If you see garbled bands or
freezes, set `sd_live = off`: the card is then read only at boot (cities,
names, settings, splash still work; map and logs are off).
