# Using your VHF/UHF and HF radios (KISS TNC)

Radios connect to the **host** (Cardputer Zero running rnsd), not to the
Heltec. The Heltec stays the LoRa RNode; its Interfaces page shows every
radio rnsd is using, via `host/ColorUIRNodeInterface.py`.

```
 UV-PRO / UV-50PRO --Bluetooth KISS--+
 HF rig + Direwolf --KISS over TCP----+--> rnsd on the Cardputer Zero --USB--> Heltec V4-R8 ColorUI (LoRa)
 Home Wi-Fi / TCP backbone ----------+
```

## Read this first: ham rules
Reticulum encrypts links and LXMF messages. FCC Part 97 forbids messages
encoded to hide their meaning, so carrying normal Reticulum traffic on
2 m / 70 cm / HF is not OK in the US, even on your own frequencies. Good
uses for these ports today: testing, unencrypted experiments, APRS/Winlink
with other software. Keep encrypted Reticulum on LoRa at Part 15 power.

## UV-PRO or UV-50PRO over Bluetooth KISS
1. Turn on KISS TNC in the radio (it disconnects the BTECH app while on).
2. Pair from the Zero: `bluetoothctl` -> `scan on`, `pair <MAC>`, `trust <MAC>`.
3. Bind a serial port: `sudo rfcomm bind 0 <MAC> 1` (find the right channel
   with `sdptool browse <MAC>` if 1 doesn't work) -> `/dev/rfcomm0`.
4. Add to `~/.reticulum/config`:
```
[[UV-PRO BT KISS]]
  type = AX25KISSInterface
  enabled = yes
  port = /dev/rfcomm0
  speed = 9600
  callsign = KO6FPT
  ssid = 0
  preamble = 150
  txtail = 10
  persistence = 200
  slottime = 20
  flow_control = false
```

## HF with Direwolf (KISS over TCP)
Run Direwolf with an HF modem (e.g. 300 baud) and its KISS TCP port (8001),
then:
```
[[HF Direwolf]]
  type = TCPClientInterface
  enabled = yes
  kiss_framing = yes
  target_host = 127.0.0.1
  target_port = 8001
```
Add the name to `colorui_hf = HF Direwolf` in the ColorUI interface section so
the screen labels it HF (yellow) instead of VHF/UHF (orange).

## Other KISS gear
Any serial KISS TNC works with `type = KISSInterface` (plus `id_callsign` and
`id_interval` for station ID). It shows up on the Interfaces page automatically.
