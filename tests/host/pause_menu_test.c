/* snes_pause_menu: the Select+Start chord filter and the menu's navigation.
 * Build: tests/host/pause_menu/CMakeLists.txt. */
#include "snes_pause_menu.h"
#include "snes_overlay_draw.h"
#include <stdio.h>
#include <string.h>

static int g_fail;
#define CHECK(cond) do { if (!(cond)) { \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

#define SEL SNES_PAD_SELECT
#define START SNES_PAD_START
#define D SNES_PAUSE_CHORD_FRAMES

/* Feed `n` frames of `word`; returns the OR of what the guest saw and counts
 * the frames on which `bit` reached it. */
static uint32_t feed(uint32_t word, int n, uint32_t bit, int *bit_frames, int *opened) {
  uint32_t seen = 0;
  for (int i = 0; i < n; i++) {
    int open_now = 0;
    uint32_t g = snes_pause_menu_filter_guest_input(word, &open_now);
    seen |= g;
    if (bit_frames && (g & bit)) (*bit_frames)++;
    if (opened && open_now) (*opened)++;
  }
  return seen;
}

static void nav(uint32_t word, uint32_t *t) {
  snes_pause_menu_poll_nav(word, (*t)++);
  snes_pause_menu_poll_nav(0, (*t)++);
}

int main(void) {
  uint32_t t = 1000;
  int frames, opened;
  snes_pause_menu_configure("Test Game", 1, 1);

  /* Other buttons pass straight through. */
  CHECK(feed(SNES_PAD_B | SNES_PAD_RIGHT, 1, 0, NULL, NULL) == (SNES_PAD_B | SNES_PAD_RIGHT));
  feed(0, D, 0, NULL, NULL);

  /* A lone Start tap arrives whole, D frames late. */
  frames = 0; opened = 0;
  CHECK((feed(START, 2, START, &frames, &opened) & START) == 0 || D < 2);
  feed(0, D + 2, START, &frames, &opened);
  CHECK(frames == 2);
  CHECK(opened == 0);
  CHECK(!snes_pause_menu_is_open());

  /* Select, then Start two frames later: the menu opens, neither reaches
   * the guest, then or after. */
  frames = 0; opened = 0;
  feed(SEL, 2, SEL | START, &frames, &opened);
  feed(SEL | START, 3, SEL | START, &frames, &opened);
  CHECK(opened == 1);
  CHECK(snes_pause_menu_is_open());

  /* Navigation: the chord is still held at open, so nothing moves yet. */
  snes_pause_menu_poll_nav(SEL | START, t++);
  snes_pause_menu_poll_nav(0, t++);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_NONE);
  nav(SNES_PAD_DOWN, &t);                 /* SAVE */
  nav(SNES_PAD_A, &t);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_SAVE);
  CHECK(snes_pause_menu_is_open());       /* the host comes back here */
  nav(SNES_PAD_DOWN, &t);                 /* LOAD */
  nav(SNES_PAD_DOWN, &t);                 /* OPTIONS */
  nav(SNES_PAD_A, &t);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_OPTIONS);
  nav(SNES_PAD_DOWN, &t);                 /* BACK TO MENU */
  nav(SNES_PAD_A, &t);                    /* asks first, NO preselected */
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_NONE);
  nav(SNES_PAD_A, &t);                    /* NO: back to the list */
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_NONE);
  CHECK(snes_pause_menu_is_open());
  nav(SNES_PAD_A, &t);                    /* asks again */
  nav(SNES_PAD_B, &t);                    /* B answers NO */
  CHECK(snes_pause_menu_is_open());
  nav(SNES_PAD_A, &t);
  nav(SNES_PAD_RIGHT, &t);                /* YES */
  nav(SNES_PAD_A, &t);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_TITLE);
  CHECK(!snes_pause_menu_is_open());

  /* Up from RESUME wraps to BACK TO LAUNCHER; it asks too. */
  feed(0, D, 0, NULL, NULL);
  opened = 0;
  feed(SEL | START, 1, 0, NULL, &opened);
  CHECK(opened == 1);
  nav(0, &t);
  nav(SNES_PAD_UP, &t);
  nav(SNES_PAD_A, &t);
  nav(SNES_PAD_LEFT, &t);
  nav(SNES_PAD_A, &t);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_QUIT);

  /* B resumes; a chord still held when it closes neither reopens it nor
   * reaches the guest until it is let go. */
  feed(0, D, 0, NULL, NULL);
  feed(SEL | START, 1, 0, NULL, NULL);
  CHECK(snes_pause_menu_is_open());
  snes_pause_menu_poll_nav(SEL | START, t++);
  snes_pause_menu_poll_nav(SEL | START | SNES_PAD_B, t++);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_RESUME);
  CHECK(!snes_pause_menu_is_open());
  frames = 0; opened = 0;
  feed(SEL | START, 10, SEL | START, &frames, &opened);
  CHECK(frames == 0 && opened == 0);
  feed(0, D, SEL | START, &frames, &opened);
  CHECK(frames == 0);

  /* Holding Start (the game's pause) and pressing Select later still opens
   * it; the Start already delivered stays delivered. */
  frames = 0; opened = 0;
  feed(START, D + 3, START, &frames, &opened);
  CHECK(frames == 3);
  feed(START | SEL, 1, SEL, &frames, &opened);
  CHECK(opened == 1);
  snes_pause_menu_close();

  {
    const uint32_t *px; int w, h;
    CHECK(!snes_pause_menu_overlay_image(&px, &w, &h));
    feed(0, D, 0, NULL, NULL);
    feed(SEL | START, 1, 0, NULL, NULL);
    CHECK(snes_pause_menu_overlay_image(&px, &w, &h) && w == SNES_PM_W && h == SNES_PM_H);
    CHECK(px[0] == 0);                    /* transparent outside the box */
    CHECK(px[(h / 2) * w + w / 2] >> 24 == 0xFF);
    snes_pause_menu_close();
  }

  puts(g_fail ? "pause_menu: FAIL" : "pause_menu: ok");
  return g_fail;
}
