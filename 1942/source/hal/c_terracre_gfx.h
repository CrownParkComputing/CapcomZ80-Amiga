/* src/hal/c_terracre_gfx.h -- Terra Cresta (terracren) gfx decode + palette
 * indirection, shared by the host validation harness (tools/terracre_host.c) and
 * the Amiga renderer (src/hal/c_terracre_render.c) so the math can't diverge.
 *
 * Faithful port of MAME nichibutsu/terracre.cpp: gfxdecode layouts + the
 * palette()/draw_sprites() PROM indirection.
 *
 * ROM blobs (from tools/make_terracrerom.py): fg (8x8x4 packed_lsb), bg
 * (16x16x4 packed_lsb), spr (custom 16x16x4, RGN_FRAC(1,2) split), proms:
 *   0x000 red  0x100 green  0x200 blue  0x300 sprite-lookup  0x400 sprite-palbank
 *
 * All three "ctab" helpers return a final 0..255 colour-table index (the value
 * MAME stores via set_pen_indirect); the 256 hardware colours ARE those 256
 * indirect RGB entries straight from the R/G/B PROMs, so the ctab index doubles
 * as the AGA pen. */
#ifndef C_TERRACRE_GFX_H
#define C_TERRACRE_GFX_H
#include <stdint.h>

/* ---- the embedded ROM blobs (defined by c_terracre_romdata.s on Amiga, or by
 * the host harness) ---- */
extern const uint8_t tc_rom_fg[];      /* 0x2000  */
extern const uint8_t tc_rom_bg[];      /* 0x10000 */
extern const uint8_t tc_rom_spr[];     /* 0x10000 */
extern const uint8_t tc_rom_proms[];   /* 0x500   */

/* ---- one MSB-first bit out of a byte array ---- */
static inline int tc_bit(const uint8_t *p, unsigned o)
{
    return (p[o >> 3] >> (7 - (o & 7))) & 1;
}

/* fg char: gfx_8x8x4_packed_lsb, 32 bytes/tile, 256 tiles. pixel = nibble. */
static inline int tc_fg_pix(int code, int x, int y)
{
    uint8_t b = tc_rom_fg[code * 32 + y * 4 + (x >> 1)];
    return (x & 1) ? (b >> 4) : (b & 0x0f);
}

/* bg tile: gfx_16x16x4_packed_lsb, 128 bytes/tile, 512 tiles. pixel = nibble. */
static inline int tc_bg_pix(int code, int x, int y)
{
    uint8_t b = tc_rom_bg[(code & 0x1ff) * 128 + y * 8 + (x >> 1)];
    return (x & 1) ? (b >> 4) : (b & 0x0f);
}

/* sprite: custom layout, RGN_FRAC(1,2) split. plane bits {0,1,2,3} consecutive;
 * x-offsets alternate the two ROM halves; 512 tiles. */
#define TC_SPR_F 0x40000               /* RGN_FRAC(1,2) of 0x10000 bytes, in bits */
static inline int tc_spr_pix(int code, int x, int y)
{
    static const int xo[16] = {
        4, 0, TC_SPR_F + 4, TC_SPR_F + 0,
        12, 8, TC_SPR_F + 12, TC_SPR_F + 8,
        20, 16, TC_SPR_F + 20, TC_SPR_F + 16,
        28, 24, TC_SPR_F + 28, TC_SPR_F + 24
    };
    unsigned o = (unsigned)(code & 0x1ff) * 512 + (unsigned)(y * 32) + xo[x];
    return tc_bit(tc_rom_spr, o) | (tc_bit(tc_rom_spr, o + 1) << 1)
         | (tc_bit(tc_rom_spr, o + 2) << 2) | (tc_bit(tc_rom_spr, o + 3) << 3);
}

/* ---- palette indirection (final 0..255 colour-table index) ---- */

/* fg/text: pens 0..0xf map straight through; 0xf is transparent. */
static inline int tc_fg_ctab(int pix) { return pix & 0x0f; }   /* transparent when ==0x0f */

/* bg: i = color*16 + pix (color 0..15, pix 0..15). */
static inline int tc_bg_ctab(int color, int pix)
{
    int i = ((color & 0x0f) << 4) | pix;
    return 0xc0 | (i & 0x0f) | ((i >> ((i & 0x08) ? 2 : 0)) & 0x30);
}

/* sprites: two-level PROM indirection.  color_full already folds the palbank PROM
 * (computed by the caller); P = color_full*16 + pix; recover MAME's nibble-swapped
 * lookup index i, then apply the sprite-lookup PROM. transparent when pix==0. */
static inline int tc_spr_ctab(int color_full, int pix)
{
    int P = ((color_full & 0xff) << 4) | (pix & 0x0f);          /* 0..0xfff */
    int i = ((P & 0xff) << 4) | ((P >> 8) & 0x0f);
    return 0x80 | ((i << ((i & 0x80) ? 2 : 4)) & 0x30)
         | (tc_rom_proms[0x300 + (i >> 4)] & 0x0f);
}

/* sprite palette bank fold (MAME draw_sprites, terracre branch): given the raw
 * sprite tile code and the attribute nibble colour, return color_full (0..255). */
static inline int tc_spr_colorfull(int tile, int attr)
{
    int color = (attr & 0xf0) >> 4;
    if (attr & 0x02) tile |= 0x100;
    color += 16 * (tc_rom_proms[0x400 + ((tile >> 1) & 0xff)] & 0x0f);
    return color;
}

/* the 256-colour hardware palette: indirect RGB straight from the R/G/B PROMs,
 * 4 bits/channel expanded to 8 (pal4bit).  rgb = 3 bytes/entry (caller-sized 768). */
static inline void tc_build_palette(uint8_t *rgb /* [256*3] */)
{
    for (int i = 0; i < 256; i++) {
        int r = tc_rom_proms[0x000 + i] & 0x0f;
        int g = tc_rom_proms[0x100 + i] & 0x0f;
        int b = tc_rom_proms[0x200 + i] & 0x0f;
        rgb[i * 3 + 0] = (uint8_t)((r << 4) | r);
        rgb[i * 3 + 1] = (uint8_t)((g << 4) | g);
        rgb[i * 3 + 2] = (uint8_t)((b << 4) | b);
    }
}

#endif /* C_TERRACRE_GFX_H */
