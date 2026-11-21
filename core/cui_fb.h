// ColorUI framebuffer: 320x240 RGB565, plain C++ (no Arduino), so the same
// drawing code runs on the Heltec V4-R8 and in the PC simulator.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace cui {

constexpr int W = 320;
constexpr int H = 240;

constexpr uint16_t rgb(uint32_t hex) {
  return (uint16_t)((((hex >> 16) & 0xF8) << 8) | (((hex >> 8) & 0xFC) << 3) | ((hex & 0xFF) >> 3));
}

// Palette (matches the design mockups)
namespace col {
constexpr uint16_t bg      = rgb(0x0B1017);
constexpr uint16_t panel   = rgb(0x121A23);
constexpr uint16_t line    = rgb(0x1F2A36);
constexpr uint16_t dim     = rgb(0x33414F);
constexpr uint16_t muted   = rgb(0x6B7A8A);
constexpr uint16_t sub     = rgb(0x9FB0C0);
constexpr uint16_t text    = rgb(0xE6EDF3);
constexpr uint16_t white   = rgb(0xFFFFFF);
constexpr uint16_t cyan    = rgb(0x3DD6F5);
constexpr uint16_t orange  = rgb(0xFF9F43);
constexpr uint16_t violet  = rgb(0xB48CFF);
constexpr uint16_t green   = rgb(0x4ADE80);
constexpr uint16_t greenbg = rgb(0x1E6B3A);
constexpr uint16_t txbg    = rgb(0x8A3B12);
constexpr uint16_t red     = rgb(0xFF5050);
constexpr uint16_t yellow  = rgb(0xFFE08A);
}  // namespace col

class Fb {
 public:
  explicit Fb(uint16_t* buf) : px(buf) {}
  uint16_t* px;

  void clear(uint16_t c);
  inline void set(int x, int y, uint16_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) px[y * W + x] = c;
  }
  void fill(int x, int y, int w, int h, uint16_t c);
  void rect(int x, int y, int w, int h, uint16_t c);
  void hline(int x, int y, int w, uint16_t c) { fill(x, y, w, 1, c); }
  void vline(int x, int y, int h, uint16_t c) { fill(x, y, 1, h, c); }
  void line(int x0, int y0, int x1, int y1, uint16_t c);
  // Darken a region toward black: keep = 0..256 (256 = unchanged).
  void shade(int x, int y, int w, int h, int keep);
  // Rounded-ish panel: filled box with a 1px border and clipped corners.
  void panelBox(int x, int y, int w, int h, uint16_t fillc, uint16_t border);

  // 8x8 bitmap text. adv = horizontal advance per glyph (7 = compact, 8 = normal).
  // Returns the x just after the last glyph.
  int text(int x, int y, const char* s, uint16_t c, int scale = 1, int adv = 7);
  static int textWidth(const char* s, int scale = 1, int adv = 7);
  int textRight(int xr, int y, const char* s, uint16_t c, int scale = 1, int adv = 7) {
    return text(xr - textWidth(s, scale, adv), y, s, c, scale, adv);
  }
  int textCenter(int xc, int y, const char* s, uint16_t c, int scale = 1, int adv = 7) {
    return text(xc - textWidth(s, scale, adv) / 2, y, s, c, scale, adv);
  }
  // Pill label: colored box with dark text.
  int pill(int x, int y, const char* s, uint16_t bgc, uint16_t fg);

  // ---- modern UI primitives (anti-aliased) ----
  // Blend color c over the pixel at (x,y); a = 0..255 coverage.
  void blend(int x, int y, uint16_t c, int a);
  void fillRound(int x, int y, int w, int h, int r, uint16_t c);
  void strokeRound(int x, int y, int w, int h, int r, uint16_t c);
  void fillCircle(int cx, int cy, int r, uint16_t c);
  // Thick arc, angles in degrees (0 = right, 90 = up), a0 < a1.
  void arc(int cx, int cy, int r, int thick, int a0, int a1, uint16_t c);
  void lineAA(float x0, float y0, float x1, float y1, uint16_t c);
  // Smooth text. y = top of the line box. Returns x after the text.
  int  txt(int x, int y, const char* s, uint16_t c, uint8_t font);
  static int txtW(const char* s, uint8_t font);
  static int lineH(uint8_t font);
  int  txtR(int xr, int y, const char* s, uint16_t c, uint8_t font) { return txt(xr - txtW(s, font), y, s, c, font); }
  int  txtC(int xc, int y, const char* s, uint16_t c, uint8_t font) { return txt(xc - txtW(s, font) / 2, y, s, c, font); }
  // Cut s (in place) with ".." until it fits in max_w pixels.
  static void fit(char* s, int max_w, uint8_t font);
  // Tabler icon, size 18 or 13 px.
  void icon(int x, int y, uint8_t id, int size, uint16_t c);
  // Rounded label chip with text; returns x after it.
  int  chip(int x, int y, const char* s, uint16_t bgc, uint16_t fg, uint8_t font);
};

// Font ids for Fb::txt (see tools/gen_ui_assets.py)
enum FontId : uint8_t { F_SM = 0, F_BODY, F_BOLD, F_TITLE, F_BIG, F_HUGE };

// Modern theme colors. Mutable so settings.ini can pick a theme.
struct Theme {
  uint16_t bg, rail, card, card2, stroke, text, sub, muted, accent, accent_bg,
           blue, orange, violet, green, yellow, red;
};
extern Theme T;
void setTheme(const char* name);   // "night" (default), "soft", "amber"

}  // namespace cui
