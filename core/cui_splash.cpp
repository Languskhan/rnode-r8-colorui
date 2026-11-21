// Boot splash for RNode R8 ColorUI. Plain C++ on the shared Fb, so the
// device and the PC simulator draw exactly the same pixels.
#include "cui_fb.h"
#include "cui_splash_art.h"

// Splash title. Override at build time to brand your own box, e.g. in
// platformio.ini build_flags:  '-DCOLORUI_BRAND="MY RETICULUM BOX"'
#ifndef COLORUI_BRAND
#define COLORUI_BRAND "RNODE R8 COLORUI"
#endif

namespace cui {

int splashCount() { return kSplashCount; }

// scene: 0..splashCount()-1 (wrapped). progress: 0..256 fill of the DEMO
// countdown bar, or -1 to hide it.
void drawSplash(Fb& fb, int scene, const char* version, int progress) {
  scene = ((scene % kSplashCount) + kSplashCount) % kSplashCount;
  const uint8_t* pix = kSplashPix[scene];
  const uint16_t* pal = kSplashPal[scene];
  for (int y = 0; y < 120; ++y) {
    const uint8_t* row = &pix[y * 160];
    uint16_t* d0 = &fb.px[(2 * y) * W];
    uint16_t* d1 = d0 + W;
    for (int x = 0; x < 160; ++x) {
      const uint16_t c = pal[row[x]];
      d0[2 * x] = d0[2 * x + 1] = d1[2 * x] = d1[2 * x + 1] = c;
    }
  }

  // title, with a 1px drop shadow so it reads over any sky
  const char* title = COLORUI_BRAND;
  fb.textCenter(W / 2 + 1, 9, title, col::bg, 2, 7);
  fb.textCenter(W / 2, 8, title, col::text, 2, 7);
  const char* kind = "RNode  -  LoRa radio for Reticulum";
  fb.textCenter(W / 2 + 1, 29, kind, col::bg, 1, 7);
  fb.textCenter(W / 2, 28, kind, col::cyan, 1, 7);

  // bottom strip: this scene's caption + version, DEMO prompt, countdown bar
  fb.shade(0, 196, W, 44, 110);
  fb.hline(0, 196, W, col::line);
  fb.text(8, 202, kSplashCaption[scene], col::yellow, 1, 7);
  fb.textRight(W - 8, 202, version, col::muted, 1, 7);
  fb.text(8, 216, "Tap screen or press PRG for DEMO", col::text, 1, 7);
  if (progress >= 0) {
    fb.fill(8, 230, W - 16, 4, col::dim);
    fb.fill(8, 230, (W - 16) * progress / 256, 4, col::cyan);
  }
}

}  // namespace cui
