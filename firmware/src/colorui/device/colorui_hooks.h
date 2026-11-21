// The only functions RNode_Firmware.ino calls. Each one just copies an event
// into a queue and returns at once: the radio never waits on the display.
#pragma once
#include <stdint.h>

#define CMD_UI_HOST 0xA0   // KISS command: host -> ColorUI stats (docs/HOST_PROTOCOL.md)

void colorui_begin();                                                     // end of setup()
void colorui_on_rx(const uint8_t* d, uint16_t len, int rssi, float snr);  // after kiss_write_packet()
void colorui_on_tx(const uint8_t* d, uint16_t len);                       // in transmit()
void colorui_on_host_cmd(const uint8_t* d, uint16_t len);                 // CMD_UI_HOST frame
void colorui_on_host_seen();                                              // any host frame
void colorui_on_channel(int noise_floor, int current_rssi, bool interference);  // ~1 Hz
void colorui_set_radio(uint32_t freq, uint32_t bw, uint8_t sf, uint8_t cr, int8_t txp);
void colorui_set_battery(int percent);                                     // -1 = none / unknown
