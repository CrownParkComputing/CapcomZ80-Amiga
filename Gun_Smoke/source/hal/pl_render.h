/* src/hal/pl_render.h -- Pac-Land tilemap+sprite -> Amiga 8-bitplane renderer.
 * Pure C (no Amiga headers): builds for m68k-amigaos-gcc AND the host test.
 * The Amiga video layer points pl_planes at an 8-plane chip buffer and uploads
 * the palette to the AGA COLOR registers; the host test reconstructs an image.
 *
 * Pac-Land shows 256 colours at once (one bank-switched window of the 1024-colour
 * PROM space) -> exactly 8 AGA bitplanes. The per-pixel "indirect index" (the LUT
 * output, 0..255) IS the AGA colour index; a single 256-entry palette, reloaded
 * only when the palette bank changes, serves fg+bg+sprites alike.
 */
#ifndef PL_RENDER_H
#define PL_RENDER_H
#include <stdint.h>

#define PL_W        320            /* bitplane width  (288 image, centred)  */
#define PL_H        256            /* bitplane height (224 image, centred)  */
#define PL_ROW      40             /* PL_W / 8                              */
#define PL_PLANE    (PL_ROW*PL_H)  /* 10240 bytes per plane                 */
#define PL_NPLANES  8
#define PL_IMG_W    288
#define PL_IMG_H    224
#define PL_XOFF     16             /* centre the 288 image in 320 ((320-288)/2);
                                    * was 16 (centred) but the image read offset
                                    * to the right -- 0 = flush left. Tune to taste. */
#define PL_YOFF     16             /* (256-224)/2 centring                  */

/* Render target: 8*PL_PLANE bytes. Set by the Amiga video layer (chip RAM) or
 * the host test (malloc). */
extern uint8_t *pl_planes;

/* Optional plane-clear hook (Amiga: blitter clear overlapping CPU emu). NULL on
 * host -> CPU memset fallback. */
extern void (*pl_clear_hook)(uint8_t *planes);

/* Provide the decoded gfx/PROM data (host: loaded ROMs; Amiga: embedded). */
void pl_render_set_gfx(const uint8_t *fg_chars, const uint8_t *bg_chars,
                       const uint8_t *sprites, const uint8_t *proms);

/* Build the 256-colour palette for `bank` (0..3) into rgb[256][3] (8-bit guns).
 * Call whenever pl_palette_bank() changes; upload result to AGA COLOR regs. */
void pl_build_palette(int bank, uint8_t rgb[256][3]);

/* Composite the current machine video state into pl_planes (8 planes).
 * Returns the number of non-empty 8px groups = a visible-content gauge. */
int  pl_render_planes(void);

/* Diagnostic: draw a static 256-colour grid into pl_planes (no machine state).
 * Exercises the clear->plane-write path; with the palette loaded it shows every
 * colour, proving the AGA 8-plane + palette pipeline end to end. */
void pl_render_testpattern(void);

#endif
