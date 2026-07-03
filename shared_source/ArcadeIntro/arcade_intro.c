/* arcade_intro.c -- standalone RTG/chunky loader intro (see arcade_intro.h).
 * Generalised from Tiger-Heli's tigerh_rtg.c loader: animated starfield + iso
 * cube + wavy title/scroller + "DECODING ROMS"->"READY" gate + control hints,
 * with ProTracker music via the shared ptplayer. Draws into the host's chunky
 * framebuffer and presents through Picasso96 WriteChunkyPixels. */
#include "arcade_intro.h"
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

#define CUSTOM_REGS ((volatile uint16_t *)0xdff000)
#define R_DMACON  (0x096/2)
#define R_INTENA  (0x09a/2)
#define R_INTREQ  (0x09c/2)
#define CIAA_PRA  (*(volatile unsigned char *)0xbfe001)
#define CIAA_DDRA (*(volatile unsigned char *)0xbfe201)
#define POTGO     (*(volatile unsigned short *)0xdff034)
#define POTINP    (*(volatile unsigned short *)0xdff016)
#define JOY1DAT   (*(volatile unsigned short *)0xdff00c)
#define P1_FIRE   0x80
#define P1_DATRY  0x4000
#define KEY_1 0x01
#define KEY_2 0x02
#define KEY_SPACE 0x40
#define KEY_RETURN 0x44
#define KEY_ESC 0x45
#define KEY_UP    0x4c
#define KEY_DOWN  0x4d
#define KEY_RIGHT 0x4e
#define KEY_LEFT  0x4f
#define KEY_F10   0x59
#define KEY_LCTRL 0x63

/* shared ptplayer + the supervisor VBR fetch (arcade_intro_glue.s) */
extern void tc_pt_install(void *vbr, long palflag);
extern void tc_pt_init(void *mod, void *samples, long pos);
extern void tc_pt_start(void);
extern void tc_pt_end(void);
extern void tc_pt_remove(void);
extern void tc_pt_mastervol(long vol);
extern void *ai_get_vbr(void) asm("ai_get_vbr");

static struct Screen *g_scr;
static struct Window *g_win;
static uint8_t *g_fb;
static int g_w, g_h;
static int g_tick;
static void *g_mod_chunk;
static long  g_mod_size;
static int   g_music_on;
static int   g_safe_x;
static int   g_force_compact;
static uint32_t loadrgb[1 + 16 * 3 + 1];

/* Present override: RTG/Picasso hosts leave this 0 and the loader presents via
 * WriteChunkyPixels. AGA/chipset hosts call ai_set_present() with a callback that
 * C2Ps the framebuffer into the screen's bitplanes (see ai_set_present). */
static void (*g_present_cb)(void) = 0;
void ai_set_present(void (*cb)(void)) { g_present_cb = cb; }

/* The loader is OPT-IN per game (boot-straight by default) while it's re-validated on real
 * hardware after the 2026-06-27 squash regression. A game calls ai_set_loader_enabled(1)
 * before ai_run to show the loader; default = boot straight to the game. */
static int g_loader_enabled = 0;
void ai_set_loader_enabled(int e) { g_loader_enabled = e; }
void ai_set_safe_margins(int x, int force_compact)
{
    if (x < 0) x = 0;
    if (x > 96) x = 96;
    g_safe_x = x;
    g_force_compact = force_compact ? 1 : 0;
}

/* 3 copper-bar gradient ramps (dark -> peak) + grey/white for text. */
static const uint8_t def_pal[16][3] = {
    {   0,   0,   0 },   /*  0 black / cutout            */
    {   8,  10,  28 },   /*  1 dark navy backdrop        */
    {  24,  40,  90 },   /*  2 blue  ramp  (copper bar A) */
    {  50,  90, 170 },   /*  3                            */
    {  90, 150, 230 },   /*  4                            */
    { 185, 215, 255 },   /*  5 blue  peak                 */
    {  60,  40,   8 },   /*  6 gold  ramp  (copper bar B) */
    { 140,  95,  20 },   /*  7                            */
    { 220, 160,  40 },   /*  8                            */
    { 255, 230, 140 },   /*  9 gold  peak                 */
    {  60,   8,  28 },   /* 10 red   ramp  (copper bar C) */
    { 150,  30,  62 },   /* 11                            */
    { 220,  55,  95 },   /* 12                            */
    { 255, 140, 175 },   /* 13 red   peak                 */
    { 140, 150, 160 },   /* 14 grey                       */
    { 245, 245, 250 },   /* 15 white                      */
};

static const int8_t sin32[32] = {
      0, 12, 24, 35, 45, 53, 59, 63, 64, 63, 59, 53, 45, 35, 24, 12,
      0,-12,-24,-35,-45,-53,-59,-63,-64,-63,-59,-53,-45,-35,-24,-12
};

/* Smooth (linearly-interpolated) sine for fluid motion. Full circle = 32<<8;
 * returns ~sin*64 but continuous between the 32 table entries. */
static int isin(int p)
{
    int i, f, a, b;
    p &= (32 << 8) - 1;
    i = p >> 8;
    f = p & 255;
    a = sin32[i];
    b = sin32[(i + 1) & 31];
    return a + ((b - a) * f) / 256;
}
static int icos(int p) { return isin(p + (8 << 8)); }

static const uint8_t font5x7[][7] = {
    {0,0,0,0,0,0,0},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,14},
    {17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{1,1,1,1,17,17,14},{17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},{0,0,0,31,0,0,0},{0,0,0,0,0,12,12},
};

static int glyph_index(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return 1 + c - 'A';
    if (c >= '0' && c <= '9') return 27 + c - '0';
    if (c == '-') return 37;
    if (c == '.') return 38;
    return 0;
}

static void put_px(int x, int y, uint8_t c)
{
    if ((unsigned)x < (unsigned)g_w && (unsigned)y < (unsigned)g_h)
        g_fb[y * g_w + x] = c;
}

static void fill_rect(int x0, int y0, int w, int h, uint8_t c)
{
    int x1 = x0 + w, y1 = y0 + h;
    if (w <= 0 || h <= 0) return;
    if (x1 <= 0 || y1 <= 0 || x0 >= g_w || y0 >= g_h) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_w) x1 = g_w;
    if (y1 > g_h) y1 = g_h;
    for (int y = y0; y < y1; y++)
        memset(g_fb + y * g_w + x0, c, (size_t)(x1 - x0));
}

static void draw_text(const char *s, int x, int y, int scale, uint8_t c)
{
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = font5x7[glyph_index(*s)];
        for (int gy = 0; gy < 7; gy++)
            for (int gx = 0; gx < 5; gx++)
                if (g[gy] & (1 << (4 - gx)))
                    fill_rect(x + gx * scale, y + gy * scale, scale, scale, c);
    }
}

static int text_width(const char *s, int scale)
{
    int n = 0;
    while (*s++) n++;
    return n * 6 * scale - scale;
}

static void draw_text_center(const char *s, int y, int scale, uint8_t c)
{
    draw_text(s, (g_w - text_width(s, scale)) / 2, y, scale, c);
}
static void draw_text_center_safe(const char *s, int y, int scale, uint8_t c)
{
    int safe = g_safe_x;
    int x = safe + ((g_w - safe * 2) - text_width(s, scale)) / 2;
    if (x < safe) x = safe;
    draw_text(s, x, y, scale, c);
}

static void draw_wavy_text(const char *s, int x, int y, int scale, uint8_t c, int phase)
{
    for (; *s; s++, x += 6 * scale) {
        char one[2] = { *s, 0 };
        int wave = sin32[(phase + x / (scale ? scale : 1)) & 31] / 8;
        draw_text(one, x, y + wave, scale, c);
    }
}

static void draw_wavy_scroller(const char *s, int y, int scale, uint8_t c, int phase)
{
    /* Bright colour-CYCLING marquee that pops against the gold bars, with a BOUNCY
     * compound motion: a big primary sine + a faster secondary bounce. The cycle
     * flows along the text over time (blue->pink->white->red), so it reads clearly
     * "different + animated" vs the old static white. (passed colour c is ignored) */
    static const uint8_t cyc[5] = { 5, 13, 15, 12, 4 };   /* light-blue/pink/white/red/blue, no gold */
    int tw = text_width(s, scale);
    int x = g_w - ((phase * 3) % (tw + g_w + 80));
    int i = 0;
    (void)c;
    for (; *s; s++, x += 6 * scale, i++) {
        if (x < -6 * scale || x >= g_w + 6 * scale) continue;
        char one[2] = { *s, 0 };
        int wave = sin32[(phase + x / (scale ? scale : 1)) & 31] / 4    /* big primary wave */
                 + sin32[(phase * 2 + i * 6) & 31] / 8;                 /* faster bounce    */
        draw_text(one, x, y + wave, scale, cyc[((phase / 5) + i) % 5]);
    }
}

static void draw_starfield(int tick)
{
    for (int i = 0; i < 150; i++) {
        int depth = 1 + (i & 3);
        int x = (i * 97 + tick * depth * 3) % g_w;
        int y = 18 + ((i * 53 + (tick * (depth - 1)) / 2) % (g_h - 120));
        uint8_t c = (depth == 4) ? 15 : (depth == 3 ? 11 : 12);
        put_px(x, y, c);
        if (depth >= 3) put_px(x - 1, y, c);
    }
}

/* One horizontal "copper bar": a full-width band whose colour ramps
 * dark->peak->dark across its height for a rounded metallic look. */
static void draw_bar(int cy, int base, int half)
{
    for (int dy = -half; dy <= half; dy++) {
        int t = half - (dy < 0 ? -dy : dy);          /* 0 at edge .. half at centre */
        int sh = (t * 3) / half; if (sh > 3) sh = 3;
        fill_rect(0, cy + dy, g_w, 1, (uint8_t)(base + sh));
    }
}

/* ---- input ---- */
static int poll_rawkey(unsigned char *code)
{
    struct IntuiMessage *msg;
    if (!g_win || !g_win->UserPort) return 0;
    while ((msg = (struct IntuiMessage *)GetMsg(g_win->UserPort))) {
        ULONG cls = msg->Class; UWORD raw = msg->Code;
        ReplyMsg((struct Message *)msg);
        if (cls == IDCMP_RAWKEY) { *code = (unsigned char)raw; return 1; }
    }
    return 0;
}

unsigned ai_read_cd32_port1(void)
{
    unsigned out = 0; volatile unsigned char t;
    CIAA_DDRA |= P1_FIRE;
    CIAA_PRA  &= (unsigned char)~P1_FIRE;
    POTGO = 0x6f00;
    for (int i = 7; i >= 0; i--) {
        t = CIAA_PRA; t = CIAA_PRA; t = CIAA_PRA; t = CIAA_PRA; (void)t;
        if (!(POTINP & P1_DATRY)) out |= (1u << i);
        CIAA_PRA |= P1_FIRE;
        CIAA_PRA &= (unsigned char)~P1_FIRE;
    }
    CIAA_DDRA &= (unsigned char)~P1_FIRE;
    POTGO = 0xff00;
    CIAA_PRA |= 0xC0;
    {
        int nb = 0;
        for (int b = 0; b < 8; b++)
            if (out & (1u << b))
                nb++;
        if (nb >= 4)
            out = 0;
    }
    return out;
}

int ai_cd32_dip_combo(unsigned cd32)
{
    return (cd32 & (AI_CD32_LSHOULDER | AI_CD32_RSHOULDER | AI_CD32_PLAY)) ==
           (AI_CD32_LSHOULDER | AI_CD32_RSHOULDER | AI_CD32_PLAY);
}

int ai_cd32_exit_combo(unsigned cd32)
{
    return (cd32 & (AI_CD32_BLUE | AI_CD32_RED | AI_CD32_YELLOW | AI_CD32_GREEN)) ==
           (AI_CD32_BLUE | AI_CD32_RED | AI_CD32_YELLOW | AI_CD32_GREEN);
}

/* 1 = proceed, -1 = exit, 0 = nothing. Once READY, ANY key (bar ESC) / fire /
 * pad button proceeds -- be generous so "press fire" always works. */
static int fire_or_exit(void)
{
    unsigned char raw;
    unsigned cd = ai_read_cd32_port1();
    while (poll_rawkey(&raw)) {
        unsigned char c = raw & 0x7f;
        if (raw & 0x80) continue;                       /* key release */
        if (c == KEY_ESC) return -1;                    /* ESC = quit       */
        if (c == KEY_F10) return 2;                     /* F10 = DIP editor */
        return 1;                                       /* any other key = play */
    }
    if (ai_cd32_dip_combo(cd))
        return 2;                                       /* pad L+R+Play = DIP editor */
    if (!(CIAA_PRA & P1_FIRE)) return 1;                /* joystick / CD32 FIR1 */
    if (cd & (AI_CD32_RED | AI_CD32_BLUE | AI_CD32_PLAY)) return 1;
    return 0;
}

/* drain (and ignore) queued raw-keys while the loader is still decoding, so a
 * stray key pressed during "DECODING" doesn't instantly skip once READY. */
static void drain_keys(void)
{
    unsigned char raw;
    while (poll_rawkey(&raw)) { }
}

/* ---- music ---- */
static void music_start(const ai_config *cfg)
{
    volatile uint16_t *c = CUSTOM_REGS;
    if (g_music_on || !cfg->mod || !cfg->mod_end) return;
    g_mod_size = (long)(cfg->mod_end - cfg->mod);
    g_mod_chunk = AllocMem((unsigned long)g_mod_size, MEMF_CHIP);
    if (!g_mod_chunk) return;
    memcpy(g_mod_chunk, cfg->mod, (size_t)g_mod_size);
    {
        APTR oldstk = SuperState();
        void *vbr = ai_get_vbr();
        UserState(oldstk);
        tc_pt_install(vbr, 1);
        tc_pt_init(g_mod_chunk, 0, 0);
        tc_pt_mastervol(48);
        tc_pt_start();
        c[R_INTREQ] = 0x2000;
        c[R_INTENA] = 0xe000;
    }
    g_music_on = 1;
}

static void music_stop(void)
{
    if (g_music_on) {
        tc_pt_end();
        tc_pt_remove();
        CUSTOM_REGS[R_DMACON] = 0x000f;
    }
    g_music_on = 0;
    if (g_mod_chunk) {
        FreeMem(g_mod_chunk, (unsigned long)g_mod_size);
        g_mod_chunk = 0; g_mod_size = 0;
    }
}

static void load_pal16(const unsigned char (*pal)[3])
{
    if (!pal) pal = def_pal;
    loadrgb[0] = (16UL << 16) | 0;
    for (int i = 0; i < 16; i++) {
        loadrgb[1 + i * 3 + 0] = ((uint32_t)pal[i][0]) * 0x01010101UL;
        loadrgb[1 + i * 3 + 1] = ((uint32_t)pal[i][1]) * 0x01010101UL;
        loadrgb[1 + i * 3 + 2] = ((uint32_t)pal[i][2]) * 0x01010101UL;
    }
    loadrgb[1 + 16 * 3] = 0;
    LoadRGB32(&g_scr->ViewPort, loadrgb);
}
static void set_loader_palette(const ai_config *cfg) { load_pal16(cfg->palette); }

static void present(void)
{
    if (g_present_cb) { g_present_cb(); return; }   /* AGA: host C2P into bitplanes */
    WriteChunkyPixels(g_win->RPort, 0, 0, g_w - 1, g_h - 1, g_fb, g_w);
}

/* THREE gold copper bars orbiting the central scroller. The orbit is a smooth
 * interpolated sine (full circle = 32<<8); the three bars are phase-spaced 120
 * apart (+0 / +120 / +240) so they're always evenly distributed around the loop.
 * Each bar keeps the depth trick: in FRONT of the text on the way down (cos>0),
 * BEHIND on the way up (cos<=0). All three use the gold ramp (base 6 -> rounded
 * dark..gold-peak idx 9, the brightest gold). The scroller is drawn between the
 * back bars and the front bars so the depth ordering reads correctly. */
static void draw_gold_bars(const ai_config *cfg, int midY, int amp, int half, int nbars)
{
    int ph   = g_tick * 80;                  /* smooth orbit phase (~2s/turn)        */
    int by[3], front[3], b;
    if (nbars < 1) nbars = 1; if (nbars > 3) nbars = 3;
    int step = (32 << 8) / nbars;            /* evenly distribute the bars around the loop */
    for (b = 0; b < nbars; b++) {
        int phk = ph + b * step;
        by[b]    = midY + 10 + (isin(phk) * amp) / 64;
        front[b] = icos(phk) > 0;
    }
    for (b = 0; b < nbars; b++) if (!front[b]) draw_bar(by[b], 6, half);   /* bars BEHIND the text */
    if (cfg->scroller)
        draw_wavy_scroller(cfg->scroller, midY, 3, 15, g_tick);
    for (b = 0; b < nbars; b++) if ( front[b]) draw_bar(by[b], 6, half);   /* bars IN FRONT */
}

static void draw_loader(const ai_config *cfg, int ready, int blink)
{
    const char *const *p;
    /* The wide layout was authored for a ~864x486 RTG screen (control columns at
     * x=80 / x=g_w-300, a 150px panel, scale-6 title). On a native chipset screen
     * such as Sky Kid / 1943 AGA's 320x256 those constants collapse (columns
     * collide, lists overrun). So small surfaces get a distinct, GENEROUSLY-SPACED
     * single-column layout (title up top, scroller + the 3 gold bars in the upper
     * half, then a well-separated status line and one centred keyboard-hint column);
     * WIDE screens keep the EXACT original constants byte-for-byte. */
    int small = g_force_compact || (g_w < 480 || g_h < 400);

    memset(g_fb, 1, (size_t)g_w * g_h);        /* dark navy backdrop (whole screen) */
    draw_starfield(g_tick);

    if (small) {
        /* ---- compact 320x256 layout, deliberately SIMPLE so it can't squash: a title up
         * top, ONE orbiting gold bar around the scroller in the middle (3 bars buried the
         * text on a small chipset screen), a PRESS FIRE line, and a single short hint. ---- */
        const char *ttl = cfg->title ? cfg->title : "";
        int tscale = 4;
        int safe = g_safe_x ? g_safe_x : 8;
        while (tscale > 1 && text_width(ttl, tscale) > g_w - safe * 2) tscale--;
        draw_wavy_text(ttl, safe + ((g_w - safe * 2) - text_width(ttl, tscale)) / 2,
                       12, tscale, 15, g_tick / 2);
        draw_gold_bars(cfg, g_h / 2 - 6, 22, 7, 1);    /* ONE bar orbiting the scroller, mid-screen */
        draw_text_center_safe(ready ? "PRESS FIRE TO START" : "DECODING ROMS ...",
                              g_h - 58, 2, ready ? (blink ? 15 : 9) : 8);
        if (cfg->key_lines && cfg->key_lines[0])       /* one short control hint, well clear */
            draw_text_center_safe(cfg->key_lines[0], g_h - 32, 1, 14);
        if (cfg->pad_lines && cfg->pad_lines[0])
            draw_text_center_safe(cfg->pad_lines[0], g_h - 18, 1, 11);
        return;
    }

    /* ---- ORIGINAL wide 864x486 layout (positions byte-for-byte unchanged) ---- */
    if (cfg->title)
        draw_wavy_text(cfg->title, (g_w - text_width(cfg->title, 6)) / 2, 30, 6, 15, g_tick / 2);
    draw_gold_bars(cfg, g_h / 2 - 24, 80, 14, 3);

    /* dark bottom panel holds the status line + the well-spaced control lists */
    fill_rect(0, g_h - 150, g_w, 150, 1);
    draw_text_center(ready ? "READY  -  PRESS FIRE OR START" : "DECODING ROMS",
                     g_h - 142, 3, ready ? (blink ? 15 : 9) : 8);
    if (cfg->key_lines) {
        int ly = g_h - 108 + 18;
        draw_text("KEYBOARD", 80, g_h - 108, 1, 9);
        for (p = cfg->key_lines; *p; p++, ly += 14) draw_text(*p, 80, ly, 1, 15);
    }
    if (cfg->pad_lines) {
        int ly = g_h - 108 + 18;
        draw_text("CD32 PAD", g_w - 300, g_h - 108, 1, 9);
        for (p = cfg->pad_lines; *p; p++, ly += 14) draw_text(*p, g_w - 300, ly, 1, 14);
    }
}

/* DIP-switch editor (F10 / pad L+R+Play in the loader). Returns 1 if changed. */
static int ai_dip_menu(const ai_dip_config *d)
{
    int sel = 0, changed = 0, joy_wait = 0;
    for (;;) {
        unsigned char raw;
        int small = (g_w < 480 || g_h < 400);
        memset(g_fb, 1, (size_t)g_w * g_h);
        draw_text_center("DIP SWITCHES", small ? 12 : 34, small ? 2 : 3, 15);
        for (int i = 0; i < d->nitems; i++) {
            const ai_dip_item *it = &d->items[i];
            unsigned char b = (unsigned char)(it->which ? *d->dsw2 : *d->dsw1);
            const char *val = "?";
            int hl = (i == sel);
            int y = small ? (42 + i * 18) : (96 + i * 22);
            for (int o = 0; o < it->nopts; o++)
                if ((it->opts[o].val & it->mask) == (b & it->mask)) { val = it->opts[o].label; break; }
            if (small) {
                draw_text(it->name, 10, y, 1, (uint8_t)(hl ? 15 : 14));
                draw_text(val, 138, y, 1, (uint8_t)(hl ? 9 : 12));
            } else {
                draw_text(it->name, 120, y, 2, (uint8_t)(hl ? 15 : 14));
                draw_text(val, 470, y, 2, (uint8_t)(hl ? 9 : 12));
            }
        }
        draw_text_center(small ? "UP/DOWN SEL  LEFT/RIGHT CHANGE  F10 EXIT"
                               : "UP DOWN SELECT    LEFT RIGHT CHANGE    F10 OR ESC EXIT",
                         g_h - (small ? 16 : 40), 1, 11);
        present();
        WaitTOF();
        if (joy_wait > 0)
            joy_wait--;
        while (poll_rawkey(&raw)) {
            unsigned char c = raw & 0x7f;
            if (raw & 0x80) continue;
            if (c == KEY_ESC || c == KEY_F10) return changed;
            if (c == KEY_UP)   sel = (sel + d->nitems - 1) % d->nitems;
            if (c == KEY_DOWN) sel = (sel + 1) % d->nitems;
            if (c == KEY_LEFT || c == KEY_RIGHT) {
                const ai_dip_item *it = &d->items[sel];
                unsigned char *b = it->which ? d->dsw2 : d->dsw1;
                int cur = 0;
                for (int o = 0; o < it->nopts; o++)
                    if ((it->opts[o].val & it->mask) == (*b & it->mask)) { cur = o; break; }
                cur += (c == KEY_RIGHT) ? 1 : -1;
                if (cur < 0) cur = it->nopts - 1;
                if (cur >= it->nopts) cur = 0;
                *b = (unsigned char)((*b & ~it->mask) | (it->opts[cur].val & it->mask));
                changed = 1;
            }
        }
        if (!joy_wait) {
            unsigned cd = ai_read_cd32_port1();
            unsigned v = JOY1DAT;
            int up = ((v >> 9) ^ (v >> 8)) & 1;
            int down = ((v >> 1) ^ v) & 1;
            int left = (v >> 9) & 1;
            int right = (v >> 1) & 1;
            if (ai_cd32_dip_combo(cd) || (cd & AI_CD32_PLAY))
                return changed;
            if (up) {
                sel = (sel + d->nitems - 1) % d->nitems;
                joy_wait = 10;
            } else if (down) {
                sel = (sel + 1) % d->nitems;
                joy_wait = 10;
            } else if (left || right || (cd & (AI_CD32_RED | AI_CD32_BLUE))) {
                const ai_dip_item *it = &d->items[sel];
                unsigned char *b = it->which ? d->dsw2 : d->dsw1;
                int cur = 0;
                for (int o = 0; o < it->nopts; o++)
                    if ((it->opts[o].val & it->mask) == (*b & it->mask)) { cur = o; break; }
                cur += left ? -1 : 1;
                if (cur < 0) cur = it->nopts - 1;
                if (cur >= it->nopts) cur = 0;
                *b = (unsigned char)((*b & ~it->mask) | (it->opts[cur].val & it->mask));
                changed = 1;
                joy_wait = 10;
            }
        }
    }
}

/* Open the DIP editor on demand (e.g. F10 in-game). Sets the 16-colour loader
 * palette; the caller restores its own palette afterwards. Returns 1 if changed. */
int ai_dip_open(const ai_dip_config *d)
{
    int ch;
    if (!g_scr || !g_win || !g_fb || !d) return 0;
    load_pal16(0);
    ch = ai_dip_menu(d);
    if (ch && d->apply) d->apply(d->ctx);
    return ch;
}

void ai_init(struct Screen *scr, struct Window *win, unsigned char *fb, int w, int h)
{
    g_scr = scr; g_win = win; g_fb = fb; g_w = w; g_h = h;
}

void ai_bezel_blit(unsigned char *fb, const unsigned char *bezel, int w, int h)
{
    if (fb && bezel) memcpy(fb, bezel, (size_t)w * h);
}

void ai_bezel_draw_simple(unsigned char *fb, int w, int h,
                          int game_w, int game_h,
                          const unsigned char *grad_pens, int ngrad,
                          unsigned char frame_pen)
{
    int ox, oy, x0, y0, x1, y1;
    if (!fb || w <= 0 || h <= 0)
        return;
    if (!grad_pens || ngrad <= 0) {
        static const unsigned char fallback = 0;
        grad_pens = &fallback;
        ngrad = 1;
    }
    for (int y = 0; y < h; y++) {
        int band = (y * ngrad) / h;
        if (band < 0) band = 0;
        if (band >= ngrad) band = ngrad - 1;
        memset(fb + (size_t)y * w, grad_pens[band], (size_t)w);
    }

    ox = (w - game_w) / 2;
    oy = (h - game_h) / 2;
    x0 = ox - 2; x1 = ox + game_w + 1;
    y0 = oy - 2; y1 = oy + game_h + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;

    for (int x = x0; x <= x1; x++) {
        fb[(size_t)y0 * w + x] = frame_pen;
        fb[(size_t)y1 * w + x] = frame_pen;
    }
    for (int y = y0; y <= y1; y++) {
        fb[(size_t)y * w + x0] = frame_pen;
        fb[(size_t)y * w + x1] = frame_pen;
    }
}

void ai_blit_1x_at(unsigned char *fb, int w, int h,
                   const unsigned char *src, int sw, int sh, int x, int y)
{
    if (!fb || !src || w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
        return;
    for (int sy = 0; sy < sh; sy++) {
        int dy = y + sy;
        int sx = 0, dx = x, n = sw;
        if ((unsigned)dy >= (unsigned)h)
            continue;
        if (dx < 0) {
            sx = -dx;
            n -= sx;
            dx = 0;
        }
        if (dx + n > w) n = w - dx;
        if (n > 0)
            memcpy(fb + (size_t)dy * w + dx, src + (size_t)sy * sw + sx, (size_t)n);
    }
}

void ai_blit_1x_center(unsigned char *fb, int w, int h,
                       const unsigned char *src, int sw, int sh)
{
    ai_blit_1x_at(fb, w, h, src, sw, sh, (w - sw) / 2, (h - sh) / 2);
}

void ai_blit_2x_at(unsigned char *fb, int w, int h,
                   const unsigned char *src, int sw, int sh, int x, int y)
{
    if (!fb || !src || w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
        return;
    for (int sy = 0; sy < sh; sy++) {
        int dy0 = y + sy * 2;
        int dy1 = dy0 + 1;
        for (int sx = 0; sx < sw; sx++) {
            int dx0 = x + sx * 2;
            unsigned char p = src[(size_t)sy * sw + sx];
            if ((unsigned)dy0 < (unsigned)h) {
                if ((unsigned)dx0 < (unsigned)w) fb[(size_t)dy0 * w + dx0] = p;
                if ((unsigned)(dx0 + 1) < (unsigned)w) fb[(size_t)dy0 * w + dx0 + 1] = p;
            }
            if ((unsigned)dy1 < (unsigned)h) {
                if ((unsigned)dx0 < (unsigned)w) fb[(size_t)dy1 * w + dx0] = p;
                if ((unsigned)(dx0 + 1) < (unsigned)w) fb[(size_t)dy1 * w + dx0 + 1] = p;
            }
        }
    }
}

int ai_run(const ai_config *cfg)
{
    int min_ticks = cfg->min_ticks > 0 ? cfg->min_ticks : 120;
    if (!g_scr || !g_win || !g_fb) return 1;

    g_tick = 0;
    if (!g_loader_enabled) {
        /* DEFAULT = boot straight (loader opt-in while re-validated after the squash
         * regression): run warmup until decoded, then proceed -- no loader, no music. */
        (void)min_ticks;
        if (cfg->warmup) {
            int guard = 0;
            do { cfg->warmup(cfg->ctx); }
            while (cfg->ready && !cfg->ready(cfg->ctx) && ++guard < 4000);
        }
        return 1;
    }

    /* ---- LOADER path (opt-in via ai_set_loader_enabled) ---- */
    set_loader_palette(cfg);
    music_start(cfg);
    for (;;) {
        int decoded = (!cfg->ready || cfg->ready(cfg->ctx));
        int ready = decoded && (g_tick >= min_ticks);
        draw_loader(cfg, ready, ((g_tick / 24) & 1) == 0);
        present();
        if (cfg->warmup) cfg->warmup(cfg->ctx);
        g_tick++;
        WaitTOF();
        if (ready) {
            int r = fire_or_exit();
            if (r == 2) {
                if (cfg->dip && ai_dip_menu(cfg->dip) && cfg->dip->apply)
                    cfg->dip->apply(cfg->dip->ctx);
                set_loader_palette(cfg);
                drain_keys();
            } else if (r) { music_stop(); return r > 0; }
        } else {
            drain_keys();
        }
    }
}
