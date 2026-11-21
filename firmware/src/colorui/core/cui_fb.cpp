#include "cui_fb.h"
#include "font8x8_basic.h"
#include <stdlib.h>
#include <stdio.h>

namespace cui {

void Fb::clear(uint16_t c) {
  for (int i = 0; i < W * H; ++i) px[i] = c;
}

void Fb::fill(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = y; j < y + h; ++j) {
    uint16_t* row = px + j * W + x;
    for (int i = 0; i < w; ++i) row[i] = c;
  }
}

void Fb::rect(int x, int y, int w, int h, uint16_t c) {
  hline(x, y, w, c); hline(x, y + h - 1, w, c);
  vline(x, y, h, c); vline(x + w - 1, y, h, c);
}

void Fb::line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    set(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Fb::shade(int x, int y, int w, int h, int keep) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  for (int j = y; j < y + h; ++j)
    for (int i = x; i < x + w; ++i) {
      uint16_t p = px[j * W + i];
      int r = ((p >> 11) & 31) * keep >> 8, g = ((p >> 5) & 63) * keep >> 8, b = (p & 31) * keep >> 8;
      px[j * W + i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

void Fb::panelBox(int x, int y, int w, int h, uint16_t fillc, uint16_t border) {
  fill(x + 1, y + 1, w - 2, h - 2, fillc);
  hline(x + 1, y, w - 2, border); hline(x + 1, y + h - 1, w - 2, border);
  vline(x, y + 1, h - 2, border); vline(x + w - 1, y + 1, h - 2, border);
}

int Fb::text(int x, int y, const char* s, uint16_t c, int scale, int adv) {
  for (; *s; ++s) {
    unsigned char ch = (unsigned char)*s;
    if (ch > 127) ch = '?';
    const unsigned char* g = font8x8_basic[ch];
    for (int row = 0; row < 8; ++row) {
      unsigned char bits = g[row];
      if (!bits) continue;
      for (int colx = 0; colx < 8; ++colx)
        if (bits & (1 << colx)) {
          if (scale == 1) set(x + colx, y + row, c);
          else fill(x + colx * scale, y + row * scale, scale, scale, c);
        }
    }
    x += adv * scale;
  }
  return x;
}

int Fb::textWidth(const char* s, int scale, int adv) {
  int n = 0;
  while (s[n]) ++n;
  return n * adv * scale;
}

int Fb::pill(int x, int y, const char* s, uint16_t bgc, uint16_t fg) {
  int w = textWidth(s) + 5;
  fill(x, y, w, 10, bgc);
  set(x, y, col::bg); set(x + w - 1, y, col::bg); set(x, y + 9, col::bg); set(x + w - 1, y + 9, col::bg);
  text(x + 3, y + 1, s, fg);
  return x + w;
}

}  // namespace cui

// ============================================================================
// Modern UI primitives
// ============================================================================
#include "cui_ui_assets.h"
#include <math.h>
#include <string.h>

namespace cui {

Theme T;

static Theme makeTheme(uint32_t accent, uint32_t accent_bg, uint32_t bg, uint32_t card) {
  Theme t;
  t.bg = rgb(bg); t.rail = rgb(0x080B0F); t.card = rgb(card); t.card2 = rgb(0x1B232C);
  t.stroke = rgb(0x26303B); t.text = rgb(0xE8EEF4); t.sub = rgb(0x9AA7B4); t.muted = rgb(0x66737F);
  t.accent = rgb(accent); t.accent_bg = rgb(accent_bg);
  t.blue = rgb(0x60A5FA); t.orange = rgb(0xFB923C); t.violet = rgb(0xA78BFA);
  t.green = rgb(0x4ADE80); t.yellow = rgb(0xFACC15); t.red = rgb(0xF87171);
  return t;
}

void setTheme(const char* n) {
  if (n && !strcmp(n, "soft"))       T = makeTheme(0x93C5FD, 0x1E2A44, 0x10141C, 0x181E29);
  else if (n && !strcmp(n, "amber")) T = makeTheme(0xFBBF24, 0x3A2C0E, 0x0E0C09, 0x1A1712);
  else                               T = makeTheme(0x2DD4BF, 0x113A35, 0x0B0F14, 0x141A21);
}

namespace {
struct ThemeInit { ThemeInit() { setTheme("night"); } } s_theme_init;

const AAFont* kFonts[] = {&F_SM_font, &F_BODY_font, &F_BOLD_font, &F_TITLE_font, &F_BIG_font, &F_HUGE_font};

inline uint16_t mix(uint16_t a, uint16_t b, int t) {   // t = 0..256 amount of b
  const int r = ((a >> 11) & 31) + ((((b >> 11) & 31) - ((a >> 11) & 31)) * t >> 8);
  const int g = ((a >> 5) & 63) + ((((b >> 5) & 63) - ((a >> 5) & 63)) * t >> 8);
  const int bl = (a & 31) + (((b & 31) - (a & 31)) * t >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
}  // namespace

void Fb::blend(int x, int y, uint16_t c, int a) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H || a <= 0) return;
  uint16_t& p = px[y * W + x];
  p = a >= 255 ? c : mix(p, c, a + (a >> 7));
}

// Coverage of a pixel by a rounded corner: distance-based, 1 px soft edge.
static int cornerCov(float dx, float dy, float r) {
  const float d = sqrtf(dx * dx + dy * dy);
  const float v = r - d + 0.5f;
  return v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255);
}

void Fb::fillRound(int x, int y, int w, int h, int r, uint16_t c) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  fill(x + r, y, w - 2 * r, h, c);
  fill(x, y + r, r, h - 2 * r, c);
  fill(x + w - r, y + r, r, h - 2 * r, c);
  for (int j = 0; j < r; ++j)
    for (int i = 0; i < r; ++i) {
      const int a = cornerCov(r - i - 0.5f, r - j - 0.5f, (float)r);
      if (!a) continue;
      blend(x + i, y + j, c, a);                 blend(x + w - 1 - i, y + j, c, a);
      blend(x + i, y + h - 1 - j, c, a);         blend(x + w - 1 - i, y + h - 1 - j, c, a);
    }
}

void Fb::strokeRound(int x, int y, int w, int h, int r, uint16_t c) {
  hline(x + r, y, w - 2 * r, c); hline(x + r, y + h - 1, w - 2 * r, c);
  vline(x, y + r, h - 2 * r, c); vline(x + w - 1, y + r, h - 2 * r, c);
  for (int j = 0; j < r; ++j)
    for (int i = 0; i < r; ++i) {
      const float dx = r - i - 0.5f, dy = r - j - 0.5f;
      const float d = sqrtf(dx * dx + dy * dy);
      const float v = 1.0f - fabsf(d - (r - 0.5f));
      if (v <= 0) continue;
      const int a = (int)(v * 255);
      blend(x + i, y + j, c, a);                 blend(x + w - 1 - i, y + j, c, a);
      blend(x + i, y + h - 1 - j, c, a);         blend(x + w - 1 - i, y + h - 1 - j, c, a);
    }
}

void Fb::fillCircle(int cx, int cy, int r, uint16_t c) {
  for (int j = -r - 1; j <= r; ++j)
    for (int i = -r - 1; i <= r; ++i) blend(cx + i, cy + j, c, cornerCov(i + 0.5f, j + 0.5f, (float)r));
}

void Fb::arc(int cx, int cy, int r, int thick, int a0, int a1, uint16_t c) {
  const float ri = r - thick;
  for (int j = -r - 1; j <= r; ++j)
    for (int i = -r - 1; i <= r; ++i) {
      const float fx = i + 0.5f, fy = j + 0.5f;
      const float d = sqrtf(fx * fx + fy * fy);
      float cov = fminf(r - d + 0.5f, d - ri + 0.5f);
      if (cov <= 0) continue;
      if (cov > 1) cov = 1;
      float ang = atan2f(-fy, fx) * 57.29578f;
      if (ang < 0) ang += 360;
      if (ang < a0 || ang > a1) continue;
      blend(cx + i, cy + j, c, (int)(cov * 255));
    }
}

void Fb::lineAA(float x0, float y0, float x1, float y1, uint16_t c) {   // Xiaolin Wu
  const bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
  if (steep) { float t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
  if (x0 > x1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
  const float dx = x1 - x0, g = dx == 0 ? 1 : (y1 - y0) / dx;
  float y = y0;
  for (int x = (int)roundf(x0); x <= (int)roundf(x1); ++x, y += g) {
    const int yi = (int)floorf(y);
    const int a = (int)((y - yi) * 255);
    if (steep) { blend(yi, x, c, 255 - a); blend(yi + 1, x, c, a); }
    else       { blend(x, yi, c, 255 - a); blend(x, yi + 1, c, a); }
  }
}

int Fb::txt(int x, int y, const char* s, uint16_t c, uint8_t font) {
  const AAFont& f = *kFonts[font];
  const int base = y + f.ascent;
  for (; *s; ++s) {
    unsigned ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) ch = '?';
    const AAGlyph& g = f.g[ch - 32];
    if (g.w) {
      const uint8_t* d = f.data + g.off;
      const int rb = (g.w + 1) / 2;
      for (int j = 0; j < g.h; ++j)
        for (int i = 0; i < g.w; ++i) {
          const uint8_t b = d[j * rb + i / 2];
          const int a = (i & 1) ? (b & 15) : (b >> 4);
          if (a) blend(x + g.xo + i, base + g.yo + j, c, a * 17);
        }
    }
    x += g.adv;
  }
  return x;
}

int Fb::txtW(const char* s, uint8_t font) {
  const AAFont& f = *kFonts[font];
  int w = 0;
  for (; *s; ++s) {
    unsigned ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) ch = '?';
    w += f.g[ch - 32].adv;
  }
  return w;
}

int Fb::lineH(uint8_t font) { return kFonts[font]->line; }

void Fb::fit(char* s, int max_w, uint8_t font) {
  if (txtW(s, font) <= max_w) return;
  size_t n = strlen(s);
  while (n > 1) {
    s[--n] = 0;
    char t[64];
    snprintf(t, sizeof t, "%s..", s);
    if (txtW(t, font) <= max_w) { strcpy(s + n, ".."); return; }
  }
}

void Fb::icon(int x, int y, uint8_t id, int size, uint16_t c) {
  if (id >= I_COUNT) return;
  const uint8_t* d = size >= 16 ? kIcon18[id] : kIcon13[id];
  const int px = size >= 16 ? 18 : 13, rb = (px + 1) / 2;
  for (int j = 0; j < px; ++j)
    for (int i = 0; i < px; ++i) {
      const uint8_t b = d[j * rb + i / 2];
      const int a = (i & 1) ? (b & 15) : (b >> 4);
      if (a) blend(x + i, y + j, c, a * 17);
    }
}

int Fb::chip(int x, int y, const char* s, uint16_t bgc, uint16_t fg, uint8_t font) {
  const int h = lineH(font) + 4, w = txtW(s, font) + 10;
  fillRound(x, y, w, h, h / 2 < 6 ? h / 2 : 6, bgc);
  txt(x + 5, y + 2, s, fg, font);
  return x + w;
}

}  // namespace cui
