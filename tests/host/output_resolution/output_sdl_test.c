#include "output_window.h"
#include <assert.h>

int main(void) {
  assert(snesrecomp_sdl_init(SDL_INIT_VIDEO));
  SDL_Window *w = snesrecomp_sdl_create_window("output pixels", 640, 480, SDL_WINDOW_RESIZABLE);
  assert(w);
  const int sizes[][2] = {{1280, 720}, {1280, 800}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
  for (int i = 0; i < 5; ++i) {
    SnesOutputResult r = SnesOutput_Apply(w, 0, sizes[i][0], sizes[i][1]);
    int width = 0, height = 0;
    SnesOutput_PixelSize(w, &width, &height);
    assert(!r.fullscreen && width == sizes[i][0] && height == sizes[i][1]);
  }
  SDL_DestroyWindow(w);
  SDL_Quit();
  return 0;
}
