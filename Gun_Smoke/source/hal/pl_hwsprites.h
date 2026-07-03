/* src/hal/pl_hwsprites.h -- Pac-Land hardware-SPRITE offload for the Amiga AGA
 * attached-pair engine (src/hal/hwscroll.c).
 *
 * Pac-Land draws its sprites in software (src/hal/pl_render.c, the loop "sprites
 * over the tilemaps"). On a stock A1200 we instead hand them to the proven
 * attached-pair hardware-sprite engine: each on-screen 16x16 arcade sprite tile
 * becomes one lossless 15-colour attached-pair hw sprite, multiplexed down the
 * screen. Multi-tile (32x32 / 32x16 / 16x32) objects are decomposed into their
 * constituent 16x16 tiles and placed all-in-hardware or rolled back as a unit
 * (never fragmented).
 *
 * Pure C; no Amiga headers beyond hwscroll.h. Links on m68k-amigaos-gcc -m68020
 * (big-endian) alongside pl_hwrender.c, which calls these.
 */
#ifndef PL_HWSPRITES_H
#define PL_HWSPRITES_H

#include <stdint.h>
#include "hwscroll.h"

/* Store the sprite gfx ROM + PROM pointers, pre-decode the 16x16 sprite pixel
 * cache (mirrors pl_render.c sprite_pix), and precompute the per-sprite-colour
 * (0..63) pen maps + indirect-index tables used to build the 15-colour hw
 * palettes. Call once after the gfx/PROMs are available, before _draw(). */
void pl_hwsprites_init(const unsigned char *spr_rom, const unsigned char *proms);

/* Collect the up-to-64 sprites from pl_spriteram() and feed them to the
 * attached-pair hw-sprite engine on hwscroll context S. Call once per frame
 * AFTER the tilemaps have been drawn into S's playfield. pal_bank is the current
 * Pac-Land palette bank (pl_palette_bank(), 0..3); the 15-colour sprite palettes
 * are rebuilt when it changes. */
void pl_hwsprites_draw(hwscroll_t *S, int pal_bank);

/* Undo last frame's bobs: restore the clean background saved beneath each. Call
 * ONCE per frame at the TOP of the render, BEFORE refresh_layer redraws the dirty
 * tilemap cells and BEFORE pl_hwsprites_draw() saves+draws this frame's bobs. */
void pl_hwsprites_restore(hwscroll_t *S);

#endif
