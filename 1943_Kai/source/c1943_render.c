/* c1943_render.c -- 1943 software renderer, transcribed from MAME 0.288
 * capcom/1943_v.cpp. Produces the native (un-rotated) 256x224 chunky frame plus
 * the 256-colour indirect palette. Draw order (screen_update):
 *   bg2 (back, opaque) -> bg1 (front, key-transparent) -> sprites -> chars (HUD).
 * Sprite/bg1 priority uses the sprite palette-bank PROM bit (proms[0x900+..]&8),
 * exactly as _1943_drawgfx does.
 *
 * Palette is "indirect": 256 RGB colours (proms[0x000..0x2ff]) selected by
 * per-layer lookup tables. The renderer emits the final indirect index (the
 * MAME pen-indirect "ctabentry"), so fb[] indexes c1943_build_palette()'s table
 * directly. Pen/transparency rules per layer match _1943_palette + video_start.
 */
#include "c1943_render.h"
#include "c1943_machine.h"
#include <stdlib.h>
#include <string.h>

/* Parallax toggle: 1943 composites bg2 (opaque base) + bg1 (transparent front overlay
 * scrolling at a different rate = the parallax). Building -DC1943_NO_PARALLAX skips the
 * whole bg1 pass -- one fewer full-screen tile composite per frame (a speed lever); the
 * opaque bg2 base + sprites + HUD still render. */
#ifdef C1943_NO_PARALLAX
#define C1943_PARALLAX 0
#else
#define C1943_PARALLAX 1
#endif
static int runtime_parallax = C1943_PARALLAX;

#define C1943_TEXT_ROW_MIN 8
#define C1943_TEXT_ROW_MAX 19
#define C1943_TEXT_MIN_TILES 4
#define C1943_GRAPHIC_MIN_TILES 22

/* gfx element counts (RGN_FRAC 1/2 low-plane half / bytes-per-tile) */
#define NCHAR 2048   /* 0x8000 / 16   */
#define NBG1   512   /* (0x40000/2) / 256 */
#define NBG2   128   /* (0x10000/2) / 256 */
#define NSPR  2048   /* (0x40000/2) / 64  */
#ifndef C1943_SPRITE_LIMIT
#define C1943_SPRITE_LIMIT 96
#endif

static const uint8_t *g1, *g2, *g3, *g4, *trom, *prom;

/* per-layer pen -> indirect-colour lookup tables, precomputed from the PROMs */
static uint8_t char_ctab[128];               /* pen = color*4 + pix (color 0..31) */
static uint8_t bg1_ctab[256], bg1_tr[256];   /* pen = color*16 + pix (color 0..15) */
static uint8_t bg2_ctab[256];
static uint8_t spr_ctab[256], spr_pri[256];
static int black_idx;                        /* indirect index used when bg2 off */

/* decoded-pixel caches: the gfx ROMs are constant, so bit-extract every char/
 * tile/sprite ONCE at init, then the per-frame compositor reads a byte instead
 * of doing 2-4 bitplane fetches per pixel. Allocated rather than static BSS so
 * the Amiga RTG build can force the hot caches into FAST RAM. */
static uint8_t *dec_char;
static uint8_t *dec_char_ink;
static uint8_t *dec_bg1;
static uint8_t *dec_bg2;
static uint8_t *dec_spr;
static void *(*cache_alloc_fn)(uint32_t bytes);
static void (*cache_free_fn)(void *ptr, uint32_t bytes);

#define DEC_CHAR(c) (dec_char + (uint32_t)(c) * 64u)
#define DEC_BG1(c)  (dec_bg1  + (uint32_t)(c) * 1024u)
#define DEC_BG2(c)  (dec_bg2  + (uint32_t)(c) * 1024u)
#define DEC_SPR(c)  (dec_spr  + (uint32_t)(c) * 256u)

static void *cache_alloc(uint32_t bytes)
{
    if (cache_alloc_fn) return cache_alloc_fn(bytes);
    return calloc(1, bytes);
}

static void cache_free(void *ptr, uint32_t bytes)
{
    if (!ptr) return;
    if (cache_free_fn) cache_free_fn(ptr, bytes);
    else free(ptr);
}

void c1943_render_set_allocators(void *(*alloc_fn)(uint32_t bytes),
                                 void (*free_fn)(void *ptr, uint32_t bytes))
{
    cache_alloc_fn = alloc_fn;
    cache_free_fn = free_fn;
}

void c1943_render_shutdown(void)
{
    cache_free(dec_char, (uint32_t)NCHAR * 64u); dec_char = 0;
    cache_free(dec_char_ink, (uint32_t)NCHAR); dec_char_ink = 0;
    cache_free(dec_bg1, (uint32_t)NBG1 * 1024u); dec_bg1 = 0;
    cache_free(dec_bg2, (uint32_t)NBG2 * 1024u); dec_bg2 = 0;
    cache_free(dec_spr, (uint32_t)NSPR * 256u); dec_spr = 0;
}

void c1943_render_set_parallax(int on)
{
    runtime_parallax = on ? C1943_PARALLAX : 0;
}

static inline int gbit(const uint8_t *p, unsigned o) { return (p[o >> 3] >> (7 - (o & 7))) & 1; }

/* tile/sprite pixel decoders (MAME gfx_layout bit offsets, MSB-first) */
static const int TXO[32] = {
    0,1,2,3, 8,9,10,11, 512,513,514,515, 520,521,522,523,
    1024,1025,1026,1027, 1032,1033,1034,1035, 1536,1537,1538,1539, 1544,1545,1546,1547
};
static const int SXO[16] = { 0,1,2,3, 8,9,10,11, 256,257,258,259, 264,265,266,267 };
static const int CXO[8]  = { 0,1,2,3, 8,9,10,11 };

static int char_pix(int code, int x, int y) {
    unsigned o = (unsigned)code * 128 + (unsigned)y * 16 + CXO[x];
    return (gbit(g1, o + 4) << 1) | gbit(g1, o);
}
static int bg1_pix(int code, int x, int y) {
    const unsigned H = 0x100000;             /* (0x40000/2)*8 bits */
    unsigned o = (unsigned)code * 2048 + (unsigned)y * 16 + TXO[x];
    return (gbit(g2, o+H+4) << 3) | (gbit(g2, o+H) << 2) | (gbit(g2, o+4) << 1) | gbit(g2, o);
}
static int bg2_pix(int code, int x, int y) {
    const unsigned H = 0x40000;              /* (0x10000/2)*8 bits */
    unsigned o = (unsigned)code * 2048 + (unsigned)y * 16 + TXO[x];
    return (gbit(g3, o+H+4) << 3) | (gbit(g3, o+H) << 2) | (gbit(g3, o+4) << 1) | gbit(g3, o);
}
static int spr_pix(int code, int x, int y) {
    const unsigned H = 0x100000;             /* (0x40000/2)*8 bits */
    unsigned o = (unsigned)code * 512 + (unsigned)y * 16 + SXO[x];
    return (gbit(g4, o+H+4) << 3) | (gbit(g4, o+H) << 2) | (gbit(g4, o+4) << 1) | gbit(g4, o);
}

static int prom_weight(int v) {
    return 0x0e*(v&1) + 0x1f*((v>>1)&1) + 0x43*((v>>2)&1) + 0x8f*((v>>3)&1);
}

void c1943_render_init(const uint8_t *gfx1, const uint8_t *gfx2,
                       const uint8_t *gfx3, const uint8_t *gfx4,
                       const uint8_t *tilerom, const uint8_t *proms)
{
    g1 = gfx1; g2 = gfx2; g3 = gfx3; g4 = gfx4; trom = tilerom; prom = proms;

    c1943_render_shutdown();
    dec_char = (uint8_t *)cache_alloc((uint32_t)NCHAR * 64u);
    dec_char_ink = (uint8_t *)cache_alloc((uint32_t)NCHAR);
    dec_bg1 = (uint8_t *)cache_alloc((uint32_t)NBG1 * 1024u);
    dec_bg2 = (uint8_t *)cache_alloc((uint32_t)NBG2 * 1024u);
    dec_spr = (uint8_t *)cache_alloc((uint32_t)NSPR * 256u);
    /* per-layer indirect-colour tables (see _1943_palette). LUT base = prom+0x300. */
    for (int pen = 0; pen < 128; pen++)
        char_ctab[pen] = (prom[0x300 + pen] & 0x0f) | 0x40;
    for (int k = 0; k < 256; k++) {
        uint8_t c = ((prom[0x500 + k] & 0x03) << 4) | (prom[0x400 + k] & 0x0f);   /* bg1 */
        bg1_ctab[k] = c; bg1_tr[k] = (c == 0x0f);
        bg2_ctab[k] = ((prom[0x700 + k] & 0x03) << 4) | (prom[0x600 + k] & 0x0f); /* bg2 */
        spr_ctab[k] = ((prom[0x900 + k] & 0x07) << 4) | (prom[0x800 + k] & 0x0f) | 0x80;
        spr_pri[k]  = (prom[0x900 + k] & 0x08) ? 1 : 0;
    }
    /* pick a pure-black indirect index for the bg2-off fill */
    black_idx = 0;
    for (int i = 0; i < 256; i++)
        if (prom_weight(prom[i]) == 0 && prom_weight(prom[0x100+i]) == 0 && prom_weight(prom[0x200+i]) == 0) {
            black_idx = i; break;
        }

    /* fill the decoded-pixel caches once (bit-extract every element) */
    if (dec_char && dec_char_ink) for (int c = 0; c < NCHAR; c++) {
        uint8_t *dc = DEC_CHAR(c);
        int ink = 0;
        for (int y = 0; y < 8;  y++) for (int x = 0; x < 8;  x++) {
            uint8_t p = (uint8_t)char_pix(c, x, y);
            dc[y*8 + x] = p;
            ink |= p;
        }
        dec_char_ink[c] = (uint8_t)(ink != 0);
    }
    if (dec_bg1) for (int c = 0; c < NBG1;  c++) for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++)
        DEC_BG1(c)[y*32 + x]  = (uint8_t)bg1_pix(c, x, y);
    if (dec_bg2) for (int c = 0; c < NBG2;  c++) for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++)
        DEC_BG2(c)[y*32 + x]  = (uint8_t)bg2_pix(c, x, y);
    if (dec_spr) for (int c = 0; c < NSPR;  c++) for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
        DEC_SPR(c)[y*16 + x]  = (uint8_t)spr_pix(c, x, y);
}

void c1943_build_palette(uint8_t pal[256][3])
{
    for (int i = 0; i < 256; i++) {
        pal[i][0] = (uint8_t)prom_weight(prom[0x000 + i]);
        pal[i][1] = (uint8_t)prom_weight(prom[0x100 + i]);
        pal[i][2] = (uint8_t)prom_weight(prom[0x200 + i]);
    }
}

void c1943_render_frame(MY_LITTLE_Z80 *z, uint8_t fb[C1943_NH][C1943_NW])
{
    static uint8_t pri[C1943_NH][C1943_NW];

    const int bg2sx = c1943_bg2_scrollx(z);
    const int bg1sx = c1943_bg1_scrollx(z);
    const int bg1sy = c1943_bg1_scrolly(z);
    int bg2_on = c1943_bg2_on();
    int bg1_on = c1943_bg1_on();
    int obj_on =
#ifdef C1943_NO_SPRITES
        0;
#else
        c1943_obj_on();
#endif
    int char_on = c1943_char_on();

    /* --- bg2 (opaque base) + bg1 (transparent overlay), tile-span rendered ---
     * Tile lookup is hoisted out of the pixel loop: it is recomputed only at each
     * 32-pixel tile boundary, not per pixel. */
    for (int oy = 0; oy < C1943_NH; oy++) {
        const int sy = oy + 16;
        uint8_t *fbrow = fb[oy];
        uint8_t *prow  = pri[oy];

        if (bg2_on) {
            int ty = sy & 0xff, row = (ty >> 5) & 7, iyr = ty & 31, ox = 0;
            while (ox < C1943_NW) {
                int tx = (ox + bg2sx) & 0xffff;
                int col = (tx >> 5) & 2047, sub = tx & 31;
                int offs = 0x8000 + (col * 8 + row) * 2;
                int attr = trom[offs + 1], code = trom[offs] % NBG2;
                const uint8_t *cb = bg2_ctab + ((attr & 0x3c) >> 2) * 16;
                int fx = attr & 0x40;
                for (; sub < 32 && ox < C1943_NW; sub++, ox++) {
                    int txp = fx ? 31 - sub : sub;
                    int rp = dec_bg2 ? DEC_BG2(code)[((attr & 0x80) ? 31 - iyr : iyr) * 32 + txp]
                                     : bg2_pix(code, txp, (attr & 0x80) ? 31 - iyr : iyr);
                    fbrow[ox] = cb[rp];
                    prow[ox] = 1;
                }
            }
        } else {
            for (int ox = 0; ox < C1943_NW; ox++) { fbrow[ox] = (uint8_t)black_idx; prow[ox] = 0; }
        }

        if (bg1_on && runtime_parallax) {
            int ty = (sy + bg1sy) & 0xff, row = (ty >> 5) & 7, iyr = ty & 31, ox = 0;
            while (ox < C1943_NW) {
                int tx = (ox + bg1sx) & 0xffff;
                int col = (tx >> 5) & 2047, sub = tx & 31;
                int offs = (col * 8 + row) * 2;
                int attr = trom[offs + 1], code = (trom[offs] + ((attr & 0x01) << 8)) % NBG1;
                int colr = (attr & 0x3c) >> 2, fx = attr & 0x40;
                for (; sub < 32 && ox < C1943_NW; sub++, ox++) {
                    int txp = fx ? 31 - sub : sub;
                    int rp = dec_bg1 ? DEC_BG1(code)[((attr & 0x80) ? 31 - iyr : iyr) * 32 + txp]
                                     : bg1_pix(code, txp, (attr & 0x80) ? 31 - iyr : iyr);
                    int k = colr * 16 + rp;
                    if (!bg1_tr[k]) { fbrow[ox] = bg1_ctab[k]; prow[ox] = 2; }
                }
            }
        }
    }

    /* --- sprites (buffered spriteram; first entry topmost) --- */
    if (obj_on) {
        const uint8_t *spr = c1943_spritebuf();
        const int flip = c1943_flip();
        int drawn = 0;
        for (int offs = 0; offs < 0x1000; offs += 32) {
            int attr = spr[offs + 1];
            int code = (spr[offs] + ((attr & 0xe0) << 3)) & (NSPR - 1);
            int color = attr & 0x0f;
            int sx0 = spr[offs + 3] - ((attr & 0x10) << 4);
            int sy0 = spr[offs + 2];
            int fx = 0, fy = 0;
            if (flip) { sx0 = 240 - sx0; sy0 = 240 - sy0; fx = fy = 1; }
            if (sx0 >= C1943_NW || sx0 <= -16 || sy0 >= 240) continue;  /* fully off-screen (e.g. parked pool) */
            if (drawn++ >= C1943_SPRITE_LIMIT) continue;
            int x0 = sx0 < 0 ? 0 : sx0;
            int x1 = sx0 + 16;
            if (x1 > C1943_NW) x1 = C1943_NW;
            if (x0 >= x1) continue;
            for (int j = 0; j < 16; j++) {
                int Y = sy0 + j, oy = Y - 16;
                if (oy < 0 || oy >= C1943_NH) continue;
                int sj = fy ? 15 - j : j;
                uint8_t *fbrow = fb[oy];
                uint8_t *prow = pri[oy];
                int kbase = color * 16;
                for (int X = x0; X < x1; X++) {
                    int i = X - sx0;
                    int sxp = fx ? 15 - i : i;
                    int rp = dec_spr ? DEC_SPR(code)[sj * 16 + sxp] : spr_pix(code, sxp, sj);
                    if (rp == 0) continue;
                    uint8_t pv = prow[X];
                    if (pv & 0x80) continue;               /* a closer sprite owns it */
                    int k = kbase + rp;
                    if ((pv & 2) == 0 || spr_pri[k] == 0)   /* not hidden behind bg1 */
                        fbrow[X] = spr_ctab[k];
                    prow[X] = 0xff;
                }
            }
        }
    }

    /* --- chars / HUD (8x8, transparent on raw pixel 0, on top) tile-span ---
     * De-strobe: some 1943 banners flash their characters on alternating frames
     * (solid to the eye on a 60Hz CRT, gappy on a sharp display / a still). So we
     * draw a char shown in EITHER this frame or the previous one. Single-shot host
     * renders see prev_char=0, so the host output is unchanged (de-strobe only acts
     * across consecutive frames). */
    static uint8_t prev_char[C1943_NH][C1943_NW];
    if (char_on) {
        const uint8_t *vram = c1943_videoram(z);
        const uint8_t *cram = c1943_colorram(z);
        for (int oy = 0; oy < C1943_NH; oy++) {
            const int sy = oy + 16;
            int crow = (sy >> 3) & 31, ciy = sy & 7;
            uint8_t *fbrow = fb[oy];
            uint8_t *pc = prev_char[oy];
            for (int ox = 0; ox < C1943_NW; ox++) if (pc[ox]) fbrow[ox] = pc[ox];
            memset(pc, 0, C1943_NW);
            for (int cx = 0; cx < 32; cx++) {
                int ox = cx * 8;
                int idx = crow * 32 + cx;
                int attr = cram[idx];
                int code = (vram[idx] + ((attr & 0xe0) << 3)) % NCHAR;
                const uint8_t *cb = char_ctab + (attr & 0x1f) * 4;
                for (int i = 0; i < 8; i++) {
                    int rp = dec_char ? DEC_CHAR(code)[ciy * 8 + i] : char_pix(code, i, ciy);
                    if (rp) {
                        uint8_t v = cb[rp];
                        pc[ox + i] = v;
                        fbrow[ox + i] = v;
                    }
                }
            }
        }
    } else {
        memset(prev_char, 0, sizeof prev_char);
    }
}
