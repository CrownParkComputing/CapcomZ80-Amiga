/* cgunsmoke_rtg.c -- Gun.Smoke live RTG/chunky reference renderer.
 *
 * Interpreter-based, OS-friendly RTG path. It renders the arcade-native frame
 * using the same decode/PROM math as tools/gunsmoke_shot.c, rotates it upright
 * to 224x256, then scales it into a full-size window on the RTG Workbench
 * public screen.
 */
#include "z80emu.h"
#include <exec/exec.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdint.h>
#include <string.h>

struct IntuitionBase *IntuitionBase = 0;
struct GfxBase *GfxBase = 0;

extern const unsigned char gunsmoke_rom_chars[];
extern const unsigned char gunsmoke_rom_tiles[];
extern const unsigned char gunsmoke_rom_sprites[];
extern const unsigned char gunsmoke_rom_bgmap[];
extern const unsigned char gunsmoke_rom_proms[];
extern unsigned char gunsmoke_peek(MY_LITTLE_Z80 *z, unsigned a);
extern int gunsmoke_scrollx(void), gunsmoke_scrolly(void);
extern int gunsmoke_chon(void), gunsmoke_bgon(void), gunsmoke_objon(void), gunsmoke_sprite3bank(void);
extern void gunsmoke_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2);

#define GAME_W      224
#define GAME_H      256

#define SW 256
#define SH 224
#define YOFF 16
#define NCHAR 1024
#define NTILE 512
#define NSPR 2048

#define JOY1DAT  (*(volatile unsigned short *)0xdff00c)
#define CIAA_PRA (*(volatile unsigned char  *)0xbfe001)

#define KEY_1         0x01
#define KEY_2         0x02
#define KEY_5         0x05
#define KEY_6         0x06
#define KEY_Q         0x10
#define KEY_P         0x19
#define KEY_SPACE     0x40
#define KEY_BACKSPACE 0x41
#define KEY_RETURN    0x44
#define KEY_ESC       0x45
#define KEY_LCTRL     0x63
#define KEY_UP        0x4C
#define KEY_DOWN      0x4D
#define KEY_RIGHT     0x4E
#define KEY_LEFT      0x4F

static struct Screen *scr;
static struct Window *win;
static uint8_t *game_frame;
static uint8_t *win_frame;
static int win_w, win_h, draw_x0, draw_y0, draw_w, draw_h;
static uint8_t dec_char[NCHAR * 64];
static uint8_t dec_tile[NTILE * 1024];
static uint8_t dec_spr[NSPR * 256];
static uint32_t loadrgb[1 + 256 * 3 + 1];
static volatile uint8_t keydown[128];
static int gfx_decoded;
static int rtg_ok;
static int paused;
static int exit_request;
static int pubscreen_locked;

static inline int gbit(const unsigned char *p, unsigned o)
{
    return (p[o >> 3] >> (7 - (o & 7))) & 1;
}

static int char_pix(int code, int x, int y)
{
    static const int xo[8] = {11,10,9,8,3,2,1,0};
    static const int yo[8] = {112,96,80,64,48,32,16,0};
    unsigned o = (unsigned)code * 128 + yo[y] + xo[x];
    return (gbit(gunsmoke_rom_chars, o + 4) << 1) | gbit(gunsmoke_rom_chars, o);
}

static const int txo[32] = {
    0,1,2,3,8,9,10,11, 512,513,514,515,520,521,522,523,
    1024,1025,1026,1027,1032,1033,1034,1035, 1536,1537,1538,1539,1544,1545,1546,1547
};
static int tile_pix(int code, int x, int y)
{
    const unsigned H = 0x100000;
    unsigned o = (unsigned)code * 2048 + (unsigned)(y * 16) + txo[x];
    return (gbit(gunsmoke_rom_tiles, o + H + 4) << 3) |
           (gbit(gunsmoke_rom_tiles, o + H) << 2) |
           (gbit(gunsmoke_rom_tiles, o + 4) << 1) |
            gbit(gunsmoke_rom_tiles, o);
}

static int spr_pix(int code, int x, int y)
{
    static const int sxo[16] = {0,1,2,3,8,9,10,11, 256,257,258,259,264,265,266,267};
    const unsigned H = 0x100000;
    unsigned o = (unsigned)code * 512 + (unsigned)(y * 16) + sxo[x];
    return (gbit(gunsmoke_rom_sprites, o + H + 4) << 3) |
           (gbit(gunsmoke_rom_sprites, o + H) << 2) |
           (gbit(gunsmoke_rom_sprites, o + 4) << 1) |
            gbit(gunsmoke_rom_sprites, o);
}

static void decode_gfx(void)
{
    for (int c=0; c<NCHAR; c++)
        for (int y=0; y<8; y++)
            for (int x=0; x<8; x++)
                dec_char[c*64 + y*8 + x] = (uint8_t)char_pix(c, x, y);
    for (int c=0; c<NTILE; c++)
        for (int y=0; y<32; y++)
            for (int x=0; x<32; x++)
                dec_tile[c*1024 + y*32 + x] = (uint8_t)tile_pix(c, x, y);
    for (int c=0; c<NSPR; c++)
        for (int y=0; y<16; y++)
            for (int x=0; x<16; x++)
                dec_spr[c*256 + y*16 + x] = (uint8_t)spr_pix(c, x, y);
    gfx_decoded = 1;
}

static inline void put_abs(int absx, int absy, uint8_t pen)
{
    int x = absy - YOFF;
    int y = 255 - absx;
    if ((unsigned)x < GAME_W && (unsigned)y < GAME_H)
        game_frame[y * GAME_W + x] = pen;
}

static void composite(MY_LITTLE_Z80 *z)
{
    const unsigned char *proms = gunsmoke_rom_proms;
    int sx = gunsmoke_scrollx() & 0xffff;
    int sy = gunsmoke_scrolly() & 0xff;
    int chon = gunsmoke_chon();
    int bgon = gunsmoke_bgon();
    int objon = gunsmoke_objon();
    int s3 = gunsmoke_sprite3bank();

    if (!gfx_decoded) decode_gfx();
    memset(game_frame, 0, GAME_W * GAME_H);

    /* BG tilemap -- per-TILE, not per-pixel (mirrors c1943_render.c): the tile
     * attr/code lookup and the code*1024 / fy*32 / color*16 MULUs are hoisted to each
     * 32px tile boundary, and the rotated dest walks a constant -GAME_W stride so the
     * inner pixel loop has NO MULU. The screen is already zeroed by the memset above, so
     * bg-off is a plain skip (the old per-pixel put_abs(0) was redundant). */
    if (bgon) for (int gy=0; gy<SH; gy++) {
        int absy = gy + YOFF;
        int wy   = (absy + sy) & 0xff;
        int row  = (wy >> 5) & 7;
        int iy   = wy & 31;
        int xcol = gy;                              /* dest x == absy - YOFF */
        int gx = 0;
        while (gx < SW) {
            int wx  = (gx + sx) & 0xffff;
            int col = (wx >> 5) & 2047;
            int sub = wx & 31;
            int off = (col * 8 + row) * 2;
            int attr = gunsmoke_rom_bgmap[off + 1];
            int code = gunsmoke_rom_bgmap[off] + ((attr & 1) << 8);
            int color = (attr & 0x3c) >> 2;
            int fy = (attr & 0x80) ? 31 - iy : iy;
            const unsigned char *trow = dec_tile + (size_t)(code & (NTILE - 1)) * 1024 + fy * 32;
            const unsigned char *cb  = proms + 0x400 + color * 16;
            const unsigned char *cb2 = proms + 0x500 + color * 16;
            int fxflip = attr & 0x40;
            unsigned char *d = game_frame + (size_t)(255 - gx) * GAME_W + xcol;
            for (; sub < 32 && gx < SW; sub++, gx++, d -= GAME_W) {
                int fx = fxflip ? 31 - sub : sub;
                int pix = trow[fx];
                *d = (unsigned char)((cb[pix] & 0xf) | ((cb2[pix] & 3) << 4));
            }
        }
    }

    if (objon) for (int offs=0x1000-32; offs>=0; offs-=32) {
        int a = 0xf000 + offs;
        int attr = gunsmoke_peek(z, a + 1);
        int bank = (attr & 0xc0) >> 6;
        int code = gunsmoke_peek(z, a);
        int color = attr & 0x0f;
        int flipy = attr & 0x10;
        int sx0 = gunsmoke_peek(z, a + 3) - ((attr & 0x20) << 3);
        int sy0 = gunsmoke_peek(z, a + 2);
        if (bank == 3) bank += s3;
        code += 256 * bank;
        for (int py=0; py<16; py++) for (int px=0; px<16; px++) {
            int fy = flipy ? 15 - py : py;
            int pix = dec_spr[(code & (NSPR - 1)) * 256 + fy * 16 + px];
            if (!pix) continue;
            int ind = 0x80 | (proms[0x600 + color * 16 + pix] & 0xf) |
                      ((proms[0x700 + color * 16 + pix] & 7) << 4);
            put_abs(sx0 + px, sy0 + py, (uint8_t)ind);
        }
    }

    /* char/text layer -- per-TILE (8x8): attr/code/color hoisted to each 8px tile,
     * rotated dest walks -GAME_W. Transparent (lut==0x0f) pixels are skipped. */
    if (chon) for (int gy=0; gy<SH; gy++) {
        int absy = gy + YOFF;
        int iy   = absy & 7;
        int xcol = gy;
        int rowbase = (absy >> 3) * 32;
        int gx = 0;
        while (gx < SW) {
            int col = gx >> 3;
            int sub = gx & 7;
            int idx = rowbase + col;
            int attr = gunsmoke_peek(z, 0xd400 + idx);
            int code = gunsmoke_peek(z, 0xd000 + idx) + ((attr & 0xe0) << 2);
            int color = attr & 0x1f;
            const unsigned char *crow = dec_char + (size_t)(code & (NCHAR - 1)) * 64 + iy * 8;
            const unsigned char *cb = proms + 0x300 + color * 4;
            unsigned char *d = game_frame + (size_t)(255 - gx) * GAME_W + xcol;
            for (; sub < 8 && gx < SW; sub++, gx++, d -= GAME_W) {
                int pix = crow[sub];
                int lut = cb[pix] & 0xf;
                if (lut != 0x0f) *d = (unsigned char)(0x40 | lut);
            }
        }
    }
}

static void upload_palette(void)
{
    const unsigned char *p = gunsmoke_rom_proms;
    loadrgb[0] = (256UL << 16) | 0;
    for (int i=0; i<256; i++) {
        uint8_t r = (uint8_t)((p[0x000 + i] & 0xf) * 17);
        uint8_t g = (uint8_t)((p[0x100 + i] & 0xf) * 17);
        uint8_t b = (uint8_t)((p[0x200 + i] & 0xf) * 17);
        loadrgb[1 + i*3 + 0] = ((uint32_t)r) * 0x01010101UL;
        loadrgb[1 + i*3 + 1] = ((uint32_t)g) * 0x01010101UL;
        loadrgb[1 + i*3 + 2] = ((uint32_t)b) * 0x01010101UL;
    }
    loadrgb[1 + 256*3] = 0;
    LoadRGB32(&scr->ViewPort, loadrgb);
}

int gunsmoke_rtg_backend_init(void)
{
    if (!game_frame) {
        game_frame = (uint8_t *)AllocMem(GAME_W * GAME_H, MEMF_FAST | MEMF_CLEAR);
        if (!game_frame)
            game_frame = (uint8_t *)AllocMem(GAME_W * GAME_H, MEMF_PUBLIC | MEMF_CLEAR);
    }
    if (!game_frame)
        return 0;
    if (!gfx_decoded)
        decode_gfx();
    return 1;
}

void gunsmoke_rtg_backend_shutdown(void)
{
    if (game_frame) {
        FreeMem(game_frame, GAME_W * GAME_H);
        game_frame = 0;
    }
}

void gunsmoke_rtg_backend_palette(uint8_t *rgb)
{
    const unsigned char *p = gunsmoke_rom_proms;
    for (int i = 0; i < 256; i++) {
        rgb[i*3 + 0] = (uint8_t)((p[0x000 + i] & 0xf) * 17);
        rgb[i*3 + 1] = (uint8_t)((p[0x100 + i] & 0xf) * 17);
        rgb[i*3 + 2] = (uint8_t)((p[0x200 + i] & 0xf) * 17);
    }
}

const uint8_t *gunsmoke_rtg_backend_frame(MY_LITTLE_Z80 *z)
{
    if (!gunsmoke_rtg_backend_init())
        return 0;
    composite(z);
    return game_frame;
}

static void poll_window_keys(void)
{
    struct IntuiMessage *msg;
    if (!win || !win->UserPort) return;
    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cls = msg->Class;
        UWORD raw = msg->Code;
        ReplyMsg((struct Message *)msg);
        if (cls == IDCMP_RAWKEY) {
            keydown[raw & 0x7f] = (raw & 0x80) ? 0 : 1;
        }
    }
}

static void scale_native_to_window(void)
{
    memset(win_frame, 0, (unsigned)win_w * (unsigned)win_h);
    for (int y=0; y<draw_h; y++) {
        int sy = (y * GAME_H) / draw_h;
        uint8_t *dst = win_frame + (draw_y0 + y) * win_w + draw_x0;
        const uint8_t *src = game_frame + sy * GAME_W;
        for (int x=0; x<draw_w; x++)
            dst[x] = src[(x * GAME_W) / draw_w];
    }
}

void gunsmoke_rtg_read_inputs(void)
{
    unsigned char sys = 0xff, p1 = 0xff;
    static int prev_pause;

    poll_window_keys();
    unsigned v = JOY1DAT;
    int right = (v >> 1) & 1;
    int left  = (v >> 9) & 1;
    int down  = ((v >> 1) ^ v) & 1;
    int up    = ((v >> 9) ^ (v >> 8)) & 1;
    int fire  = !(CIAA_PRA & 0x80) || keydown[KEY_SPACE] || keydown[KEY_LCTRL];

    if (keydown[KEY_RIGHT]) right = 1;
    if (keydown[KEY_LEFT])  left = 1;
    if (keydown[KEY_DOWN])  down = 1;
    if (keydown[KEY_UP])    up = 1;

    if (right) p1 &= (unsigned char)~0x01;
    if (left)  p1 &= (unsigned char)~0x02;
    if (down)  p1 &= (unsigned char)~0x04;
    if (up)    p1 &= (unsigned char)~0x08;
    if (fire)  p1 &= (unsigned char)~0x20;

    if (keydown[KEY_5] || keydown[KEY_BACKSPACE]) sys &= (unsigned char)~0x40;
    if (keydown[KEY_6])                            sys &= (unsigned char)~0x80;
    if (keydown[KEY_1] || keydown[KEY_RETURN])    sys &= (unsigned char)~0x01;
    if (keydown[KEY_2])                            sys &= (unsigned char)~0x02;
    if (keydown[KEY_Q] || keydown[KEY_ESC])       exit_request = 1;
    if (keydown[KEY_P] && !prev_pause)            paused = !paused;
    prev_pause = keydown[KEY_P] ? 1 : 0;

    gunsmoke_set_inputs(sys, p1, 0xff);
}

int gunsmoke_rtg_paused(void) { return paused; }
int gunsmoke_rtg_exit_requested(void) { return exit_request; }

void gunsmoke_rtg_open(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 40);
    if (!IntuitionBase || !GfxBase) return;

    game_frame = (uint8_t *)AllocMem(GAME_W * GAME_H, MEMF_FAST | MEMF_CLEAR);
    if (!game_frame) game_frame = (uint8_t *)AllocMem(GAME_W * GAME_H, MEMF_PUBLIC | MEMF_CLEAR);

    scr = LockPubScreen(0);
    if (!scr) return;
    pubscreen_locked = 1;

    win_w = scr->Width;
    win_h = scr->Height;
    if (win_w <= 0 || win_h <= 0) return;

    draw_x0 = 0;
    draw_y0 = 0;
    draw_w = win_w;
    draw_h = win_h;

    win_frame = (uint8_t *)AllocMem((unsigned)win_w * (unsigned)win_h, MEMF_FAST | MEMF_CLEAR);
    if (!win_frame) win_frame = (uint8_t *)AllocMem((unsigned)win_w * (unsigned)win_h, MEMF_PUBLIC | MEMF_CLEAR);
    if (!game_frame || !win_frame) return;

    win = OpenWindowTags(0,
                         WA_PubScreen, (ULONG)scr,
                         WA_Left, 0,
                         WA_Top, 0,
                         WA_Width, win_w,
                         WA_Height, win_h,
                         WA_Borderless, TRUE,
                         WA_Backdrop, FALSE,
                         WA_Activate, TRUE,
                         WA_RMBTrap, TRUE,
                         WA_IDCMP, IDCMP_RAWKEY,
                         TAG_END);
    if (win) {
        WindowToFront(win);
        ActivateWindow(win);
    }
    upload_palette();
    rtg_ok = 1;
}

void gunsmoke_rtg_frame(MY_LITTLE_Z80 *z)
{
    if (!rtg_ok) return;
    composite(z);
    scale_native_to_window();
    if (win && win->RPort)
        WriteChunkyPixels(win->RPort, 0, 0, win_w - 1, win_h - 1, win_frame, win_w);
    WaitTOF();
}
