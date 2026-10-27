/* SDL2/3 output mode application. Preferences are never replaced by fallbacks.
 * Re-enumerate the window's display on every apply (including a new launch). */
#ifndef SNESRECOMP_OUTPUT_WINDOW_H
#define SNESRECOMP_OUTPUT_WINDOW_H
#include "sdl_compat.h"
#include "output_resolution.h"
#include <stdio.h>

typedef struct SnesOutputResult {
  int fullscreen;                 /* effective 0=window, 1=borderless, 2=exclusive */
  char note[384];
} SnesOutputResult;

static inline bool SnesOutput_SetFullscreen(SDL_Window *window, bool enabled, bool exclusive) {
#if SNESRECOMP_SDL3
  (void)exclusive;
  return SDL_SetWindowFullscreen(window, enabled);
#else
  return SDL_SetWindowFullscreen(window, enabled ?
      (exclusive ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP) : 0) == 0;
#endif
}

static inline bool SnesOutput_ClearMode(SDL_Window *window) {
#if SNESRECOMP_SDL3
  return SDL_SetWindowFullscreenMode(window, NULL);
#else
  return SDL_SetWindowDisplayMode(window, NULL) == 0;
#endif
}

static inline bool SnesOutput_ChooseMode(SDL_Window *window, int width, int height) {
  double best_hz = -1;
#if SNESRECOMP_SDL3
  SDL_DisplayID display = SDL_GetDisplayForWindow(window);
  const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
  if (!desktop) return false;
  if (!width || !height) { width = desktop->w; height = desktop->h; }
  int count = 0, best = -1;
  SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
  for (int i = 0; modes && i < count; ++i) {
    if (SnesOutput_BetterMode(modes[i]->w, modes[i]->h, modes[i]->refresh_rate,
          width, height, desktop->refresh_rate, best_hz)) {
      best = i; best_hz = modes[i]->refresh_rate;
    }
  }
  bool ok = best >= 0 && SDL_SetWindowFullscreenMode(window, modes[best]);
  SDL_free(modes);
  return ok;
#else
  int display = SDL_GetWindowDisplayIndex(window);
  SDL_DisplayMode desktop, mode, best;
  if (display < 0 || SDL_GetDesktopDisplayMode(display, &desktop) != 0) return false;
  if (!width || !height) { width = desktop.w; height = desktop.h; }
  for (int i = 0; i < SDL_GetNumDisplayModes(display); ++i) {
    if (SDL_GetDisplayMode(display, i, &mode) == 0 &&
        SnesOutput_BetterMode(mode.w, mode.h, mode.refresh_rate,
          width, height, desktop.refresh_rate, best_hz)) {
      best = mode; best_hz = mode.refresh_rate;
    }
  }
  return best_hz >= 0 && SDL_SetWindowDisplayMode(window, &best) == 0;
#endif
}

static inline void SnesOutput_PixelSize(SDL_Window *window, int *width, int *height) {
#if SNESRECOMP_SDL3
  SDL_GetWindowSizeInPixels(window, width, height);
#else
  /* Available since SDL 2.26; older backends still expose GL drawable size. */
#if SDL_VERSION_ATLEAST(2, 26, 0)
  SDL_GetWindowSizeInPixels(window, width, height);
#else
  SDL_GL_GetDrawableSize(window, width, height);
  if (*width <= 0 || *height <= 0) SDL_GetWindowSize(window, width, height);
#endif
#endif
}

static inline SnesOutputResult SnesOutput_Apply(SDL_Window *window, int fullscreen,
                                               int width, int height) {
  SnesOutputResult result = {fullscreen, ""};
  if (!SnesOutput_SetFullscreen(window, false, false)) {
    snprintf(result.note, sizeof(result.note), "Could not leave fullscreen: %.280s", SDL_GetError());
    result.fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? 1 : 0;
    return result;
  }
  SnesOutput_ClearMode(window);
  if (fullscreen == 2 && !SnesOutput_ChooseMode(window, width, height)) {
    result.fullscreen = 1;
    snprintf(result.note, sizeof(result.note),
        "Requested fullscreen resolution is unavailable. Using desktop fullscreen. "
        "In Steam Game Mode, check the shortcut's Game Resolution setting.");
    SnesOutput_ClearMode(window);
  }
  if (result.fullscreen && !SnesOutput_SetFullscreen(window, true, result.fullscreen == 2)) {
    /* A mode can disappear between enumeration and apply (undocking). */
    SnesOutput_ClearMode(window);
    result.fullscreen = SnesOutput_SetFullscreen(window, true, false) ? 1 : 0;
    snprintf(result.note, sizeof(result.note),
        "Fullscreen mode could not be applied. Using %s: %.220s",
        result.fullscreen ? "desktop fullscreen" : "a window", SDL_GetError());
  }
#if SNESRECOMP_SDL3
  SDL_SyncWindow(window);
#endif
  if (!result.fullscreen && width > 0 && height > 0) {
    /* Window dimensions are logical points; a preset specifies drawable pixels.
     * Measure density on the actual window, rather than using UI text scale. */
    int logical_w = 0, logical_h = 0, pixel_w = 0, pixel_h = 0;
    SDL_GetWindowSize(window, &logical_w, &logical_h);
    SnesOutput_PixelSize(window, &pixel_w, &pixel_h);
    int w = pixel_w > 0 && logical_w > 0 ? (int)lround((double)width * logical_w / pixel_w) : width;
    int h = pixel_h > 0 && logical_h > 0 ? (int)lround((double)height * logical_h / pixel_h) : height;
    SDL_SetWindowSize(window, w > 0 ? w : 1, h > 0 ? h : 1);
#if SNESRECOMP_SDL3
    SDL_SyncWindow(window);
#endif
  }
  return result;
}
#endif
