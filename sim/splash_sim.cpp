// Renders every boot splash scene to PPM files: splash_sim OUTDIR [progress]
#include "cui.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
  static uint16_t buf[cui::W * cui::H];
  cui::Fb fb(buf);
  const int prog = argc > 2 ? atoi(argv[2]) : 150;
  for (int s = 0; s < cui::splashCount(); ++s) {
    fb.clear(0);
    cui::drawSplash(fb, s, "ColorUI 0.6", prog);
    char path[512];
    snprintf(path, sizeof path, "%s/splash%d.ppm", argv[1], s);
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", cui::W, cui::H);
    for (int i = 0; i < cui::W * cui::H; ++i) {
      const uint16_t c = buf[i];
      const unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)};
      fwrite(rgb, 1, 3, f);
    }
    fclose(f);
  }
  printf("%d scenes\n", cui::splashCount());
}
