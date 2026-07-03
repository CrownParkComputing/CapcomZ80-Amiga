/* src/hal/c1942_render.h -- 1942 -> Amiga 8-bitplane renderer (pure C).
 * 1942 has 256 indirect colours -> 8 AGA planes. Rendered UPRIGHT (ROT270):
 * the 256x224 hardware bitmap is rotated into a 224-wide x 256-tall image,
 * centred in the 320x256 planes. The pen written to the planes is the LUT
 * indirect colour index; one static 256-entry palette serves all layers. */
#ifndef C1942_RENDER_H
#define C1942_RENDER_H
#include <stdint.h>
#include "z80emu.h"

#define C_W        320
#define C_H        256
#define C_ROW      40
#define C_PLANE    (C_ROW*C_H)     /* 10240 */
#define C_NPLANES  8
#define C_GW       256             /* game bitmap width  (sx) */
#define C_GH       224             /* game bitmap height (sy) */
#define C_XOFF     48              /* upright image: (320-224)/2 */

extern uint8_t *c1942_planes;
extern void (*c1942_clear_hook)(uint8_t *planes);

void c1942_build_palette(uint8_t rgb[256][3]);   /* static 256-colour palette */
int  c1942_render_planes(MY_LITTLE_Z80 *z);      /* composite+rotate -> planes; returns visible-content gauge */

#endif
