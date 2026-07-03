/* src/hal/c1943_render.h -- 1943 -> Amiga 8-bitplane renderer (256 indirect
 * colours, ROT90 upright; same geometry as 1942). */
#ifndef C1943_RENDER_H
#define C1943_RENDER_H
#include <stdint.h>
#include "z80emu.h"

/* Display = exactly the C_GH(224)px game content, no border (320->224 vs the
 * original): ~30% less bitplane DMA than the old 320px screen, no visual change
 * (content stays at the same screen X). 224 = 14 lores words. */
#define C_W        224
#define C_H        256
#define C_ROW      28              /* 224px / 8 */
#define C_PLANE    (C_ROW*C_H)
#define C_NPLANES  7              /* 7 planes (128 colors): pens remapped 0..127 */
#define C_GW       256             /* game width  (sx) */
#define C_GH       224             /* game height (sy) */
#define C_XOFF     0               /* content fills the display */

extern uint8_t *c1943_planes;
extern void (*c1943_clear_hook)(uint8_t *planes);

void c1943_build_palette(uint8_t rgb[256][3]);
int  c1943_render_planes(MY_LITTLE_Z80 *z);

#endif
