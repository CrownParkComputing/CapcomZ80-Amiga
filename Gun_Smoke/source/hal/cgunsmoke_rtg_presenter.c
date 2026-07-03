/* cgunsmoke_rtg_presenter.c -- 1943-style RTG presenter for Gun.Smoke.
 * Interpreted main/audio Z80s + YM2203 + RTG chunky painter.
 */
#include <exec/exec.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <stdint.h>
#include <string.h>
#include "capcom_z80_video.h"
#include "z80emu.h"
#ifndef GUNSMOKE_NO_EMBEDDED_INTRO
#include "arcade_intro.h"
#else
#define AI_CD32_PLAY      0x02
#define AI_CD32_LSHOULDER 0x04
#define AI_CD32_RSHOULDER 0x08
#define AI_CD32_GREEN     0x10
#define AI_CD32_YELLOW    0x20
#define AI_CD32_RED       0x40
#define AI_CD32_BLUE      0x80
static unsigned ai_read_cd32_port1(void) { return 0; }
static int ai_cd32_exit_combo(unsigned cd32) { (void)cd32; return 0; }
#endif

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;
struct Device *TimerBase = 0;

#define RTG_W 864
#define RTG_H 486
#define RTG_MODE_ID 0x50FF1000UL
#define GAME_W 224
#define GAME_H 256
#define PLAY_SCALE_NUM 7
#define PLAY_SCALE_DEN 4

extern const unsigned char gunsmoke_rom_main[];
extern const unsigned char gunsmoke_rom_snd[];
extern const unsigned char ai_default_mod[], ai_default_mod_end[];
extern const unsigned char gunsmoke_rtg_bezel[], gunsmoke_rtg_bezel_end[];
extern void gunsmoke_load(const unsigned char *maincpu);
extern void gunsmoke_init(MY_LITTLE_Z80 *z);
extern void gunsmoke_run_frame(MY_LITTLE_Z80 *z);
extern void gunsmoke_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2);
extern void gunsmoke_set_dsw(unsigned char d1, unsigned char d2);
extern void gunsmoke_audio_init(const unsigned char *snd);
extern void gunsmoke_audio_amiga_open(void);
extern void gunsmoke_audio_amiga_frame(void);
extern void gunsmoke_audio_amiga_close(void);
extern void gunsmoke_audio_shutdown(void);
extern int gunsmoke_rtg_backend_init(void);
extern void gunsmoke_rtg_backend_shutdown(void);
extern void gunsmoke_rtg_backend_palette(uint8_t *rgb);
extern const uint8_t *gunsmoke_rtg_backend_frame(MY_LITTLE_Z80 *z);

static struct Screen *scr;
static struct Window *win;
static struct MsgPort *timer_port;
static struct timerequest *timer_io;
static MY_LITTLE_Z80 z;
static uint8_t *rtg_frame;
static uint32_t loadrgb[1 + 256 * 3 + 1];
static uint8_t game_rgb[256 * 3];
static uint8_t game_pen[256];           /* RGB332 pen per game-pal index -- kills per-pixel rgb332 */
static int col_x[RTG_W], row_y[RTG_H];  /* NN-scale geometry LUTs -- kill the per-pixel DIVU */
static unsigned char keydown[128];
static ULONG frame_ticks, next_tick;
static int gx0, gy0, play_w, play_h, g_pause, ok, audio_opened, cleaned, intro_decoded, intro_failed;
static uint8_t dsw1 = 0xf7, dsw2 = 0xff;
unsigned char hal_quit = 0;

#define JOY1DAT  (*(volatile unsigned short *)0xdff00cUL)
#define CIAA_PRA (*(volatile unsigned char  *)0xbfe001UL)
#define PORT1_FIRE 0x80

#define RK_1 0x01
#define RK_2 0x02
#define RK_5 0x05
#define RK_6 0x06
#define RK_Q 0x10
#define RK_P 0x19
#define RK_X 0x32
#define RK_SPACE 0x40
#define RK_BACKSPACE 0x41
#define RK_RETURN 0x44
#define RK_ESC 0x45
#define RK_F10 0x59
#define RK_LCTRL 0x63
#define RK_LALT 0x64
#define RK_UP 0x4c
#define RK_DOWN 0x4d
#define RK_RIGHT 0x4e
#define RK_LEFT 0x4f

static void close_timer(void)
{
    if (timer_io) {
        if (TimerBase) CloseDevice((struct IORequest *)timer_io);
        DeleteIORequest((struct IORequest *)timer_io);
        timer_io = 0;
    }
    if (timer_port) { DeleteMsgPort(timer_port); timer_port = 0; }
    TimerBase = 0;
    frame_ticks = next_tick = 0;
}

static void open_timer(void)
{
    struct EClockVal ev;
    ULONG rate;
    timer_port = CreateMsgPort();
    if (!timer_port) return;
    timer_io = (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_io));
    if (!timer_io) { close_timer(); return; }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)timer_io, 0) != 0) {
        close_timer();
        return;
    }
    TimerBase = timer_io->tr_node.io_Device;
    rate = ReadEClock(&ev);
    frame_ticks = (rate + 30) / 60;
    if (frame_ticks < 1) frame_ticks = 1;
    next_tick = ev.ev_lo;
}

static void frame_pace(void)
{
    struct EClockVal ev;
    ULONG now;
    if (!TimerBase || !frame_ticks) { WaitTOF(); return; }
    ReadEClock(&ev);
    now = ev.ev_lo;
    if ((LONG)(now - next_tick) > (LONG)frame_ticks) {
        next_tick = now;
        return;
    }
    next_tick += frame_ticks;
    do {
        ReadEClock(&ev);
        now = ev.ev_lo;
    } while ((LONG)(now - next_tick) < 0);
}

static void upload_palette(void)
{
    gunsmoke_rtg_backend_palette(game_rgb);
    loadrgb[0] = (256u << 16) | 0u;
    for (int i = 0; i < 256; i++) {
        unsigned r = ((unsigned)(i >> 5) * 255u) / 7u;
        unsigned g = ((unsigned)((i >> 2) & 7) * 255u) / 7u;
        unsigned b = ((unsigned)(i & 3) * 255u) / 3u;
        loadrgb[1 + i*3 + 0] = (uint32_t)r * 0x01010101u;
        loadrgb[1 + i*3 + 1] = (uint32_t)g * 0x01010101u;
        loadrgb[1 + i*3 + 2] = (uint32_t)b * 0x01010101u;
    }
    loadrgb[1 + 256*3] = 0;
    LoadRGB32(&scr->ViewPort, loadrgb);
    for (int i = 0; i < 256; i++)           /* game-pal index -> RGB332 pen, once */
        game_pen[i] = capcom_z80_rgb332(game_rgb[i*3 + 0], game_rgb[i*3 + 1], game_rgb[i*3 + 2]);
}

static void draw_bezel(void)
{
    if (!rtg_frame) return;
    if ((size_t)(gunsmoke_rtg_bezel_end - gunsmoke_rtg_bezel) >= (size_t)RTG_W * RTG_H)
        memcpy(rtg_frame, gunsmoke_rtg_bezel, (size_t)RTG_W * RTG_H);
    else
        memset(rtg_frame, 0, (size_t)RTG_W * RTG_H);
}

static void present_full(void)
{
    if (win && rtg_frame)
        WriteChunkyPixels(win->RPort, 0, 0, RTG_W - 1, RTG_H - 1, rtg_frame, RTG_W);
}

static void present_game(const uint8_t *src)
{
    if (!src || !rtg_frame || !win) return;
    int last_sy = -1;
    for (int y = 0; y < play_h; y++) {
        uint8_t *d = rtg_frame + (size_t)(gy0 + y) * RTG_W + gx0;
        int sy = row_y[y];
        if (sy == last_sy) { memcpy(d, d - RTG_W, play_w); continue; } /* same src row: dup */
        last_sy = sy;
        const uint8_t *s = src + (size_t)sy * GAME_W;
        for (int x = 0; x < play_w; x++) d[x] = game_pen[s[col_x[x]]];
    }
    WriteChunkyPixels(win->RPort, gx0, gy0, gx0 + play_w - 1, gy0 + play_h - 1,
                      rtg_frame + (size_t)gy0 * RTG_W + gx0, RTG_W);
}

#ifndef GUNSMOKE_NO_EMBEDDED_INTRO
static void apply_dips(void *ctx)
{
    (void)ctx;
    gunsmoke_set_dsw(dsw1, dsw2);
}

static const ai_dip_opt dip_coin[] = {
    {0x00,"4C 1C"}, {0x01,"3C 1C"}, {0x02,"2C 1C"}, {0x07,"1C 1C"},
    {0x06,"1C 2C"}, {0x05,"1C 3C"}, {0x04,"1C 4C"}, {0x03,"1C 5C"}
};
static const ai_dip_opt dip_cab[] = { {0x00,"UPRIGHT"}, {0x08,"COCKTAIL"} };
static const ai_dip_opt dip_flip[] = { {0x40,"OFF"}, {0x00,"ON"} };
static const ai_dip_opt dip_demo[] = { {0x80,"ON"}, {0x00,"OFF"} };
static const ai_dip_opt dip_diff[] = { {0x00,"EASY"}, {0x04,"NORMAL"}, {0x08,"HARD"}, {0x0c,"HARDEST"} };
static const ai_dip_opt dip_lives[] = { {0x03,"3"}, {0x02,"2"}, {0x01,"4"}, {0x00,"5"} };
static const ai_dip_item dip_items[] = {
    {"COIN A",0,0x07,8,dip_coin},
    {"CABINET",0,0x08,2,dip_cab},
    {"FLIP SCREEN",0,0x40,2,dip_flip},
    {"LIVES",1,0x03,4,dip_lives},
    {"DIFFICULTY",1,0x0c,4,dip_diff},
    {"DEMO SOUNDS",1,0x80,2,dip_demo}
};
static const ai_dip_config dip_cfg = {
    dip_items, (int)(sizeof dip_items / sizeof dip_items[0]), &dsw1, &dsw2, apply_dips, 0
};

static int intro_ready(void *ctx) { (void)ctx; return intro_decoded; }
static int intro_failed_cb(void *ctx) { (void)ctx; return intro_failed; }
static const char *intro_status(void *ctx)
{
    (void)ctx;
    return intro_decoded ? "READY" : "DECODING GFX";
}
static void intro_warmup(void *ctx)
{
    (void)ctx;
    if (!intro_decoded) {
        intro_decoded = gunsmoke_rtg_backend_init();
        if (!intro_decoded) intro_failed = 1;
        return;
    }
    gunsmoke_run_frame(&z);
}
static const char *const intro_keys[] = {
    "ARROWS MOVE", "CTRL / SPACE STRAIGHT SHOT", "ALT LEFT SHOT   X RIGHT SHOT",
    "5 COIN   1 START", "F10 DIP   P PAUSE   ESC EXIT", 0
};
static const char *const intro_pad[] = {
    "STICK MOVE", "RED STRAIGHT", "BLUE LEFT SHOT   YELLOW / GREEN RIGHT SHOT",
    "L / R COIN   PLAY START", "L + R + PLAY DIP SWITCHES", 0
};
static const ai_config intro_cfg = {
    "GUN.SMOKE",
    "WHITTY ARCADE PRESENTS GUN.SMOKE    CAPCOM 1985 Z80 HARDWARE    MAIN Z80 INTERPRETER WITH BANKED ROMS    SOUND BOARD IS Z80 PLUS TWO YM2203 CHIPS MIXED TO PAULA    RTG OUTPUT USES A CENTRED BEZEL PAINTER WITH FULL CD32 PAD SUPPORT F10 DIP SWITCHES AND DIRECT PLAY WINDOW REFRESH    PRESS FIRE OR START WHEN READY    ",
    intro_keys, intro_pad,
    ai_default_mod, ai_default_mod_end,
    0, 120,
    intro_ready, intro_warmup, 0,
    &dip_cfg, intro_status, intro_failed_cb
};
#endif

static void poll_window_keys(void)
{
    struct IntuiMessage *m;
    if (!win || !win->UserPort) return;
    while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cls = m->Class;
        UWORD raw = m->Code;
        ReplyMsg((struct Message *)m);
        if (cls == IDCMP_RAWKEY)
            keydown[raw & 0x7f] = (raw & 0x80) ? 0 : 1;
    }
}

static void poll_input(void)
{
    static int prev_p, prev_f10, prev_dip, exit_hold;
    static int coin_prev, start_prev, coin_hold, start_hold;
    int aim;
    poll_window_keys();

    unsigned cd32 = ai_read_cd32_port1();
#ifndef GUNSMOKE_NO_EMBEDDED_INTRO
    int dip_now = ai_cd32_dip_combo(cd32);
    if ((keydown[RK_F10] && !prev_f10) || (dip_now && !prev_dip)) {
        if (audio_opened) { gunsmoke_audio_amiga_close(); audio_opened = 0; }
        ai_dip_open(&dip_cfg);
        upload_palette();
        draw_bezel();
        present_full();
        gunsmoke_audio_amiga_open();
        audio_opened = 1;
        keydown[RK_F10] = 0;
    }
    prev_f10 = keydown[RK_F10];
    prev_dip = dip_now;
#endif

    if (keydown[RK_P] && !prev_p) g_pause = !g_pause;
    prev_p = keydown[RK_P];
    if (keydown[RK_ESC] || keydown[RK_Q]) hal_quit = 1;
    if (ai_cd32_exit_combo(cd32)) { if (++exit_hold >= 60) hal_quit = 1; }
    else exit_hold = 0;

    unsigned v = JOY1DAT;
    int right = (v >> 1) & 1;
    int left  = (v >> 9) & 1;
    int down  = ((v >> 1) ^ v) & 1;
    int up    = ((v >> 9) ^ (v >> 8)) & 1;
    if (keydown[RK_RIGHT]) right = 1;
    if (keydown[RK_LEFT])  left = 1;
    if (keydown[RK_DOWN])  down = 1;
    if (keydown[RK_UP])    up = 1;
    aim = 0;
    if (right && !left) aim = 1;
    else if (left && !right) aim = -1;

    int generic_fire = !(CIAA_PRA & PORT1_FIRE) || keydown[RK_SPACE] || keydown[RK_LCTRL];
    int fire_l = (cd32 & AI_CD32_BLUE) || keydown[RK_LALT] || (generic_fire && aim < 0);
    int fire_c = (cd32 & AI_CD32_RED) || (generic_fire && aim == 0);
    int fire_r = (cd32 & (AI_CD32_YELLOW | AI_CD32_GREEN)) || keydown[RK_X] || (generic_fire && aim > 0);

    int coin = keydown[RK_5] || keydown[RK_BACKSPACE] || (cd32 & (AI_CD32_LSHOULDER | AI_CD32_RSHOULDER));
    int start = keydown[RK_1] || keydown[RK_RETURN] || (cd32 & AI_CD32_PLAY);
    if (coin && !coin_prev) coin_hold = 8;
    if (start && !start_prev) start_hold = 8;
    coin_prev = coin;
    start_prev = start;

    uint8_t sys = 0xff, p1 = 0xff;
    if (coin_hold) { sys &= ~0x40; coin_hold--; }
    if (start_hold) { sys &= ~0x01; start_hold--; }
    if (keydown[RK_6]) sys &= ~0x80;
    if (keydown[RK_2]) sys &= ~0x02;
    if (right) p1 &= ~0x01;
    if (left)  p1 &= ~0x02;
    if (down)  p1 &= ~0x04;
    if (up)    p1 &= ~0x08;
    if (fire_l) p1 &= ~0x10;
    if (fire_c) p1 &= ~0x20;
    if (fire_r) p1 &= ~0x40;
    gunsmoke_set_inputs(sys, p1, 0xff);
    gunsmoke_set_dsw(dsw1, dsw2);
}

static void shutdown(void)
{
    if (cleaned) return;
    cleaned = 1;
    if (audio_opened) { gunsmoke_audio_amiga_close(); audio_opened = 0; }
    gunsmoke_audio_shutdown();
    gunsmoke_rtg_backend_shutdown();
    close_timer();
    if (win) { CloseWindow(win); win = 0; }
    if (rtg_frame) { FreeMem(rtg_frame, RTG_W * RTG_H); rtg_frame = 0; }
    if (scr) { CloseScreen(scr); scr = 0; }
    if (GfxBase) { CloseLibrary((struct Library *)GfxBase); GfxBase = 0; }
    if (IntuitionBase) { CloseLibrary((struct Library *)IntuitionBase); IntuitionBase = 0; }
    ok = 0;
}

void hal_cleanup(void) { shutdown(); }

void hal_game_init(void)
{
    cleaned = 0;
    intro_decoded = 0;
    intro_failed = 0;
    gunsmoke_load(gunsmoke_rom_main);
    gunsmoke_init(&z);
    gunsmoke_set_dsw(dsw1, dsw2);

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 40);
    if (!IntuitionBase || !GfxBase) return;
    open_timer();

    ULONG mode = BestModeID(BIDTAG_NominalWidth, RTG_W, BIDTAG_NominalHeight, RTG_H,
                            BIDTAG_DesiredWidth, RTG_W, BIDTAG_DesiredHeight, RTG_H,
                            BIDTAG_Depth, 8, TAG_DONE);
    if (mode == INVALID_ID) mode = RTG_MODE_ID;
    if (mode != INVALID_ID)
        scr = OpenScreenTags(0, SA_DisplayID, mode, SA_Width, RTG_W, SA_Height, RTG_H,
                             SA_Depth, 8, SA_Quiet, 1, SA_Type, CUSTOMSCREEN,
                             SA_ShowTitle, 0, TAG_END);
    if (!scr)
        scr = OpenScreenTags(0, SA_Width, RTG_W, SA_Height, RTG_H, SA_Depth, 8,
                             SA_Quiet, 1, SA_Type, CUSTOMSCREEN, SA_ShowTitle, 0, TAG_END);
    if (!scr) return;

    rtg_frame = (uint8_t *)AllocMem(RTG_W * RTG_H, MEMF_FAST | MEMF_CLEAR);
    if (!rtg_frame) rtg_frame = (uint8_t *)AllocMem(RTG_W * RTG_H, MEMF_PUBLIC | MEMF_CLEAR);
    if (!rtg_frame) return;

    play_w = (GAME_W * PLAY_SCALE_NUM) / PLAY_SCALE_DEN;
    play_h = (GAME_H * PLAY_SCALE_NUM) / PLAY_SCALE_DEN;
    if (play_w > RTG_W) { play_w = RTG_W; play_h = (GAME_H * RTG_W) / GAME_W; }
    if (play_h > RTG_H) { play_h = RTG_H; play_w = (GAME_W * RTG_H) / GAME_H; }
    gx0 = (RTG_W - play_w) / 2;
    gy0 = (RTG_H - play_h) / 2;

    /* NN-scale geometry LUTs (built once, play rect is fixed): remove a DIVU + MULU
     * per pixel from present_game -- the per-pixel (x*GAME_W)/play_w was ~206k DIVUs
     * per frame, the dominant cost that made Gun.Smoke far slower than 1943/Kai. */
    for (int x = 0; x < play_w; x++) col_x[x] = (x * GAME_W) / play_w;
    for (int y = 0; y < play_h; y++) row_y[y] = (y * GAME_H) / play_h;

    win = OpenWindowTags(0, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0,
                         WA_Width, RTG_W, WA_Height, RTG_H,
                         WA_Borderless, TRUE, WA_Backdrop, TRUE,
                         WA_Activate, TRUE, WA_RMBTrap, TRUE,
                         WA_IDCMP, IDCMP_RAWKEY, TAG_END);
    if (!win) return;
    ScreenToFront(scr);
    ActivateWindow(win);

#ifndef GUNSMOKE_NO_EMBEDDED_INTRO
    ai_init(scr, win, rtg_frame, RTG_W, RTG_H);
    ai_set_loader_enabled(1);
    if (!ai_run(&intro_cfg)) { hal_quit = 1; return; }
#endif
    memset(keydown, 0, sizeof keydown);

    upload_palette();
    draw_bezel();
    present_full();
    gunsmoke_audio_init(gunsmoke_rom_snd);
    gunsmoke_audio_amiga_open();
    audio_opened = 1;
    ok = 1;
}

void hal_game_frame(void)
{
    poll_input();
    if (hal_quit) { shutdown(); return; }
    if (g_pause) { frame_pace(); return; }
    gunsmoke_run_frame(&z);
    gunsmoke_audio_amiga_frame();
    if (!ok) return;
    present_game(gunsmoke_rtg_backend_frame(&z));
    frame_pace();
}

int hal_game_should_exit(void)
{
    return hal_quit;
}
