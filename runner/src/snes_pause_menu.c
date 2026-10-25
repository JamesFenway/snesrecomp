/*
 * snes_pause_menu.c — the in-game pause menu. See the header for the
 * contract; the host's half is RunPauseMenuLoop in desktop/host_main.c.
 *
 * Software-rasterized into a plain ARGB buffer like the save-state browser,
 * with no dependency on a renderer.
 */
#include "snes_pause_menu.h"
#include "snes_overlay_draw.h"
#include "desktop/sdl_compat.h"
#include <stdio.h>
#include <string.h>

#define PM_W SNES_PM_W
#define PM_H SNES_PM_H
#define PM_CHORD (SNES_PAD_SELECT | SNES_PAD_START)

#define PM_BOX_W 336
#define PM_ROW_H 34
#define PM_TEXT_SCALE 2

/* Trilogy gold on the save-state browser's dark blue. */
#define PM_COL_BG     0xFF0F1118u
#define PM_COL_HEAD   0xFF171B25u
#define PM_COL_EDGE   0xFF303746u
#define PM_COL_GOLD   0xFFFFD24Du
#define PM_COL_TEXT   0xFFE2E5EBu
#define PM_COL_DIM    0xFF7F8796u
#define PM_COL_SEL_BG 0xFF2B2830u

typedef struct PmItem {
  const char *label;
  SnesPauseAction action;
} PmItem;

static PmItem s_items[8];
static int s_item_count;
static char s_title[40] = "PAUSED";

static const SnesPauseOptionsSource *s_options;

static int s_open;
static int s_selected;
/* The OPTIONS page is up; s_opt_sel is its selected row, s_opt_top the
 * first row shown. */
static int s_on_options;
static int s_opt_sel, s_opt_top;
/* 0 = the item list; 1 = "are you sure" for s_confirm_action. */
static int s_confirming;
static int s_confirm_yes;
static SnesPauseAction s_confirm_action;
static SnesPauseAction s_action;
static char s_status[48];
static int s_dirty = 1;
static uint32_t s_panel[PM_W * PM_H];

/* Navigation edges and repeat, as in the save-state browser. */
static uint32_t s_nav_prev;
static uint32_t s_nav_skip;   /* held at open / rearm: no edge until released */
static uint32_t s_repeat_next;
static int s_repeat_dir;

/* Chord detector and the Select/Start delay line. */
static uint32_t s_chord_prev;
static uint32_t s_chord_guard;
static uint32_t s_line[SNES_PAUSE_CHORD_FRAMES];
static int s_line_pos;

void snes_pause_menu_configure(const char *title, const SnesPauseOptionsSource *options,
                               int to_launcher)
{
  int n = 0;
  if (title && title[0]) {
    size_t i;
    snprintf(s_title, sizeof(s_title), "%s", title);
    for (i = 0; s_title[i]; i++)
      if (s_title[i] >= 'a' && s_title[i] <= 'z') s_title[i] = (char)(s_title[i] - 32);
  }
  s_items[n++] = (PmItem){ "RESUME", SNES_PAUSE_RESUME };
  s_items[n++] = (PmItem){ "SAVE", SNES_PAUSE_SAVE };
  s_items[n++] = (PmItem){ "LOAD", SNES_PAUSE_LOAD };
  s_options = options && options->count && options->get && options->change ? options : NULL;
  if (s_options) s_items[n++] = (PmItem){ "OPTIONS", SNES_PAUSE_OPTIONS };
  s_items[n++] = (PmItem){ "BACK TO MENU", SNES_PAUSE_TITLE };
  s_items[n++] = (PmItem){ to_launcher ? "BACK TO LAUNCHER" : "QUIT GAME", SNES_PAUSE_QUIT };
  s_item_count = n;
  s_dirty = 1;
}

int snes_pause_menu_is_open(void)
{
  return s_open;
}

void snes_pause_menu_open(void)
{
  if (s_open) return;
  if (!s_item_count) snes_pause_menu_configure(NULL, NULL, 0);
  s_open = 1;
  s_selected = 0;
  s_confirming = 0;
  s_on_options = 0;
  s_action = SNES_PAUSE_NONE;
  s_status[0] = '\0';
  s_repeat_dir = 0;
  s_nav_skip = ~0u;   /* the chord or key that opened it is still down */
  s_dirty = 1;
}

void snes_pause_menu_close(void)
{
  s_open = 0;
  s_confirming = 0;
  s_on_options = 0;
  s_repeat_dir = 0;
  /* Whatever closed the menu stays out of the game until it is let go. */
  s_chord_guard |= s_nav_prev;
  s_dirty = 1;
}

void snes_pause_menu_rearm(uint32_t held)
{
  s_nav_prev = held;
  s_nav_skip = held;
  s_repeat_dir = 0;
  s_dirty = 1;
}

void snes_pause_menu_set_status(const char *text)
{
  snprintf(s_status, sizeof(s_status), "%s", text ? text : "");
  s_dirty = 1;
}

SnesPauseAction snes_pause_menu_take_action(void)
{
  SnesPauseAction a = s_action;
  s_action = SNES_PAUSE_NONE;
  return a;
}

uint32_t snes_pause_menu_filter_guest_input(uint32_t inputs, int *open_now)
{
  const uint32_t chord = inputs & PM_CHORD;
  uint32_t delayed;
  if (open_now) *open_now = 0;
  /* A guarded button leaves the guard once released. */
  s_chord_guard &= inputs;
  if (!s_open && chord == PM_CHORD && (s_chord_prev & PM_CHORD) != PM_CHORD &&
      (s_chord_guard & PM_CHORD) == 0) {
    /* The lone press still in the line belongs to the chord: drop it. */
    memset(s_line, 0, sizeof(s_line));
    s_chord_guard |= PM_CHORD;
    s_chord_prev = inputs;
    snes_pause_menu_open();
    s_nav_prev = inputs;
    if (open_now) *open_now = 1;
    return inputs & ~PM_CHORD;
  }
  s_chord_prev = inputs;
  delayed = s_line[s_line_pos];
  s_line[s_line_pos] = chord & ~s_chord_guard;
  s_line_pos = (s_line_pos + 1) % SNES_PAUSE_CHORD_FRAMES;
  return (inputs & ~PM_CHORD) | (delayed & ~s_chord_guard);
}

/* ── OPTIONS page ───────────────────────────────────────────────────────── */

static int opt_count(void)
{
  return s_options ? s_options->count() : 0;
}

static int opt_row(int i, SnesPauseOptionRow *r)
{
  memset(r, 0, sizeof(*r));
  return i >= 0 && i < opt_count() && s_options->get(i, r);
}

static int opt_selectable(int i)
{
  SnesPauseOptionRow r;
  return opt_row(i, &r) && !r.heading;
}

/* The selectable row nearest `from` going `dir` (and the other way if none). */
static int opt_settle(int from, int dir)
{
  const int n = opt_count();
  int i;
  if (n <= 0) return 0;
  if (from < 0) from = 0;
  if (from >= n) from = n - 1;
  for (i = from; i >= 0 && i < n; i += dir)
    if (opt_selectable(i)) return i;
  for (i = from; i >= 0 && i < n; i -= dir)
    if (opt_selectable(i)) return i;
  return from;
}

static void opt_move(int dir)
{
  const int n = opt_count();
  int i = s_opt_sel + dir;
  while (i >= 0 && i < n && !opt_selectable(i)) i += dir;
  if (i < 0 || i >= n) {
    /* Wrap to the far end. */
    i = opt_settle(dir > 0 ? 0 : n - 1, dir);
  }
  s_opt_sel = i;
  s_dirty = 1;
}

static void opt_change(int delta)
{
  SnesPauseOptionRow r;
  if (!opt_row(s_opt_sel, &r) || r.heading || r.disabled) return;
  s_options->change(s_opt_sel, delta);
  /* A feature switched on or off adds or removes the rows under it. */
  s_opt_sel = opt_settle(s_opt_sel, 1);
  s_dirty = 1;
}

static void move(int delta)
{
  if (s_on_options) {
    /* +-1 = Up / Down, +-2 = Left / Right (change the row). */
    if (delta == 2 || delta == -2) opt_change(delta / 2);
    else opt_move(delta);
    return;
  }
  if (s_confirming) {
    s_confirm_yes = !s_confirm_yes;
  } else {
    s_selected = (s_selected + delta + s_item_count) % s_item_count;
    s_status[0] = '\0';
  }
  s_dirty = 1;
}

static void choose(void)
{
  if (s_on_options) {
    opt_change(+1);
    return;
  }
  if (s_confirming) {
    SnesPauseAction a = s_confirm_action;
    s_confirming = 0;
    s_dirty = 1;
    if (s_confirm_yes) {
      s_action = a;
      snes_pause_menu_close();
    }
    return;
  }
  {
    SnesPauseAction a = s_items[s_selected].action;
    s_status[0] = '\0';
    s_dirty = 1;
    if (a == SNES_PAUSE_TITLE || a == SNES_PAUSE_QUIT) {
      s_confirming = 1;
      s_confirm_yes = 0;
      s_confirm_action = a;
      return;
    }
    s_action = a;
    if (a == SNES_PAUSE_RESUME) snes_pause_menu_close();
    if (a == SNES_PAUSE_OPTIONS) {
      s_on_options = 1;
      s_opt_top = 0;
      s_opt_sel = opt_settle(0, 1);
    }
  }
}

static void back(void)
{
  if (s_on_options) {
    s_on_options = 0;
    s_action = SNES_PAUSE_OPTIONS_DONE;
    s_dirty = 1;
    return;
  }
  if (s_confirming) {
    s_confirming = 0;
    s_dirty = 1;
    return;
  }
  s_action = SNES_PAUSE_RESUME;
  snes_pause_menu_close();
}

void snes_pause_menu_poll_nav(uint32_t inputs, uint32_t ticks_ms)
{
  uint32_t pressed;
  int dir;
  if (!s_open) {
    s_nav_prev = inputs;
    return;
  }
  s_nav_skip &= inputs;
  pressed = inputs & ~s_nav_prev & ~s_nav_skip;
  s_nav_prev = inputs;

  if ((inputs & PM_CHORD) == PM_CHORD && (pressed & PM_CHORD)) {
    /* The chord resumes from anywhere; leaving the page is reported first,
     * so the host still applies what changed on it. */
    s_confirming = 0;
    if (s_on_options) back();
    back();
    return;
  }
  if (pressed & SNES_PAD_B) { back(); return; }
  if (pressed & (SNES_PAD_A | SNES_PAD_START)) { choose(); return; }

  dir = (inputs & ~s_nav_skip & SNES_PAD_DOWN) ? 1
      : (inputs & ~s_nav_skip & SNES_PAD_UP) ? -1 : 0;
  if (s_on_options) {
    /* Left / Right change the row, with the same repeat as Up / Down, so a
     * held Right walks the volume up. Encoded as +-2 to tell them apart. */
    const int lr = (inputs & ~s_nav_skip & SNES_PAD_RIGHT) ? 1
                 : (inputs & ~s_nav_skip & SNES_PAD_LEFT) ? -1 : 0;
    if (!dir && lr) dir = 2 * lr;
  } else if (s_confirming && !dir)
    dir = (inputs & ~s_nav_skip & SNES_PAD_RIGHT) ? 1
        : (inputs & ~s_nav_skip & SNES_PAD_LEFT) ? -1 : 0;
  if (dir == 0) {
    s_repeat_dir = 0;
    return;
  }
  if (dir != s_repeat_dir) {
    s_repeat_dir = dir;
    s_repeat_next = ticks_ms + SNES_OVL_REPEAT_DELAY;
    move(dir);
    return;
  }
  if (!s_confirming && (uint32_t)(ticks_ms - s_repeat_next) < 0x80000000u) {
    s_repeat_next = ticks_ms + SNES_OVL_REPEAT_RATE;
    move(dir);
  }
}

void snes_pause_menu_handle_key(int key, int repeat)
{
  if (!s_open || repeat) return;
  /* SDLK_* is unsigned under SDL3 and signed under SDL2. */
  if (key == (int)SDLK_ESCAPE || key == (int)SDLK_BACKSPACE)
    back();
}

/* ── Panel ─────────────────────────────────────────────────────────────── */

static void text_centered(int cx, int y, const char *s, uint32_t col, int scale)
{
  int w = (int)strlen(s) * 8 * scale;
  snes_ovl_draw_text(s_panel, PM_W, PM_H, cx - w / 2, y, s, col, scale);
}

/* The OPTIONS page. */
#define PO_BOX_W 480
#define PO_ROW_H 24
#define PO_ROWS_MAX 10
#define PO_HELP_LINES 4
#define PO_HELP_CHARS 56

static void opt_keep_visible(int n, int shown)
{
  SnesPauseOptionRow r;
  if (s_opt_sel < s_opt_top) {
    s_opt_top = s_opt_sel;
    /* Show the section title above the first row of a section. */
    if (s_opt_top > 0 && opt_row(s_opt_top - 1, &r) && r.heading) s_opt_top--;
  }
  if (s_opt_sel >= s_opt_top + shown) s_opt_top = s_opt_sel - shown + 1;
  if (s_opt_top > n - shown) s_opt_top = n - shown;
  if (s_opt_top < 0) s_opt_top = 0;
}

/* Text fitted to max_w pixels: scale 2 when it fits, else 1, cut with "..". */
static int fit_text(char *out, size_t cap, const char *s, int max_w)
{
  size_t n = strlen(s), keep;
  if ((int)n * 16 <= max_w) { snprintf(out, cap, "%s", s); return 2; }
  if ((int)n * 8 <= max_w) { snprintf(out, cap, "%s", s); return 1; }
  keep = max_w / 8 > 2 ? (size_t)(max_w / 8 - 2) : 0;
  if (keep >= cap - 3) keep = cap - 3;
  memcpy(out, s, keep);
  memcpy(out + keep, "..", 3);
  return 1;
}

/* Up to PO_HELP_LINES lines of `s`, broken at spaces, PO_HELP_CHARS wide. */
static void wrap_help(const char *s, char lines[PO_HELP_LINES][PO_HELP_CHARS + 1])
{
  int l;
  for (l = 0; l < PO_HELP_LINES; l++) lines[l][0] = '\0';
  for (l = 0; l < PO_HELP_LINES && *s; l++) {
    size_t len = strlen(s), cut = len;
    while (*s == ' ') s++, len--;
    cut = len;
    if (len > PO_HELP_CHARS) {
      cut = PO_HELP_CHARS;
      while (cut > 0 && s[cut] != ' ') cut--;
      if (cut == 0) cut = PO_HELP_CHARS;
      if (l == PO_HELP_LINES - 1) {   /* last line: cut with ".." */
        cut = PO_HELP_CHARS - 2;
        memcpy(lines[l], s, cut);
        memcpy(lines[l] + cut, "..", 3);
        return;
      }
    }
    memcpy(lines[l], s, cut);
    lines[l][cut] = '\0';
    s += cut;
  }
}

static void rasterize_options(void)
{
  const int n = opt_count();
  const int shown = n < PO_ROWS_MAX ? (n > 0 ? n : 1) : PO_ROWS_MAX;
  const int head_h = 44, help_h = 6 + PO_HELP_LINES * 12, note_h = 16, foot_h = 40;
  const int box_h = head_h + 8 + shown * PO_ROW_H + 8 + help_h + note_h + foot_h;
  const int bx = (PM_W - PO_BOX_W) / 2, by = (PM_H - box_h) / 2;
  const int cx = PM_W / 2;
  const char *note = s_options && s_options->note ? s_options->note() : NULL;
  SnesPauseOptionRow r, sel_row;
  char buf[80];
  int i, y;

  memset(&sel_row, 0, sizeof(sel_row));
  opt_keep_visible(n, shown);
  memset(s_panel, 0, sizeof(s_panel));
  snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx, by, PO_BOX_W, box_h, PM_COL_BG);
  snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx, by, PO_BOX_W, head_h, PM_COL_HEAD);
  snes_ovl_stroke_rect(s_panel, PM_W, PM_H, bx, by, PO_BOX_W, box_h, PM_COL_EDGE);
  text_centered(cx, by + 8, "OPTIONS", PM_COL_GOLD, 2);
  text_centered(cx, by + 28, s_title, PM_COL_DIM, 1);

  y = by + head_h + 8;
  for (i = s_opt_top; i < n && i < s_opt_top + shown; i++, y += PO_ROW_H) {
    const int sel = i == s_opt_sel;
    if (!opt_row(i, &r)) continue;
    if (sel) sel_row = r;
    if (r.heading) {
      snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 16, y + 12, r.label, PM_COL_GOLD, 1);
      snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx + 16 + (int)strlen(r.label) * 8 + 8, y + 15,
                         PO_BOX_W - 40 - (int)strlen(r.label) * 8 - 8, 1, PM_COL_EDGE);
      continue;
    }
    {
      const uint32_t col = r.disabled ? PM_COL_DIM : sel ? PM_COL_GOLD : PM_COL_TEXT;
      const int lx = bx + 24 + (r.indent ? 24 : 0);
      const int right = bx + PO_BOX_W - 20;
      char val[48];
      int vscale, vw, lscale;
      if (sel) {
        snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx + 10, y, PO_BOX_W - 20, PO_ROW_H - 2,
                           PM_COL_SEL_BG);
        snes_ovl_stroke_rect(s_panel, PM_W, PM_H, bx + 10, y, PO_BOX_W - 20, PO_ROW_H - 2,
                             PM_COL_GOLD);
      }
      snprintf(val, sizeof(val), (sel && !r.disabled) ? "< %s >" : "%s", r.value);
      vscale = (int)strlen(val) * 16 <= 176 ? 2 : 1;
      vw = (int)strlen(val) * 8 * vscale;
      snes_ovl_draw_text(s_panel, PM_W, PM_H, right - vw, y + (vscale == 2 ? 3 : 7), val,
                         col, vscale);
      lscale = fit_text(buf, sizeof(buf), r.label, right - vw - 16 - lx);
      snes_ovl_draw_text(s_panel, PM_W, PM_H, lx, y + (lscale == 2 ? 3 : 7), buf, col, lscale);
    }
  }
  /* Scroll marks. */
  if (s_opt_top > 0)
    snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + PO_BOX_W - 12, by + head_h + 2, "^",
                       PM_COL_DIM, 1);
  if (s_opt_top + shown < n)
    snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + PO_BOX_W - 12, y, "V", PM_COL_DIM, 1);

  y += 8;
  {
    char lines[PO_HELP_LINES][PO_HELP_CHARS + 1];
    int l;
    wrap_help(sel_row.help, lines);
    snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx + 16, y - 4, PO_BOX_W - 32, 1, PM_COL_EDGE);
    for (l = 0; l < PO_HELP_LINES; l++)
      snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 16, y + 4 + 12 * l, lines[l], PM_COL_DIM, 1);
  }
  y += help_h;
  if (s_status[0]) note = s_status;
  if (note && note[0]) text_centered(cx, y + 2, note, PM_COL_GOLD, 1);

  {
    const int fy = by + box_h - foot_h;
    snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx + 1, fy, PO_BOX_W - 2, foot_h - 1, PM_COL_HEAD);
    snes_ovl_draw_button(s_panel, PM_W, PM_H, bx + 20, fy + 11, 'A', SNES_OVL_COL_A);
    snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 44, fy + 16, "OR < > CHANGE", PM_COL_TEXT, 1);
    snes_ovl_draw_button(s_panel, PM_W, PM_H, bx + 176, fy + 11, 'B', SNES_OVL_COL_B);
    snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 200, fy + 16, "BACK", PM_COL_TEXT, 1);
    snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 264, fy + 16, "UP/DOWN PICK A ROW",
                       PM_COL_DIM, 1);
  }
  s_dirty = 0;
}

static void rasterize(void)
{
  if (s_on_options) {
    rasterize_options();
    return;
  }
  const int rows = s_confirming ? 3 : s_item_count;
  const int head_h = 44, foot_h = 40;
  const int box_h = head_h + 12 + rows * PM_ROW_H + 8 + (s_status[0] ? 18 : 0) + foot_h;
  const int bx = (PM_W - PM_BOX_W) / 2, by = (PM_H - box_h) / 2;
  const int cx = PM_W / 2;
  int i, y;

  memset(s_panel, 0, sizeof(s_panel));
  snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx, by, PM_BOX_W, box_h, PM_COL_BG);
  snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx, by, PM_BOX_W, head_h, PM_COL_HEAD);
  snes_ovl_stroke_rect(s_panel, PM_W, PM_H, bx, by, PM_BOX_W, box_h, PM_COL_EDGE);
  text_centered(cx, by + 8, "PAUSED", PM_COL_GOLD, 2);
  text_centered(cx, by + 28, s_title, PM_COL_DIM, 1);

  y = by + head_h + 12;
  if (s_confirming) {
    const char *q = s_confirm_action == SNES_PAUSE_QUIT
        ? "LEAVE THE GAME?" : "BACK TO THE TITLE?";
    text_centered(cx, y + 9, q, PM_COL_TEXT, 2);
    text_centered(cx, y + PM_ROW_H + 4, "UNSAVED PROGRESS WILL BE LOST", PM_COL_DIM, 1);
    {
      const int ry = y + 2 * PM_ROW_H;
      const int nx = cx - 96, yx = cx + 32;
      int k;
      for (k = 0; k < 2; k++) {
        const int sel = (k == 1) == (s_confirm_yes != 0);
        const int x = k ? yx : nx;
        if (sel) {
          snes_ovl_fill_rect(s_panel, PM_W, PM_H, x - 8, ry, 80, PM_ROW_H - 4, PM_COL_SEL_BG);
          snes_ovl_stroke_rect(s_panel, PM_W, PM_H, x - 8, ry, 80, PM_ROW_H - 4, PM_COL_GOLD);
        }
        snes_ovl_draw_text(s_panel, PM_W, PM_H, x + (k ? 8 : 16), ry + 7,
                           k ? "YES" : "NO", sel ? PM_COL_GOLD : PM_COL_TEXT, 2);
      }
    }
  } else {
    for (i = 0; i < s_item_count; i++) {
      const int ry = y + i * PM_ROW_H;
      const int sel = i == s_selected;
      if (sel) {
        snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx + 12, ry, PM_BOX_W - 24, PM_ROW_H - 4,
                           PM_COL_SEL_BG);
        snes_ovl_stroke_rect(s_panel, PM_W, PM_H, bx + 12, ry, PM_BOX_W - 24, PM_ROW_H - 4,
                             PM_COL_GOLD);
        snes_ovl_fill_disc(s_panel, PM_W, PM_H, bx + 30, ry + 15, 4, PM_COL_GOLD);
      }
      snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 48, ry + 7, s_items[i].label,
                         sel ? PM_COL_GOLD : PM_COL_TEXT, PM_TEXT_SCALE);
    }
  }
  y += rows * PM_ROW_H + 8;
  if (s_status[0]) {
    text_centered(cx, y, s_status, PM_COL_GOLD, 1);
    y += 18;
  }

  {
    const int fy = by + box_h - foot_h;
    snes_ovl_fill_rect(s_panel, PM_W, PM_H, bx + 1, fy, PM_BOX_W - 2, foot_h - 1, PM_COL_HEAD);
    snes_ovl_draw_button(s_panel, PM_W, PM_H, bx + 20, fy + 11, 'A', SNES_OVL_COL_A);
    snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 44, fy + 16, "CHOOSE", PM_COL_TEXT, 1);
    snes_ovl_draw_button(s_panel, PM_W, PM_H, bx + 116, fy + 11, 'B', SNES_OVL_COL_B);
    snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 140, fy + 16,
                       s_confirming ? "NO" : "RESUME", PM_COL_TEXT, 1);
    snes_ovl_draw_text(s_panel, PM_W, PM_H, bx + 212, fy + 16, "SELECT+START",
                       PM_COL_DIM, 1);
  }
  s_dirty = 0;
}

int snes_pause_menu_overlay_image(const uint32_t **pixels, int *w, int *h)
{
  if (!s_open) {
    if (pixels) *pixels = NULL;
    if (w) *w = 0;
    if (h) *h = 0;
    return 0;
  }
  if (s_dirty) rasterize();
  if (pixels) *pixels = s_panel;
  if (w) *w = PM_W;
  if (h) *h = PM_H;
  return 1;
}
