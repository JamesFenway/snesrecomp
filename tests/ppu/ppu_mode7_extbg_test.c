/* Mode 7 EXTBG ($2133 bit 6): the line renderer must draw BG2 from the Mode 7
 * pixels (colour = bits 0-6, priority = bit 7) the way the legacy per-pixel
 * renderer does. It used to draw only BG1, so a floor put on BG2 was black. */
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

#define CHECK(test) do { if (!(test)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); \
} } while (0)

static uint32_t out[2][kPpuBufWidth];
static Ppu p;

static void render(unsigned main_layers, unsigned setini, int flags, uint32_t *dst) {
  memset(&p, 0, sizeof(p));
  ppu_reset(&p);
  p.inidisp = 15;
  p.bgmode = 7;
  p.setini = (uint8_t)setini;
  p.screenEnabled[0] = (uint8_t)main_layers;
  p.m7matrix[0] = p.m7matrix[3] = 256;
  for (int i = 0; i < 128; ++i) p.oam[2 * i] = 0xf000;
  /* Map: every tile is tile 1. Tile 1: pixel colours 1..64, every other one
   * with the priority bit set; pixel 0 transparent. */
  for (int i = 0; i < 0x4000; ++i) p.vram[i] = 1;
  for (unsigned i = 0; i < 64; ++i) {
    unsigned px = i ? (i | ((i & 1) << 7)) : 0;
    p.vram[64 + i] = (uint16_t)((p.vram[64 + i] & 0xff) | px << 8);
  }
  for (unsigned i = 1; i < 256; ++i) p.cgram[i] = (uint16_t)(i * 0x0421 + 7);
  memset(dst, 0, sizeof(out[0]));
  PpuBeginDrawing(&p, (uint8_t *)dst, kPpuBufWidth * 4, flags);
  ppu_runLine(&p, 0);
  ppu_runLine(&p, 1);
}

static int nonblack(const uint32_t *row) {
  int n = 0;
  for (int x = 0; x < 256; ++x) n += (row[x] & 0xffffff) != 0;
  return n;
}

int main(void) {
  static const struct { unsigned layers, setini; } cases[] = {
    {0x01, 0x00}, {0x01, 0x40},  /* BG1 alone, EXTBG off/on */
    {0x02, 0x40},                /* BG2 alone: the black-floor case */
    {0x03, 0x40},                /* both: BG2.hi over BG1 over BG2.lo */
    {0x02, 0x00},                /* BG2 without EXTBG draws nothing */
  };
  for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
    render(cases[c].layers, cases[c].setini, kPpuRenderFlags_NewRenderer, out[0]);
    render(cases[c].layers, cases[c].setini, 0, out[1]);
    for (int x = 0; x < 256; ++x)
      if (out[0][x] != out[1][x]) {
        fprintf(stderr, "case %u x=%d: line renderer %06x, legacy %06x\n",
                c, x, out[0][x] & 0xffffff, out[1][x] & 0xffffff);
        exit(1);
      }
    if (cases[c].layers & 1 || cases[c].setini)
      CHECK(nonblack(out[0]) > 200);
    else
      CHECK(nonblack(out[0]) == 0);
  }
  /* BG2 alone: colour comes from bits 0-6 only. */
  render(0x02, 0x40, kPpuRenderFlags_NewRenderer, out[0]);
  render(0x01, 0x40, kPpuRenderFlags_NewRenderer, out[1]);
  CHECK(memcmp(out[0], out[1], 256 * 4) != 0);
  printf("ppu_mode7_extbg_test: PASS\n");
  return 0;
}
