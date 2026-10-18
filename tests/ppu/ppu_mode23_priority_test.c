/* Modes 2 and 3 rank the backgrounds against the sprites differently from
 * mode 1: front to back OBJ3, BG1.hi, OBJ2, BG2.hi, OBJ1, BG1.lo, OBJ0,
 * BG2.lo. The line renderer used mode 1's order, so priority 0-2 sprites
 * vanished behind BG2 (Super Empire Strikes Back's mode-2 mountain band).
 * The legacy per-pixel renderer has the hardware order; this compares the
 * two on pseudo-random VRAM (tilemaps with mixed priority bits) with one
 * sprite of each priority per line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes/ppu.h"
#include "snes/snes.h"

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
uint16_t WsShadowTile(int layer, int x, uint32_t y, uint16_t tile) {
  (void)layer; (void)x; (void)y; return tile;
}
bool WsShadowLayerActive(int layer) { (void)layer; return false; }
uint32_t WsShadowWorldX(int layer) { (void)layer; return 0; }
uint32_t WsShadowPresentWorldY(int layer, int x) { (void)layer; (void)x; return 0; }
uint32_t WsShadowScrollY(int layer) { (void)layer; return 0; }
void WsShadowOnVramWrite(uint16_t address, uint16_t value) { (void)address; (void)value; }

enum { kLines = 24 };
static uint32_t out[2][kLines + 1][kPpuBufWidth];
static Ppu p;
static uint32_t s_rng;

static uint16_t Rand16(void) {
  s_rng = s_rng * 1103515245u + 12345u;
  return (uint16_t)(s_rng >> 8);
}

static void render(unsigned mode, unsigned seed, int flags) {
  memset(&p, 0, sizeof(p));
  ppu_reset(&p);
  s_rng = seed;
  p.inidisp = 15;
  p.bgmode = (uint8_t)mode;
  p.screenEnabled[0] = 0x13;  /* BG1, BG2, OBJ */
  p.bgXsc[0] = 0x00;          /* BG1 map at word $0000 */
  p.bgXsc[1] = 0x04;          /* BG2 map at $0400 */
  p.bgXsc[2] = 0x08;          /* BG3 (mode 2 offset-per-tile table) at $0800 */
  p.bgTileAdr = 0x21;         /* BG1 tiles at $1000, BG2 at $2000 */
  p.obsel = 3;                /* sprite tiles at $6000 */
  for (int i = 0; i < 0x8000; ++i) p.vram[i] = Rand16();
  /* Tile entries: keep the tile numbers small, so they stay inside the tile
   * data written above, and no flips; priority bits and palettes random. */
  for (int i = 0; i < 0x800; ++i) p.vram[i] &= 0x3c3f;
  memset(&p.vram[0x800], 0, 0x800 * 2);  /* no offset-per-tile */
  for (int i = 0; i < 256; ++i) p.cgram[i] = Rand16() & 0x7fff;
  for (int i = 0; i < 128; ++i) p.oam[2 * i] = 0xf000;
  memset(p.highOam, 0, sizeof(p.highOam));
  /* Four 8x8 sprites, priorities 0-3, side by side on lines 1-8 and again,
   * shifted, on lines 9-16, over random tile data. */
  for (int i = 0; i < 8; ++i) {
    p.oam[2 * i] = (uint16_t)((i < 4 ? 0 : 8) << 8 | (16 + 40 * (i & 3) + 4 * (i >> 2)));
    p.oam[2 * i + 1] = (uint16_t)((i & 3) << 12 | (i & 7) << 9 | (i * 3));
  }
  PpuBeginDrawing(&p, (uint8_t *)out[flags ? 0 : 1][0], kPpuBufWidth * 4, flags);
  for (int y = 0; y <= kLines; ++y) ppu_runLine(&p, y);
}

int main(void) {
  int checked = 0;
  for (unsigned mode = 2; mode <= 3; ++mode) {
    for (unsigned seed = 1; seed <= 64; ++seed) {
      render(mode, seed, kPpuRenderFlags_NewRenderer);
      render(mode, seed, 0);
      /* ppu_runLine(y) draws picture row y-1; compare rows 0..kLines-1. */
      for (int y = 0; y < kLines; ++y)
        for (int x = 0; x < 256; ++x) {
          const uint32_t a = out[0][y][x] & 0xffffff, b = out[1][y][x] & 0xffffff;
          if (a != b) {
            fprintf(stderr, "mode %u seed %u line %d x=%d: line renderer %06x, "
                    "legacy %06x\n", mode, seed, y, x, a, b);
            return 1;
          }
          ++checked;
        }
    }
  }
  printf("ppu_mode23_priority_test: PASS (%d pixels)\n", checked);
  return 0;
}
