// Wi-Fi file manager for the SD card (http://<board IP>/ or http://rnode-r8.local/).
// Runs inside the ColorUI task, between frames, so the SD card and the LCD
// (which share SPI3) are never used at the same time.
#pragma once

// Call every loop of the UI task. Starts the web server once Wi-Fi is up and
// the SD card is mounted live; then serves requests.
void colorui_web_poll();
// True while a request is being served (the UI skips a frame).
bool colorui_web_busy();
