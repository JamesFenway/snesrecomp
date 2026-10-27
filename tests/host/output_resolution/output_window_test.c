/* A controllable display backend exercises mode loss, rejection and density;
 * the separate SDL test checks pixel sizes on a real SDL window. */
#define SNESRECOMP_DESKTOP_SDL_COMPAT_H
#define SNESRECOMP_SDL3 1
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
typedef unsigned SDL_DisplayID;
typedef struct SDL_DisplayMode { int w, h; float refresh_rate; } SDL_DisplayMode;
typedef struct SDL_Window { int w, h, density, fullscreen; const SDL_DisplayMode *mode; } SDL_Window;
#define SDL_WINDOW_FULLSCREEN 1
static SDL_DisplayMode desktop = {1920, 1080, 60};
static SDL_DisplayMode modes[] = {{1920, 1080, 120}, {1920, 1080, 60}, {3840, 2160, 60}};
static int mode_count = 3, reject_exclusive, reject_all;
static bool SDL_SetWindowFullscreen(SDL_Window *w, bool enabled) {
  if (enabled && (reject_all || (w->mode && reject_exclusive))) return false;
  w->fullscreen = enabled;
  return true;
}
static bool SDL_SetWindowFullscreenMode(SDL_Window *w, const SDL_DisplayMode *m) { w->mode = m; return true; }
static SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *w) { (void)w; return 1; }
static const SDL_DisplayMode *SDL_GetDesktopDisplayMode(SDL_DisplayID id) { (void)id; return &desktop; }
static SDL_DisplayMode **SDL_GetFullscreenDisplayModes(SDL_DisplayID id, int *count) {
  (void)id; *count = mode_count;
  SDL_DisplayMode **list = malloc(sizeof(*list) * (mode_count + 1));
  for (int i = 0; i < mode_count; ++i) list[i] = &modes[i];
  list[mode_count] = NULL; return list;
}
#define SDL_free free
static const char *SDL_GetError(void) { return "display disconnected"; }
static int SDL_GetWindowFlags(SDL_Window *w) { return w->fullscreen; }
static void SDL_GetWindowSizeInPixels(SDL_Window *w, int *x, int *y) {
  *x = w->w * w->density; *y = w->h * w->density;
}
static void SDL_GetWindowSize(SDL_Window *w, int *x, int *y) { *x = w->w; *y = w->h; }
static bool SDL_SetWindowSize(SDL_Window *w, int x, int y) { w->w = x; w->h = y; return true; }
static bool SDL_SyncWindow(SDL_Window *w) { (void)w; return true; }
#include "output_window.h"

int main(void) {
  SDL_Window w = {640, 480, 2, 0, NULL};
  SnesOutputResult r = SnesOutput_Apply(&w, 0, 3840, 2160);
  assert(!r.fullscreen && !r.note[0] && w.w == 1920 && w.h == 1080);
  int x = 0, y = 0; SnesOutput_PixelSize(&w, &x, &y);
  assert(x == 3840 && y == 2160);
  r = SnesOutput_Apply(&w, 2, 1920, 1080);
  assert(r.fullscreen == 2 && w.mode == &modes[1]); /* retain 60 Hz */
  desktop.refresh_rate = 120;
  r = SnesOutput_Apply(&w, 2, 1920, 1080);
  assert(r.fullscreen == 2 && w.mode == &modes[0]);
  r = SnesOutput_Apply(&w, 2, 3840, 2160);
  assert(r.fullscreen == 2 && w.mode == &modes[2]);
  mode_count = 2; /* undock: 4K disappears before the next launch */
  r = SnesOutput_Apply(&w, 2, 3840, 2160);
  assert(r.fullscreen == 1 && !w.mode && strstr(r.note, "unavailable"));
  r = SnesOutput_Apply(&w, 2, 0, 0);
  assert(r.fullscreen == 2 && w.mode == &modes[0]); /* native Auto */
  reject_exclusive = 1;
  r = SnesOutput_Apply(&w, 2, 1920, 1080);
  assert(r.fullscreen == 1 && !w.mode && strstr(r.note, "could not be applied"));
  reject_all = 1;
  r = SnesOutput_Apply(&w, 2, 1920, 1080);
  assert(!r.fullscreen && !w.mode && w.w == 960 && w.h == 540 && r.note[0]);
  reject_exclusive = reject_all = 0;
  r = SnesOutput_Apply(&w, 1, 3840, 2160);
  assert(r.fullscreen == 1 && !w.mode); /* borderless never switches a mode */
  r = SnesOutput_Apply(&w, 0, 1280, 800);
  assert(!r.fullscreen && !w.mode && w.w == 640 && w.h == 400);
  r = SnesOutput_Apply(&w, 0, 0, 0);
  assert(!r.fullscreen && w.w == 640 && w.h == 400); /* Auto leaves size alone */
  return 0;
}
