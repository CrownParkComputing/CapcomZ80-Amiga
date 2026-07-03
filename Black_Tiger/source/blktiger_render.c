/* blktiger_render.c -- Black Tiger software renderer (from MAME blktiger.cpp video).
 *
 * Layers, back-to-front:
 *   backdrop -> BG back half -> sprites -> BG priority/front half -> text.
 *
 * The BG front half follows MAME's per-colour split-table/transmask priority model.
 *
 * GFX decode (MAME GFXLAYOUT, MSB-first plane offsets):
 *   chars   8x8  2bpp  planes{4,0}            xoffs{0,1,2,3,8,9,10,11}            charinc 128
 *   tiles   16x16 4bpp planes{H+4,H+0,4,0}    xoffs{0..3,8..11,256..259,264..267} charinc 512  (H=RGN_FRAC(1,2))
 *   sprites 16x16 4bpp (same spritelayout as tiles)
 */
#include "blktiger_render.h"

static const int XOFS16[16] = {0,1,2,3,8,9,10,11, 256,257,258,259,264,265,266,267};
static const int XOFS8[8]   = {0,1,2,3,8,9,10,11};
#define VIS_Y0 16       /* MAME visible area is internal rows 16..239 */

/* RGN_FRAC(1,2) for the 0x40000 tile/sprite regions = (0x40000*8)/2 = 0x100000 bits */
#define HALFBITS 0x100000L

static inline int rbit(const unsigned char *r, long b){ return (r[b>>3] >> (7 - (b & 7))) & 1; }

/* decode one 4bpp pen of a 16x16 tile/sprite */
static int pen16(const unsigned char *rgn, int code, int x, int y){
    long base = (long)code*512 + (long)y*16 + XOFS16[x];
    int b0 = rbit(rgn, base + 0);
    int b1 = rbit(rgn, base + 4);
    int b2 = rbit(rgn, base + HALFBITS + 0);
    int b3 = rbit(rgn, base + HALFBITS + 4);
    return (b3<<3) | (b2<<2) | (b1<<1) | b0;
}
/* decode one 2bpp pen of an 8x8 char */
static int pen8(const unsigned char *rgn, int code, int x, int y){
    long base = (long)code*128 + (long)y*16 + XOFS8[x];
    return (rbit(rgn, base+4) << 1) | rbit(rgn, base+0);
}

/* ---- decoded-pixel caches (the 1943 approach) -------------------------------
 * The gfx ROMs are constant, so bit-extract every char/tile/sprite ONCE at init
 * into flat 1-byte-per-pixel arrays (the pen value); the per-frame compositor
 * then reads ONE byte per pixel instead of doing 4 bitplane fetches. This is the
 * difference between ~1fps and full speed on the 68020.  Static BSS (like the
 * 1943 dec_* arrays) -> works in both host tooling and the RTG build, no malloc. */
#define BT_NTILE  2048   /* (0x40000/2)*8 / 512 bits  -> 11-bit tile code   */
#define BT_NSPR   2048   /* (0x40000/2)*8 / 512 bits  -> 11-bit sprite code */
#define BT_NCHAR  2048   /* 0x8000*8 / 128 bits       -> 11-bit char code   */
static unsigned char tcache[BT_NTILE*256];   /* 512KB: code*256 + y*16 + x -> pen 0..15 */
static unsigned char scache[BT_NSPR *256];   /* 512KB: code*256 + y*16 + x -> pen 0..15 */
static unsigned char ccache[BT_NCHAR* 64];   /* 128KB: code*64  + y*8  + x -> pen 0..3  */

void bt_render_init(void){
    const unsigned char *tiles   = bt_tiles();
    const unsigned char *sprites = bt_sprites();
    const unsigned char *chars   = bt_chars();
    for(int c=0;c<BT_NTILE;c++) for(int y=0;y<16;y++) for(int x=0;x<16;x++)
        tcache[c*256 + y*16 + x] = (unsigned char)pen16(tiles,   c, x, y);
    for(int c=0;c<BT_NSPR; c++) for(int y=0;y<16;y++) for(int x=0;x<16;x++)
        scache[c*256 + y*16 + x] = (unsigned char)pen16(sprites, c, x, y);
    for(int c=0;c<BT_NCHAR;c++) for(int y=0;y<8; y++) for(int x=0;x<8; x++)
        ccache[c*64  + y*8  + x] = (unsigned char)pen8 (chars,   c, x, y);
}

/* MAME tilemap scan mappers (logical col,row -> tile index in the 0x4000 BG RAM) */
static int bg8x4_scan(int col,int row){ return (col&0x0f) + ((row&0x0f)<<4) + ((col&0x70)<<4) + ((row&0x30)<<7); }
static int bg4x8_scan(int col,int row){ return (col&0x0f) + ((row&0x0f)<<4) + ((col&0x30)<<4) + ((row&0x70)<<6); }

static const unsigned short bg_front_mask[4] = {
    0xffff, /* group 0: no foreground pens */
    0xfff0, /* group 1: pens 0..3 over sprites */
    0xff00, /* group 2: pens 0..7 over sprites */
    0xf000  /* group 3: pens 0..11 over sprites */
};
static const unsigned char bg_split_table[16] = {
    3,3,2,2, 1,1,0,0, 0,0,0,0, 0,0,0,0
};

static void draw_bg(MY_LITTLE_Z80 *z, unsigned short f[BT_NH][BT_NW], int front){
    (void)z;
    const unsigned char *sr = bt_scrollram();
    int layout = bt_screen_layout();
    int sx0 = bt_scrollx(), sy0 = bt_scrolly();
    int mw = layout ? 128*16 : 64*16;   /* wide 8x4 : tall 4x8 */
    int mh = layout ? 64*16  : 128*16;
    for(int sy=0; sy<BT_NH; sy++) for(int sx=0; sx<BT_NW; sx++){
        int screen_y = sy + VIS_Y0;
        int mx = (sx + sx0) & (mw-1);
        int my = (screen_y + sy0) & (mh-1);
        int ti = layout ? bg8x4_scan(mx>>4, my>>4) : bg4x8_scan(mx>>4, my>>4);
        const unsigned char *a = sr + 2*ti;
        int code  = a[0] | ((a[1]&0x07)<<8);
        int color = (a[1]&0x78)>>3;
        int px = mx&15, py = my&15;
        if(a[1]&0x80) px = 15-px;            /* tile flipX (no flipY) */
        int pen = tcache[code*256 + py*16 + px];
        if(front){
            unsigned mask = bg_front_mask[bg_split_table[color & 15]];
            if(mask & (1u << pen)) continue;
        } else {
            if(pen == 15) continue;
        }
        f[sy][sx] = (unsigned short)(0x000 + color*16 + pen);
    }
}

static void draw_sprites(unsigned short f[BT_NH][BT_NW]){
    const unsigned char *spr = bt_spritebuf();
    int flip = bt_flip();
    for(int offs = 0x200-4; offs >= 0; offs -= 4){   /* offset 0 last = on top */
        int attr  = spr[offs+1];
        int sx    = spr[offs+3] - ((attr&0x10)<<4);  /* 9-bit X, high bit subtracted */
        int sy    = spr[offs+2];
        int code  = spr[offs] | ((attr&0xe0)<<3);
        int color = attr&0x07;
        int flipx = (attr&0x08) ? 1 : 0;
        int flipy = flip;
        if(flip){ sx = 240 - sx; sy = 240 - sy; flipx = !flipx; }
        for(int py=0; py<16; py++) for(int px=0; px<16; px++){
            int dx = sx+px, dy = sy+py - VIS_Y0;
            if(dx<0 || dx>=BT_NW || dy<0 || dy>=BT_NH) continue;
            int tx = flipx ? 15-px : px;
            int ty = flipy ? 15-py : py;
            int pen = scache[code*256 + ty*16 + tx];
            if(pen != 15) f[dy][dx] = (unsigned short)(0x200 + color*16 + pen);
        }
    }
}

static void draw_text(MY_LITTLE_Z80 *z, unsigned short f[BT_NH][BT_NW]){
    const unsigned char *tx = bt_txram(z);   /* 0xd000 code, +0x400 attr */
    for(int ty=0; ty<32; ty++) for(int tc=0; tc<32; tc++){   /* 32x32 map, cropped to visible rows 16..239 */
        int idx  = ty*32 + tc;
        int code = tx[idx] | ((tx[idx+0x400]&0xe0)<<3);
        int color= tx[idx+0x400]&0x1f;
        for(int py=0; py<8; py++) for(int px=0; px<8; px++){
            int pen = ccache[code*64 + py*8 + px];
            if(pen == 3) continue;           /* text transparent pen = 3 */
            int sx = tc*8+px, sy = ty*8+py - VIS_Y0;
            if(sx<BT_NW && sy<BT_NH) f[sy][sx] = (unsigned short)(0x300 + color*4 + pen);
        }
    }
}

void bt_render(MY_LITTLE_Z80 *z, unsigned short f[BT_NH][BT_NW]){
    for(int y=0;y<BT_NH;y++) for(int x=0;x<BT_NW;x++) f[y][x] = 0x3ff;  /* backdrop = palette 0x3ff */
    if(bt_bgon())  draw_bg(z, f, 0);
    if(bt_objon()) draw_sprites(f);
    if(bt_bgon())  draw_bg(z, f, 1);
    if(bt_chon())  draw_text(z, f);
}
