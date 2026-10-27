/* Render the actual built-in presets through the desktop GL presenter.
 * Run each preset in a separate process; screenshots contain no ROM data. */
#include "config.h"
#include "util.h"
#include "opengl.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
Config g_config;
void Die(const char *message) { fprintf(stderr, "%s\n", message); exit(1); }
void MkDir(const char *path) { mkdir(path, 0777); }
void OpenGLRenderer_Create(struct RendererFuncs *funcs);
int main(int argc, char **argv) {
  if (argc != 2 || !SDL_Init(SDL_INIT_VIDEO)) return 1;
  struct RendererFuncs renderer;
  OpenGLRenderer_Create(&renderer);
  SDL_Window *window = SDL_CreateWindow("Preset test", 768, 672, SDL_WINDOW_OPENGL);
  if (!window || !renderer.Initialize(window)) return 2;
  g_config.shader = argv[1][0] ? argv[1] : NULL;
  renderer.Reconfigure();
  uint8 *pixels; int pitch;
  renderer.BeginDraw(256, 224, &pixels, &pitch);
  for (int y = 0; y < 224; ++y) for (int x = 0; x < 256; ++x) {
    uint8 *pixel = pixels + y * pitch + x * 4;
    pixel[0] = (x / 8 % 2) * 190;
    pixel[1] = (y / 8 % 2) * 190;
    pixel[2] = 80 + (x % 32) * 5;
    pixel[3] = 255;
  }
  OpenGLRenderer_RequestScreenshot();
  renderer.EndDraw();
  SDL_Quit();
  return 0;
}
