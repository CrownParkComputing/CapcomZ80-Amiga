#include "sidearms_render.h"
#include <string.h>

#define RW 512
#define RH 256
#define VX 64
#define VY 16
#define TILE_COUNT 512
#define SPR_COUNT 2048
#define CHAR_COUNT 1024
#define HALFBITS 0x100000L

static const unsigned char *g_chars;
static const unsigned char *g_tiles;
static const unsigned char *g_sprites;
static const unsigned char *g_tilemap;

static unsigned char tcache[TILE_COUNT * 32 * 32];
static unsigned char scache[SPR_COUNT * 16 * 16];
static unsigned char ccache[CHAR_COUNT * 8 * 8];

static const int txo[32] = {
    0,1,2,3,8,9,10,11, 512,513,514,515,520,521,522,523,
    1024,1025,1026,1027,1032,1033,1034,1035, 1536,1537,1538,1539,1544,1545,1546,1547
};
static const int sxo[16] = {
    0,1,2,3,8,9,10,11, 256,257,258,259,264,265,266,267
};
static const int cxo[8] = {0,1,2,3,8,9,10,11};

static inline int rbit(const unsigned char *r, long b)
{
    return (r[b >> 3] >> (7 - (b & 7))) & 1;
}

static int tile_pen(int code, int x, int y)
{
    long o = (long)code * 2048 + (long)y * 16 + txo[x];
    return (rbit(g_tiles, o + HALFBITS + 4) << 3) |
           (rbit(g_tiles, o + HALFBITS) << 2) |
           (rbit(g_tiles, o + 4) << 1) |
           rbit(g_tiles, o);
}

static int spr_pen(int code, int x, int y)
{
    long o = (long)code * 512 + (long)y * 16 + sxo[x];
    return (rbit(g_sprites, o + HALFBITS + 4) << 3) |
           (rbit(g_sprites, o + HALFBITS) << 2) |
           (rbit(g_sprites, o + 4) << 1) |
           rbit(g_sprites, o);
}

static int char_pen(int code, int x, int y)
{
    long o = (long)code * 128 + (long)y * 16 + cxo[x];
    return (rbit(g_chars, o + 4) << 1) | rbit(g_chars, o);
}

static int bg_scan(int row, int col)
{
    int off = ((row << 7) + col) << 1;
    return ((off & 0xf801) | ((off & 0x0700) >> 7) | ((off & 0x00fe) << 3)) & 0x7fff;
}

void sa_set_gfx(const unsigned char *chars, const unsigned char *tiles,
                const unsigned char *sprites, const unsigned char *tilemap)
{
    g_chars = chars;
    g_tiles = tiles;
    g_sprites = sprites;
    g_tilemap = tilemap;
}

void sa_render_init(void)
{
    for (int c = 0; c < TILE_COUNT; c++)
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++)
                tcache[c * 1024 + y * 32 + x] = (unsigned char)tile_pen(c, x, y);

    for (int c = 0; c < SPR_COUNT; c++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                scache[c * 256 + y * 16 + x] = (unsigned char)spr_pen(c, x, y);

    for (int c = 0; c < CHAR_COUNT; c++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                ccache[c * 64 + y * 8 + x] = (unsigned char)char_pen(c, x, y);
}

static void draw_bg(unsigned short f[SA_NH][SA_NW])
{
    int sx0 = csidearms_scrollx() & 0xfff;
    int sy0 = csidearms_scrolly() & 0xfff;
    for (int y = 0; y < SA_NH; y++) {
        int ry = y + VY;
        for (int x = 0; x < SA_NW; x++) {
            int rx = x + VX;
            int wx = (rx + sx0) & 0xfff;
            int wy = (ry + sy0) & 0xfff;
            int off = bg_scan(wy >> 5, wx >> 5);
            int code = g_tilemap[off];
            int attr = g_tilemap[off + 1];
            int color = (attr >> 3) & 0x1f;
            int px = wx & 31;
            int py = wy & 31;
            if (attr & 0x02) px = 31 - px;
            if (attr & 0x04) py = 31 - py;
            code |= (attr << 8) & 0x100;
            int pen = tcache[code * 1024 + py * 32 + px];
            if (pen != 15) f[y][x] = (unsigned short)(color * 16 + pen);
        }
    }
}

static void draw_sprites(MY_LITTLE_Z80 *z, unsigned short f[SA_NH][SA_NW])
{
    for (int offs = 0x1000 - 32; offs >= 0; offs -= 32) {
        int a = 0xf000 + offs;
        int sy = csidearms_peek(z, a + 2);
        if (!sy || csidearms_peek(z, a + 5) == 0xc3) continue;
        int attr = csidearms_peek(z, a + 1);
        int color = attr & 0x0f;
        int code = csidearms_peek(z, a) + ((attr << 3) & 0x700);
        int sx = csidearms_peek(z, a + 3) + ((attr << 4) & 0x100);
        for (int py = 0; py < 16; py++)
            for (int px = 0; px < 16; px++) {
                int pen = scache[code * 256 + py * 16 + px];
                if (pen == 15) continue;
                int dx = sx + px - VX;
                int dy = sy + py - VY;
                if ((unsigned)dx < SA_NW && (unsigned)dy < SA_NH)
                    f[dy][dx] = (unsigned short)(512 + color * 16 + pen);
            }
    }
}

static void draw_text(MY_LITTLE_Z80 *z, unsigned short f[SA_NH][SA_NW])
{
    for (int y = 0; y < SA_NH; y++) {
        int ry = y + VY;
        for (int x = 0; x < SA_NW; x++) {
            int rx = x + VX;
            int col = rx >> 3;
            int row = ry >> 3;
            int idx = row * 64 + col;
            int attr = csidearms_peek(z, 0xd800 + idx);
            int code = csidearms_peek(z, 0xd000 + idx) + ((attr << 2) & 0x300);
            int color = attr & 0x3f;
            int pen = ccache[code * 64 + (ry & 7) * 8 + (rx & 7)];
            if (pen != 3) f[y][x] = (unsigned short)(768 + color * 4 + pen);
        }
    }
}

void sa_render(MY_LITTLE_Z80 *z, unsigned short f[SA_NH][SA_NW])
{
    memset(f, 0, (size_t)SA_NH * SA_NW * sizeof f[0][0]);

    if (csidearms_gfxctrl() & 0x01) draw_bg(f);
    if (csidearms_gfxctrl() & 0x02) draw_sprites(z, f);
    if (csidearms_control() & 0x40) draw_text(z, f);
}
