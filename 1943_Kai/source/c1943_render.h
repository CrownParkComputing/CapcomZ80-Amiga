/* c1943_render.h -- 1943 software renderer -> chunky 8-bit + 256-colour palette.
 * Native (un-rotated) MAME bitmap: 256 wide x 224 tall. The Amiga RTG presenter
 * applies the ROT270 cabinet rotation at blit time (Gaplus pattern). */
#ifndef C1943_RENDER_H
#define C1943_RENDER_H

#include <stdint.h>
#include "z80emu.h"

#define C1943_NW 256   /* native width  (MAME visible x 0..255) */
#define C1943_NH 224   /* native height (MAME visible y 16..239) */

/* gfx/PROM region pointers (decoded on the fly; MAME gfxdecode regions):
 *   gfx1  chars     0x8000  (8x8 2bpp)
 *   gfx2  bg1 tiles 0x40000 (32x32 4bpp, RGN_FRAC 1/2)
 *   gfx3  bg2 tiles 0x10000 (32x32 4bpp, RGN_FRAC 1/2)
 *   gfx4  sprites   0x40000 (16x16 4bpp, RGN_FRAC 1/2)
 *   tilerom         0x10000 (bm14 @0 = bg1 map, bm23 @0x8000 = bg2 map)
 *   proms           0x0c00  (RGB + per-layer lookup/bank tables) */
void c1943_render_init(const uint8_t *gfx1, const uint8_t *gfx2,
                       const uint8_t *gfx3, const uint8_t *gfx4,
                       const uint8_t *tilerom, const uint8_t *proms);

/* Optional cache allocator. The Amiga RTG build installs a MEMF_FAST allocator
 * before c1943_render_init() so the decoded tile/sprite caches do not land in
 * slow memory. Host tools can ignore this and fall back to calloc/free. */
void c1943_render_set_allocators(void *(*alloc_fn)(uint32_t bytes),
                                 void (*free_fn)(void *ptr, uint32_t bytes));

void c1943_render_shutdown(void);

/* Runtime speed lever used by the RTG PMiga diagnostics. Disabling parallax skips
 * the front bg1 full-screen overlay while keeping bg2, sprites, and HUD. */
void c1943_render_set_parallax(int on);

/* Build the 256-entry indirect RGB palette (the renderer outputs indices into it). */
void c1943_build_palette(uint8_t pal[256][3]);

/* Render one frame to the native chunky framebuffer. */
void c1943_render_frame(MY_LITTLE_Z80 *z, uint8_t fb[C1943_NH][C1943_NW]);

#endif /* C1943_RENDER_H */
