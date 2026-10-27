#include "output_resolution.h"
#include <assert.h>

int main(void) {
  int w = 1, h = 2;
  unsigned char mode = 99;
  const char *sizes[] = {"1280x720", "1280x800", "1920x1080", "2560x1440", "3840x2160"};
  for (int i = 0; i < 5; ++i) assert(SnesOutput_ParseSize(sizes[i], &w, &h) && w > 0 && h > 0);
  assert(SnesOutput_ParseSize("AUTO", &w, &h) && !w && !h);
  const char *bad[] = {"", "0x0", "1920", "1920x", "1920x1080junk", "-1920x1080",
      "999999999999999999999999999x2160", "16385x2160", "63x1080", "1920x63", "1920x1080x2"};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
    w = 42; h = 43;
    assert(!SnesOutput_ParseSize(bad[i], &w, &h) && w == 42 && h == 43);
  }
  for (int i = 0; i <= 3; ++i)
    assert(SnesOutput_ParseMode(SnesOutput_ModeName(i), &mode) && mode == i);
  assert(SnesOutput_ParseMode("FuLlScReEn", &mode) && mode == 3);
  assert(!SnesOutput_ParseMode("exclusive-ish", &mode) && mode == 3);
  for (int legacy = 0; legacy <= 2; ++legacy) {
    assert(SnesOutput_Fullscreen(0, legacy) == legacy);
    assert(SnesOutput_Fullscreen(1, legacy) == 0);
    assert(SnesOutput_Fullscreen(2, legacy) == 1);
    assert(SnesOutput_Fullscreen(3, legacy) == 2);
  }
  assert(!SnesOutput_BetterMode(3840, 2160, 60, 1920, 1080, 60, -1));
  assert(SnesOutput_BetterMode(1920, 1080, 60, 1920, 1080, 59.94, 120));
  assert(!SnesOutput_BetterMode(1920, 1080, 120, 1920, 1080, 59.94, 60));
  assert(SnesOutput_BetterMode(1920, 1080, 120, 1920, 1080, 120, 60));
  return 0;
}
