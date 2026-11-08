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

/* A game style: counts its callbacks. */
static int g_style_box, g_style_text, g_style_cursor;
static void style_box(uint32_t *p, int s, int h, int x, int y, int w, int bh) {
  (void)p; (void)s; (void)h; (void)x; (void)y; (void)w; (void)bh; g_style_box++;
}
static void style_text(uint32_t *p, int s, int h, int x, int y, const char *t, uint32_t c, int sc) {
  (void)p; (void)s; (void)h; (void)x; (void)y; (void)t; (void)c; (void)sc; g_style_text++;
}
static void style_cursor(uint32_t *p, int s, int h, int x, int y, int rh) {
  (void)p; (void)s; (void)h; (void)x; (void)y; (void)rh; g_style_cursor++;
}

/* A fake OPTIONS source: heading, VOLUME, heading, a feature whose option
 * row exists only while the feature is on. */
static int g_volume = 50, g_feature, g_choice, g_changes;
static int fake_count(void) { return g_feature ? 5 : 4; }
static int fake_get(int i, SnesPauseOptionRow *r) {
  switch (i) {
  case 0: snprintf(r->label, sizeof(r->label), "SOUND"); r->heading = 1; return 1;
  case 1: snprintf(r->label, sizeof(r->label), "VOLUME");
          snprintf(r->value, sizeof(r->value), "%d%%", g_volume); return 1;
  case 2: snprintf(r->label, sizeof(r->label), "DISPLAY"); r->heading = 1; return 1;
  case 3: snprintf(r->label, sizeof(r->label), "WIDESCREEN WITH A VERY LONG NAME INDEED");
          snprintf(r->value, sizeof(r->value), "%s", g_feature ? "ON" : "OFF");
          snprintf(r->help, sizeof(r->help), "Fills the screen. Presentation only, the "
                   "game plays the same; this help text is long enough to wrap twice.");
          return 1;
  case 4: if (!g_feature) return 0;
          snprintf(r->label, sizeof(r->label), "SHAPE"); r->indent = 1;
          snprintf(r->value, sizeof(r->value), "%d", g_choice); return 1;
  }
  return 0;
}
static int fake_change(int i, int delta) {
  g_changes++;
  if (i == 1) g_volume += 10 * delta;
  if (i == 3) g_feature = !g_feature;
  if (i == 4) g_choice += delta;
  return i;
}
static const char *fake_note(void) { return g_changes ? "RESTARTS THE GAME" : NULL; }
static const SnesPauseOptionsSource g_fake = { fake_count, fake_get, fake_change, fake_note };

int main(void) {
  uint32_t t = 1000;
  int frames, opened;
  snes_pause_menu_configure("Test Game", &g_fake, 1);

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
  /* The OPTIONS page: VOLUME first (the heading is skipped). */
  nav(SNES_PAD_RIGHT, &t);
  CHECK(g_volume == 60);
  nav(SNES_PAD_LEFT, &t);
  nav(SNES_PAD_LEFT, &t);
  CHECK(g_volume == 40);
  {   /* Right held: one step, then repeat after the delay. */
    uint32_t t0 = t;
    snes_pause_menu_poll_nav(SNES_PAD_RIGHT, t0);
    snes_pause_menu_poll_nav(SNES_PAD_RIGHT, t0 + SNES_OVL_REPEAT_DELAY + 1);
    snes_pause_menu_poll_nav(0, t0 + SNES_OVL_REPEAT_DELAY + 2);
    t = t0 + SNES_OVL_REPEAT_DELAY + 3;
    CHECK(g_volume == 60);
  }
  nav(SNES_PAD_DOWN, &t);                 /* WIDESCREEN (skips DISPLAY) */
  nav(SNES_PAD_A, &t);
  CHECK(g_feature == 1);
  nav(SNES_PAD_DOWN, &t);                 /* SHAPE, there now */
  nav(SNES_PAD_RIGHT, &t);
  CHECK(g_choice == 1);
  {
    const uint32_t *px; int w, h;
    CHECK(snes_pause_menu_overlay_image(&px, &w, &h) && px[(h / 2) * w + w / 2] >> 24 == 0xFF);
  }
  nav(SNES_PAD_UP, &t);                   /* WIDESCREEN */
  nav(SNES_PAD_A, &t);                    /* off: SHAPE goes */
  CHECK(g_feature == 0);
  nav(SNES_PAD_DOWN, &t);                 /* wraps to VOLUME */
  nav(SNES_PAD_RIGHT, &t);
  CHECK(g_volume == 70);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_NONE);
  nav(SNES_PAD_B, &t);                    /* B leaves the page, not the menu */
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_OPTIONS_DONE);
  CHECK(snes_pause_menu_is_open());
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

  /* REWIND: listed second while the host says the ring is on; choosing it
   * keeps the menu open (the host runs the filmstrip and comes back). */
  snes_pause_menu_set_rewind(1);
  snes_pause_menu_open();
  snes_pause_menu_rearm(0);
  nav(SNES_PAD_DOWN, &t);
  nav(SNES_PAD_A, &t);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_REWIND);
  CHECK(snes_pause_menu_is_open());
  snes_pause_menu_close();
  snes_pause_menu_set_rewind(0);
  snes_pause_menu_open();
  snes_pause_menu_rearm(0);
  nav(SNES_PAD_DOWN, &t);
  nav(SNES_PAD_A, &t);
  CHECK(snes_pause_menu_take_action() == SNES_PAUSE_SAVE);
  snes_pause_menu_close();

  /* A style: its callbacks draw, alpha-0 colours are skipped, and NULL
   * puts the stock look back. */
  {
    static const SnesPauseMenuStyle style = {
      .col_bg = 0, .col_head = 0, .col_edge = 0, .col_accent = 0xFF102030u,
      .col_text = 0xFF405060u, .col_dim = 0xFF708090u,
      .box = style_box, .text = style_text, .cursor = style_cursor,
    };
    const uint32_t *px; int w, h;
    snes_pause_menu_set_style(&style);
    snes_pause_menu_open();
    CHECK(snes_pause_menu_overlay_image(&px, &w, &h));
    CHECK(g_style_box == 1 && g_style_text > 3 && g_style_cursor == 1);
    CHECK(px[(h / 2) * w + w / 2] == 0);   /* no fill, nothing drawn there */
    snes_pause_menu_close();
    /* Alpha-0 text is skipped, not drawn as holes. */
    {
      static const SnesPauseMenuStyle hidden = {
        .col_bg = 0xFF112233u, .col_accent = 0, .col_text = 0, .col_dim = 0,
      };
      int y, x, holes = 0;
      snes_pause_menu_set_style(&hidden);
      snes_pause_menu_open();
      CHECK(snes_pause_menu_overlay_image(&px, &w, &h));
      for (y = h / 2 - 60; y < h / 2 + 60; y++)
        for (x = w / 2 - 100; x < w / 2 + 100; x++)
          holes += px[y * w + x] != 0xFF112233u;
      CHECK(holes == 0);
      snes_pause_menu_close();
    }
    snes_pause_menu_set_style(NULL);
    snes_pause_menu_open();
    CHECK(snes_pause_menu_overlay_image(&px, &w, &h));
    CHECK(px[(h / 2) * w + w / 2] >> 24 == 0xFF);
    CHECK(g_style_box == 1);
    snes_pause_menu_close();
  }

  puts(g_fail ? "pause_menu: FAIL" : "pause_menu: ok");
  return g_fail;
}
