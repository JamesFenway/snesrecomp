#ifndef SNES_PAUSE_MENU_H
#define SNES_PAUSE_MENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * In-game pause menu (SnesDesktopHostGame.pause_menu): RESUME, SAVE, LOAD,
 * OPTIONS, BACK TO MENU and BACK TO LAUNCHER (QUIT GAME when no front end
 * started the game), over the frozen game.
 *
 * Opened with Select+Start pressed together on the pad, or the PauseMenu key
 * (Escape by default). Like the save-state browser, the guest is FROZEN while
 * it is open: the host stops calling RtlRunFrame, so RESUME brings the game
 * back bit-identical. The game's own Start pause is left alone.
 *
 * The chord must not leak into the game, but Select and Start rarely go down
 * on the same frame. So while the menu is enabled, those two bits reach the
 * guest SNES_PAUSE_CHORD_FRAMES late: a press of either alone arrives intact
 * (same length, a few frames later), and when the other joins inside that
 * window the menu opens and neither is ever delivered. Every other button is
 * untouched, and scripted / debug-server input is not filtered at all.
 *
 * This module only keeps the state and draws the panel; the host runs the
 * modal loop and carries out the action the player chose (see
 * snes_pause_menu_take_action), because only the host can open the
 * save-state browser or the launcher, reset the console or quit.
 */

#define SNES_PAUSE_CHORD_FRAMES 4

/* Panel size: the same 2x-the-field raster as the save-state browser, drawn
 * with a transparent background so the dimmed game shows around the box. */
#define SNES_PM_W 512
#define SNES_PM_H 448

typedef enum SnesPauseAction {
  SNES_PAUSE_NONE = 0,
  SNES_PAUSE_RESUME,
  SNES_PAUSE_SAVE,
  SNES_PAUSE_LOAD,
  SNES_PAUSE_OPTIONS,
  SNES_PAUSE_TITLE,    /* back to the game's title menu (confirmed) */
  SNES_PAUSE_QUIT,     /* back to the launcher, or quit (confirmed) */
  SNES_PAUSE_OPTIONS_DONE,  /* left the OPTIONS page (B), back on the list */
} SnesPauseAction;

/* The OPTIONS page: rows the host supplies (volume, mods, ...), drawn and
 * navigated here. Up/Down pick a row (headings are skipped), Left/Right
 * change it (-1 / +1), A changes it +1, B leaves the page with
 * SNES_PAUSE_OPTIONS_DONE. Rows are read again after every change, so a
 * change may add or remove rows (a feature's options appear when it is on). */
typedef struct SnesPauseOptionRow {
  char label[64];
  char value[40];
  char help[256];   /* shown under the list while the row is selected */
  int heading;      /* a section title: not selectable, no value */
  int indent;       /* an option under the feature above it */
  int disabled;     /* drawn grey, cannot change */
} SnesPauseOptionRow;

typedef struct SnesPauseOptionsSource {
  int  (*count)(void);
  int  (*get)(int index, SnesPauseOptionRow *out);
  /* Changes row `index`; returns the index that row has afterwards (rows
   * may come and go around it), or -1 when it is gone. */
  int  (*change)(int index, int delta);
  /* One line above the hints, or NULL / "" for none. */
  const char *(*note)(void);
} SnesPauseOptionsSource;

/* Once, before the first frame. `title` is shown on top (upper-cased; the
 * overlay font has A-Z, 0-9 and some punctuation). OPTIONS is listed only
 * with an options source; to_launcher picks BACK TO LAUNCHER over QUIT GAME.
 * The source must outlive the menu. */
void snes_pause_menu_configure(const char *title, const SnesPauseOptionsSource *options,
                               int to_launcher);

int  snes_pause_menu_is_open(void);
void snes_pause_menu_open(void);
void snes_pause_menu_close(void);

/* Per frame, on seat 0's human word, while the game runs. Returns the word
 * the guest should see (Select/Start delayed, see above) and sets *open_now
 * to 1 on the frame the chord opens the menu. */
uint32_t snes_pause_menu_filter_guest_input(uint32_t inputs, int *open_now);

/* Navigation while open: Up/Down move (with repeat), A or Start chooses,
 * B or Select+Start resumes. On BACK TO MENU / BACK TO LAUNCHER a NO / YES
 * question comes first (NO preselected; B answers NO). */
void snes_pause_menu_poll_nav(uint32_t inputs, uint32_t ticks_ms);

/* Keyboard keys that are not on the SNES word: Escape / Backspace go back
 * (out of the question or the OPTIONS page, else resume). */
void snes_pause_menu_handle_key(int key, int repeat);

/* The action chosen since the last call, then SNES_PAUSE_NONE again. The
 * menu stays open for SAVE / LOAD / OPTIONS (which opens the page) and
 * OPTIONS_DONE, so the host returns to it; it closes itself for RESUME,
 * TITLE and QUIT. */
SnesPauseAction snes_pause_menu_take_action(void);

/* One line under the items ("NO SAVE TO LOAD", ...); NULL clears it. */
void snes_pause_menu_set_status(const char *text);

/* Skip whatever the player still holds when the host returns to the menu
 * from the browser or the launcher, so the A that closed those does not
 * also choose an item here. */
void snes_pause_menu_rearm(uint32_t held);

/* ARGB8888, SNES_PM_W x SNES_PM_H, transparent outside the box. Returns 0
 * and nulls the outputs while closed. */
int  snes_pause_menu_overlay_image(const uint32_t **pixels, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif /* SNES_PAUSE_MENU_H */
