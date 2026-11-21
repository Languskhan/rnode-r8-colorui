#include "cui_fb.h"
#include "font8x8_basic.h"
#include <stdlib.h>

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
