/* Output size is presentation state, independent of the emulated framebuffer.
 * Auto leaves legacy Fullscreen / WindowSize / WindowScale behaviour intact. */
#ifndef SNESRECOMP_OUTPUT_RESOLUTION_H
#define SNESRECOMP_OUTPUT_RESOLUTION_H
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

enum { kSnesOutput_Auto, kSnesOutput_Windowed, kSnesOutput_Borderless,
       kSnesOutput_Fullscreen };

static inline const char *SnesOutput_ModeName(int mode) {
  static const char *names[] = {"auto", "windowed", "borderless", "fullscreen"};
  return names[mode >= 0 && mode <= 3 ? mode : 0];
}

static inline bool SnesOutput_Equals(const char *a, const char *b) {
  while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
  return !*a && !*b;
}

static inline bool SnesOutput_ParseMode(const char *value, unsigned char *out) {
  for (int i = 0; i <= 3; ++i)
    if (SnesOutput_Equals(value, SnesOutput_ModeName(i))) { *out = i; return true; }
  return false;
}

static inline bool SnesOutput_ParseSize(const char *value, int *width, int *height) {
  if (SnesOutput_Equals(value, "auto")) { *width = *height = 0; return true; }
  /* Bounded before multiplication: even a maliciously long value cannot
   * overflow, allocate an enormous surface, or leave a half-parsed size. */
  int dims[2] = {0, 0};
  const char *p = value;
  for (int i = 0; i < 2; ++i) {
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) {
      dims[i] = dims[i] * 10 + (*p++ - '0');
      if (dims[i] > 16384) return false;
    }
    if (dims[i] < 64) return false;
    if (!i) { if (*p != 'x' && *p != 'X') return false; ++p; }
  }
  if (*p) return false;
  *width = dims[0]; *height = dims[1];
  return true;
}

static inline int SnesOutput_Fullscreen(int mode, int legacy) {
  return mode == kSnesOutput_Auto ? (legacy <= 2 ? legacy : 0) :
         mode == kSnesOutput_Windowed ? 0 : mode == kSnesOutput_Fullscreen ? 2 : 1;
}

/* Prefer the current refresh rate at the exact requested pixel dimensions.
 * Never quietly choose a nearby resolution. Ties prefer the higher rate. */
static inline bool SnesOutput_BetterMode(int w, int h, double hz,
    int want_w, int want_h, double current_hz, double best_hz) {
  if (w != want_w || h != want_h) return false;
  if (best_hz < 0) return true;
  double delta = fabs(hz - current_hz), best_delta = fabs(best_hz - current_hz);
  return delta < best_delta || (delta == best_delta && hz > best_hz);
}
#endif
