/* sidearms_aga_main.c -- Side Arms 8-bit RTG presenter.
 *
 * The game and bezel share a fixed RGB332 palette. That is lower colour fidelity than
 * truecolour, but it avoids the expensive/dangerous dynamic palette remapping that
 * caused flashing sprite blocks and audio underruns on the large bezel screen. */
#include <exec/exec.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <devices/timer.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <stdint.h>
#include <string.h>
#include "z80emu.h"
#include "sidearms_machine.h"
#include "sidearms_render.h"
#include "arcade_intro.h"
#include "gaplus_menu.h"
#include "sidearms_rtg_bezel.h"     /* SA_RTG_W/H + SA_GAME_X/Y/W/H (playfield hole) */

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase = 0;

/* ---- cybergraphics.library (no SDK header in this toolchain -- see sidearms_cgx.s) ---- */
void *CyberGfxBase = 0;
extern unsigned long BestCModeIDTagList(struct TagItem *taglist);
extern unsigned long WritePixelArray(void *src, unsigned long sx, unsigned long sy,
    unsigned long srcmod, void *rastport, unsigned long dx, unsigned long dy,
    unsigned long w, unsigned long h, unsigned long fmt);
#define CYBRBIDTG_Depth         (0x80050000UL + 0)
#define CYBRBIDTG_NominalWidth  (0x80050000UL + 1)
#define CYBRBIDTG_NominalHeight (0x80050000UL + 2)
#define RECTFMT_RGB   0                              /* 3 bytes/pixel R,G,B */
#define CGX_INVALID_ID 0xFFFFFFFFUL

extern const unsigned char sidearms_maincpu[];
extern const unsigned char sidearms_chars[];
extern const unsigned char sidearms_tiles[];
extern const unsigned char sidearms_sprites[];
extern const unsigned char sidearms_tilemap[];
extern const unsigned char sidearms_snd[];          /* sound Z80 program (bd-06.1l) */
extern const unsigned char sidearms_rtg_bezel[], sidearms_rtg_bezel_end[];
extern const unsigned char ai_default_mod[], ai_default_mod_end[];

/* sound chain: audio Z80 + 2x YM2203 -> Paula (sidearms_audio.c + _amiga.c) */
extern void sidearms_audio_init(const unsigned char *snd);
extern void sa_audio_amiga_open(void);
extern void sa_audio_amiga_frame(void);
extern void sa_audio_amiga_close(void);
extern void sidearms_audio_shutdown(void);          /* free the YM2203 OPN state on exit */

static struct Screen *scr;
static struct Window *win;
static int ok;
static int bezel_active;
static MY_LITTLE_Z80 z;

static uint16_t frame16[SA_NH][SA_NW];
static uint8_t  pen8[1024];              /* live xBRG_444 palette index -> fixed RGB332 pen */
static uint8_t *rtg_frame;               /* indexed full-screen framebuffer (FAST) */
static uint32_t loadrgb[1 + 256 * 3 + 1];
static unsigned disp_w, disp_h;
static int gx, gy, gw, gh;               /* game window on the RTG screen */
static uint32_t pal_csum = 0xffffffff;
static unsigned char keydown[128];
static uint8_t sa_dsw0 = 0xfc, sa_dsw1 = 0xff;
unsigned char hal_quit = 0;

/* ---- 60Hz EClock frame pacer (Ikari): screen-refresh-independent timing ---- */
static struct MsgPort *timer_port;
static struct timerequest *timer_io;
static ULONG frame_ticks, next_tick;

static void close_timer(void){
    if(timer_io){ if(TimerBase) CloseDevice((struct IORequest*)timer_io);
        DeleteIORequest((struct IORequest*)timer_io); timer_io=0; }
    if(timer_port){ DeleteMsgPort(timer_port); timer_port=0; }
    TimerBase=0; frame_ticks=next_tick=0;
}
static void open_timer(void){
    struct EClockVal ev; ULONG rate;
    timer_port=CreateMsgPort(); if(!timer_port) return;
    timer_io=(struct timerequest*)CreateIORequest(timer_port,sizeof(*timer_io));
    if(!timer_io){ close_timer(); return; }
    if(OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_ECLOCK,(struct IORequest*)timer_io,0)!=0){ close_timer(); return; }
    TimerBase=timer_io->tr_node.io_Device;
    rate=ReadEClock(&ev);
    frame_ticks=(rate+30)/60; if(frame_ticks<1) frame_ticks=1;
    next_tick=ev.ev_lo;
}
static void frame_pace(void){
    struct EClockVal ev; ULONG now;
    if(!TimerBase || !frame_ticks){ WaitTOF(); return; }
    ReadEClock(&ev); now=ev.ev_lo;
    if((LONG)(now-next_tick) > (LONG)frame_ticks){ next_tick=now; return; }
    next_tick+=frame_ticks;
    do{ ReadEClock(&ev); now=ev.ev_lo; }while((LONG)(now-next_tick) < 0);
}

static int sa_booted;
static void sa_intro_warmup(void *c){
    (void)c;
    if(!sa_booted){
        for(int i=0;i<600;i++) sa_run_frame(&z);
        sa_booted=1;
    }
}
static int sa_intro_ready(void *c){ (void)c; return sa_booted; }
static const char *const sa_intro_keys[] = {
    "ARROWS  MOVE", "SPACE / CTRL  SHOOT", "ALT  SPECIAL",
    "5 COIN   1 START", "F10 DIP SWITCHES", "ESC EXIT", 0
};
static const char *const sa_intro_pad[] = {
    "STICK  MOVE", "RED  SHOOT", "BLUE / YELLOW / GREEN  SPECIAL",
    "L / R COIN   PLAY START", "L + R + PLAY DIP SWITCHES", 0
};
static const ai_dip_opt sa_dip_diff[] = {
    {0x07,"0 EASIEST"}, {0x06,"1"}, {0x05,"2"}, {0x04,"3 NORMAL"},
    {0x03,"4"}, {0x02,"5"}, {0x01,"6"}, {0x00,"7 HARDEST"}
};
static const ai_dip_opt sa_dip_lives[] = { {0x08,"3"}, {0x00,"5"} };
static const ai_dip_opt sa_dip_bonus[] = {
    {0x30,"100000"}, {0x20,"100000 100000"},
    {0x10,"150000 150000"}, {0x00,"200000 200000"}
};
static const ai_dip_opt sa_dip_flip[] = { {0x40,"OFF"}, {0x00,"ON"} };
static const ai_dip_opt sa_dip_coin_a[] = {
    {0x00,"4C 1C"}, {0x01,"3C 1C"}, {0x02,"2C 1C"}, {0x07,"1C 1C"},
    {0x06,"1C 2C"}, {0x05,"1C 3C"}, {0x04,"1C 4C"}, {0x03,"1C 6C"}
};
static const ai_dip_opt sa_dip_coin_b[] = {
    {0x00,"4C 1C"}, {0x08,"3C 1C"}, {0x10,"2C 1C"}, {0x38,"1C 1C"},
    {0x30,"1C 2C"}, {0x28,"1C 3C"}, {0x20,"1C 4C"}, {0x18,"1C 6C"}
};
static const ai_dip_opt sa_dip_continue[] = { {0x00,"NO"}, {0x40,"YES"} };
static const ai_dip_opt sa_dip_demo[] = { {0x00,"OFF"}, {0x80,"ON"} };
static const ai_dip_item sa_dip_items[] = {
    {"DIFFICULTY",0,0x07,8,sa_dip_diff},
    {"LIVES",0,0x08,2,sa_dip_lives},
    {"BONUS LIFE",0,0x30,4,sa_dip_bonus},
    {"FLIP SCREEN",0,0x40,2,sa_dip_flip},
    {"COIN A",1,0x07,8,sa_dip_coin_a},
    {"COIN B",1,0x38,8,sa_dip_coin_b},
    {"ALLOW CONTINUE",1,0x40,2,sa_dip_continue},
    {"DEMO SOUNDS",1,0x80,2,sa_dip_demo}
};
static void sa_apply_dips(void *ctx){
    (void)ctx;
    csidearms_set_dsw(sa_dsw0, sa_dsw1, 0xff);
}
static const ai_dip_config sa_dip_cfg = {
    sa_dip_items, (int)(sizeof sa_dip_items / sizeof sa_dip_items[0]),
    &sa_dsw0, &sa_dsw1,
    sa_apply_dips, 0
};
static const ai_config sa_intro_cfg = {
    "SIDE ARMS",
    "WHITTY ARCADE PRESENTS SIDE ARMS    CAPCOM 1986 Z80 HARDWARE    MAIN Z80 INTERPRETED AT SIXTY HERTZ WITH BANKED ROMS PALETTE RAM TILEMAPS SPRITES AND TEXT LAYERS    SOUND BOARD IS Z80 PLUS TWO YM2203 CHIPS MIXED TO PAULA AT 8040 HZ    RTG OUTPUT USES AN 864 BY 486 BEZEL WITH PLAY WINDOW REFRESH AND FIXED RGB332 COLOUR    PRESS FIRE OR START WHEN READY    ",
    sa_intro_keys, sa_intro_pad,
    ai_default_mod, ai_default_mod_end,
    0, 150,
    sa_intro_ready, sa_intro_warmup, 0,
    &sa_dip_cfg
};

#define RK_1 0x01
#define RK_5 0x05
#define RK_SPACE 0x40
#define RK_ESC 0x45
#define RK_F10 0x59
#define RK_LCTRL 0x63
#define RK_LALT 0x64
#define RK_RALT 0x65
#define RK_UP 0x4C
#define RK_DOWN 0x4D
#define RK_RIGHT 0x4E
#define RK_LEFT 0x4F
#define CIAA_PRA (*(volatile unsigned char *)0xbfe001UL)
#define JOY1DAT  (*(volatile unsigned short *)0xdff00cUL)
#define PORT1_FIRE 0x80
#define CIAA_DDRA (*(volatile unsigned char  *)0xbfe201UL)
#define POTGO     (*(volatile unsigned short *)0xdff034UL)
#define POTINP    (*(volatile unsigned short *)0xdff016UL)
/* CD32 pad face buttons (clocked serially out of the fire line, port 1) */
#define CD32_BLUE 0x80
#define CD32_RED  0x40
#define CD32_YELLOW 0x20
#define CD32_GREEN  0x10
#define CD32_RSHOULDER 0x08
#define CD32_LSHOULDER 0x04
#define CD32_PLAY 0x02
#define CD32_DATRY 0x4000
static unsigned read_cd32(void){
    unsigned out=0; volatile unsigned char t;
    CIAA_DDRA |= PORT1_FIRE;
    CIAA_PRA  &= (unsigned char)~PORT1_FIRE;
    POTGO = 0x6f00;
    for(int i=7;i>=0;i--){
        t=CIAA_PRA;t=CIAA_PRA;t=CIAA_PRA;t=CIAA_PRA;t=CIAA_PRA;t=CIAA_PRA;(void)t;
        if(!(POTINP & CD32_DATRY)) out |= (1u<<i);
        CIAA_PRA |= PORT1_FIRE;
        CIAA_PRA &= (unsigned char)~PORT1_FIRE;
    }
    CIAA_DDRA &= (unsigned char)~PORT1_FIRE;
    POTGO = 0xff00;
    CIAA_PRA |= 0xc0;
    return out;
}

static uint8_t rgb332(unsigned r, unsigned g, unsigned b){
    return (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

static void upload_rgb332_palette(void){
    loadrgb[0] = (256u << 16) | 0u;
    for(int i=0;i<256;i++){
        unsigned r = ((unsigned)(i >> 5) * 255u) / 7u;
        unsigned g = ((unsigned)((i >> 2) & 7) * 255u) / 7u;
        unsigned b = ((unsigned)(i & 3) * 255u) / 3u;
        loadrgb[1+i*3+0] = (uint32_t)r * 0x01010101u;
        loadrgb[1+i*3+1] = (uint32_t)g * 0x01010101u;
        loadrgb[1+i*3+2] = (uint32_t)b * 0x01010101u;
    }
    loadrgb[1+256*3] = 0;
    LoadRGB32(&scr->ViewPort, loadrgb);
}

/* ---- live xBRG_444 palette RAM -> fixed RGB332 display pens ---- */
static void rebuild_pen8(const unsigned char *pram){
    uint32_t cs = 2166136261u;
    const uint32_t *pw = (const uint32_t*)pram;
    for(int i=0;i<0x800/4;i++) cs = (cs ^ pw[i]) * 16777619u;
    if(cs == pal_csum) return;
    for(int i=0;i<1024;i++){
        int lo=pram[i], hi=pram[0x400+i];
        unsigned r = (unsigned)((lo >> 4) & 0x0f) * 0x11u;
        unsigned g = (unsigned)(lo & 0x0f) * 0x11u;
        unsigned b = (unsigned)(hi & 0x0f) * 0x11u;
        pen8[i] = rgb332(r, g, b);
    }
    pal_csum = cs;
}

static void upload_loader_palette(void){
    loadrgb[0] = (16u << 16) | 0u;
    for(int i=0;i<16;i++){
        loadrgb[1+i*3+0] = (uint32_t)gm_palette[i][0] * 0x01010101u;
        loadrgb[1+i*3+1] = (uint32_t)gm_palette[i][1] * 0x01010101u;
        loadrgb[1+i*3+2] = (uint32_t)gm_palette[i][2] * 0x01010101u;
    }
    loadrgb[1+16*3] = 0;
    LoadRGB32(&scr->ViewPort, loadrgb);
}

/* Blit the static RGB332 bezel backdrop once (or a black screen when there is no bezel). */
static void blit_bezel(void){
    if(!win) return;
    if(bezel_active){
        size_t bn = (size_t)(sidearms_rtg_bezel_end - sidearms_rtg_bezel);
        if(rtg_frame && bn >= (size_t)SA_RTG_W*SA_RTG_H)
            memcpy(rtg_frame, sidearms_rtg_bezel, (size_t)SA_RTG_W*SA_RTG_H);
        if(rtg_frame)
            WriteChunkyPixels(win->RPort, 0,0, SA_RTG_W-1, SA_RTG_H-1, rtg_frame, SA_RTG_W);
    } else {
        SetRast(win->RPort, 0);
        if(rtg_frame) memset(rtg_frame, 0, (size_t)disp_w * disp_h);
    }
}

/* ---- present: map the 10-bit arcade palette indices to fixed RGB332, scale into
 * the large bezel game window, then blit ONLY that window. ---- */
static void present(void){
    const unsigned char *pram = sa_palette(&z);   /* lo[0x400]@c000, hi[0x400]@c400 */
    if(!win || !rtg_frame) return;
    rebuild_pen8(pram);
    for(int y=0;y<gh;y++){
        const uint16_t *src = frame16[(y * SA_NH) / gh];
        uint8_t *d = rtg_frame + (size_t)(gy + y) * disp_w + gx;
        for(int x=0;x<gw;x++){
            unsigned pen = src[(x * SA_NW) / gw] & 0x3ff;
            d[x] = pen8[pen];
        }
    }
    WriteChunkyPixels(win->RPort, gx, gy, gx+gw-1, gy+gh-1,
                      rtg_frame + (size_t)gy * disp_w + gx, disp_w);
}

/* ----- loader: gm_* draws 8-bit into l8 and blits full-screen ----- */
static void draw_loader_screen(uint8_t *l8, int w, int h, int tick, int ready, int blink){
    memset(l8, 0, (size_t)w*h);
    for(int i=0;i<90;i++){
        int d=1+(i&3), x=(i*97+tick*d*3)%w, y=10+((i*53+tick)%(h>120?h-96:h));
        gm_put_px(l8,w,h,x,y,(d>=3)?15:12);
    }
    gm_draw_text_center(l8,w,h,"SIDE ARMS",26,4,15);
    gm_draw_text_center(l8,w,h,"CAPCOM Z80 BOARD",66,1,14);
    gm_draw_text_center(l8,w,h,"Z80  +  2x YM2203  +  PAULA",84,1,11);
    gm_fill_rect(l8,w,h,0,h-98,w,98,1);
    gm_draw_text_center(l8,w,h,ready?"READY":"STARTING UP",h-88,2,10);
    if(ready && blink) gm_draw_text_center(l8,w,h,"FIRE OR START TO PLAY",h-64,1,15);
    gm_draw_text_center(l8,w,h,"5 COIN    1 START    ESC EXIT",h-30,1,11);
    gm_draw_text_center(l8,w,h,"ARROWS MOVE   RED JUMP   BLUE ATTACK",h-16,1,14);
}

static void run_loader(void){
    int tick=0, sf_prev=1, w=(int)disp_w, h=(int)disp_h;
    uint8_t *l8  = (uint8_t*)AllocMem((size_t)w*h, MEMF_FAST|MEMF_CLEAR);
    if(!l8){            /* low RAM: skip the loader, just warm up behind black */
        if(l8) FreeMem(l8,(size_t)w*h);
        sa_intro_warmup(0);
        return;
    }
    upload_loader_palette();
    for(;;){
        struct IntuiMessage *m;
        while(win && win->UserPort && (m=(struct IntuiMessage*)GetMsg(win->UserPort))){
            ULONG cls=m->Class; UWORD raw=m->Code; ReplyMsg((struct Message*)m);
            if(cls==IDCMP_RAWKEY) keydown[raw&0x7f]=(raw&0x80)?0:1;
        }
        if(keydown[RK_ESC]){ hal_quit=1; break; }
        if(!sa_booted) sa_intro_warmup(0);
        unsigned cd32 = read_cd32();
        int ready = sa_booted && tick >= 150;
        int fire = !(CIAA_PRA & PORT1_FIRE) || (cd32&(CD32_RED|CD32_BLUE|CD32_YELLOW|CD32_GREEN))
            || keydown[RK_SPACE] || keydown[RK_LCTRL] || keydown[RK_LALT] || keydown[RK_RALT];
        int start = keydown[RK_1] || (cd32&CD32_PLAY);
        int sf = fire || start;
        if(ready && sf && !sf_prev) break;
        sf_prev = sf;
        draw_loader_screen(l8, w, h, tick, ready, ((tick/24)&1)==0);
        WriteChunkyPixels(win->RPort, 0,0, w-1,h-1, l8, w);
        frame_pace();
        tick++;
    }
    memset(keydown,0,sizeof keydown);
    FreeMem(l8,(size_t)w*h);
}

/* ----- input ----- (P1 active-low: 0x01 R,0x02 L,0x04 D,0x08 U,0x10 B1,0x20 B2;
 *  SYSTEM: 0x01 START1, 0x40 COIN1) */
static void poll_input(void){
    struct IntuiMessage *m;
    if(win && win->UserPort)
        while((m=(struct IntuiMessage*)GetMsg(win->UserPort))){
            ULONG cls=m->Class; UWORD raw=m->Code; ReplyMsg((struct Message*)m);
            if(cls==IDCMP_RAWKEY) keydown[raw&0x7f]=(raw&0x80)?0:1;
        }
    if(keydown[RK_ESC]) hal_quit=1;

    static int potinit=0;
    if(!potinit){ POTGO=0xff00; potinit=1; }   /* pot pins -> button inputs (fire2/3) */
    unsigned cd32 = read_cd32();
    {
        static int kf10, pdip;
        int dip_now = ai_cd32_dip_combo(cd32);
        if((keydown[RK_F10] && !kf10) || (dip_now && !pdip)){
            sa_audio_amiga_close();
            ai_dip_open(&sa_dip_cfg);
            upload_rgb332_palette();
            blit_bezel();
            pal_csum = 0xffffffff;
            sa_audio_amiga_open();
            keydown[RK_F10] = 0;
        }
        kf10 = keydown[RK_F10];
        pdip = dip_now;
    }
    { volatile int d; for(d=0; d<8; d++) (void)POTINP; }
    unsigned v=JOY1DAT;
    int right=(v>>1)&1,left=(v>>9)&1,down=((v>>1)^v)&1,up=((v>>9)^(v>>8))&1;
    if(keydown[RK_RIGHT])right=1; if(keydown[RK_LEFT])left=1;
    if(keydown[RK_DOWN])down=1;   if(keydown[RK_UP])up=1;
    int fire1 = !(CIAA_PRA & PORT1_FIRE);          /* joystick button 1 (red)  */
    int fire2 = !(POTINP & CD32_DATRY);            /* joystick button 2 (blue) */
    int b1 = fire1 || (cd32 & CD32_RED) || keydown[RK_SPACE] || keydown[RK_LCTRL];                  /* JUMP   */
    int b2 = fire2 || (cd32 & CD32_BLUE) || keydown[RK_LALT];      /* weapon 2 */
    int b3 = (cd32 & (CD32_YELLOW|CD32_GREEN)) || keydown[RK_RALT]; /* weapon 3 */

    uint8_t sys=0xff, p1=0xff, p2=0xff;
    static int ck,sk,ch,sh;
    int coin=keydown[RK_5] || (cd32 & (CD32_LSHOULDER|CD32_RSHOULDER));   /* pad shoulder = COIN  */
    int start=keydown[RK_1] || (cd32 & CD32_PLAY);                        /* pad PLAY     = START */
    if(coin&&!ck)ch=8; if(start&&!sk)sh=8; ck=coin; sk=start;
    if(ch){ sys&=~0x40; ch--; } if(sh){ sys&=~0x01; sh--; }
    if(right)p1&=~0x01; if(left)p1&=~0x02; if(down)p1&=~0x04; if(up)p1&=~0x08;
    if(b1)p1&=~0x10; if(b2)p1&=~0x20; if(b3)p1&=~0x40;
    sa_set_inputs(sys, p1, p2, sa_dsw0, sa_dsw1);
}

static void shutdown(void){
    static int done; if(done) return; done=1;
    sa_audio_amiga_close();          /* stop Paula AUD0/1 DMA + free chip buffer FIRST */
    frame_pace();                    /* let the audio DMA settle before teardown */
    close_timer();
    if(win){ CloseWindow(win); win=0; }
    if(rtg_frame){ FreeMem(rtg_frame, (size_t)disp_w*disp_h); rtg_frame=0; }
    if(scr){ CloseScreen(scr); scr=0; }
    if(CyberGfxBase){ CloseLibrary((struct Library*)CyberGfxBase); CyberGfxBase=0; }
    if(GfxBase){ CloseLibrary((struct Library*)GfxBase); GfxBase=0; }
    if(IntuitionBase){ CloseLibrary((struct Library*)IntuitionBase); IntuitionBase=0; }
    sidearms_audio_shutdown();       /* free the YM2203 OPN core state (no leak under AGS) */
    ok=0;
}
void hal_cleanup(void){ shutdown(); }   /* called by amiga.s at every exit path */

/* Open an 8-bit RTG screen W x H; returns 1 on success. */
static int try_open(int W, int H){
    ULONG modeid=BestModeID(BIDTAG_NominalWidth,(ULONG)W, BIDTAG_NominalHeight,(ULONG)H,
                            BIDTAG_DesiredWidth,(ULONG)W, BIDTAG_DesiredHeight,(ULONG)H,
                            BIDTAG_Depth,8, TAG_DONE);
    if(modeid!=INVALID_ID){
        scr=OpenScreenTags(0, SA_DisplayID, modeid, SA_Width,(ULONG)W, SA_Height,(ULONG)H,
                           SA_Depth,8, SA_Type,CUSTOMSCREEN,
                           SA_Quiet,TRUE, SA_ShowTitle,FALSE, TAG_END);
        if(scr) return 1;
    }
    scr=OpenScreenTags(0, SA_Width,(ULONG)W, SA_Height,(ULONG)H,
                       SA_Depth,8, SA_Type,CUSTOMSCREEN,
                       SA_Quiet,TRUE, SA_ShowTitle,FALSE, TAG_END);
    return scr != 0;
}

void hal_game_init(void){
    IntuitionBase=(struct IntuitionBase*)OpenLibrary((CONST_STRPTR)"intuition.library",39);
    GfxBase=(struct GfxBase*)OpenLibrary((CONST_STRPTR)"graphics.library",39);
    if(!IntuitionBase||!GfxBase){ hal_quit=1; return; }
    open_timer();

    sa_load_maincpu(sidearms_maincpu);
    sa_set_gfx(sidearms_chars, sidearms_tiles, sidearms_sprites, sidearms_tilemap);
    sa_render_init();              /* decode-cache gfx ROMs ONCE (the 1943 fix: no per-pixel bitplane fetch) */
    sa_init(&z);
    sa_set_inputs(0xff, 0xff, 0xff, sa_dsw0, sa_dsw1);

    /* Prefer the 864x486 bezel screen; fall back to plain truecolor sizes (no bezel). */
    if(try_open(SA_RTG_W, SA_RTG_H) && scr->Width==SA_RTG_W && scr->Height==SA_RTG_H){
        bezel_active=1;
    } else {
        if(scr){ CloseScreen(scr); scr=0; }
        if(!try_open(640,480) && !try_open(800,600) && !try_open(640,512)){ hal_quit=1; return; }
        bezel_active=0;
    }
    disp_w=(unsigned)scr->Width;
    disp_h=(unsigned)scr->Height;

    if(bezel_active){
        gx=SA_GAME_X; gy=SA_GAME_Y; gw=SA_GAME_W; gh=SA_GAME_H;
    } else {                          /* native 384x224 centred with small margins */
        int fw=(int)disp_w-32, fh=(int)disp_h-32;
        if(fw<1) fw=(int)disp_w; if(fh<1) fh=(int)disp_h;
        if((long)fw*SA_NH <= (long)fh*SA_NW){ gw=fw; gh=(SA_NH*gw)/SA_NW; }
        else                                { gh=fh; gw=(SA_NW*gh)/SA_NH; }
        if(gw<1)gw=1; if(gh<1)gh=1;
        gx=((int)disp_w-gw)/2; gy=((int)disp_h-gh)/2;
    }

    rtg_frame=(uint8_t*)AllocMem((size_t)disp_w*disp_h, MEMF_FAST|MEMF_CLEAR);
    if(!rtg_frame) rtg_frame=(uint8_t*)AllocMem((size_t)disp_w*disp_h, MEMF_ANY|MEMF_CLEAR);
    if(!rtg_frame){ shutdown(); hal_quit=1; return; }

    win=OpenWindowTags(0, WA_CustomScreen,(ULONG)scr, WA_Left,0, WA_Top,0,
                       WA_Width,(ULONG)disp_w, WA_Height,(ULONG)disp_h, WA_Backdrop,TRUE, WA_Borderless,TRUE,
                       WA_Activate,TRUE, WA_RMBTrap,TRUE, WA_IDCMP,IDCMP_RAWKEY, TAG_END);
    if(win){ ScreenToFront(scr); ActivateWindow(win); }
    if(!win){ shutdown(); hal_quit=1; return; }
    SetRast(win->RPort, 0);
    ai_init(scr, win, rtg_frame, (int)disp_w, (int)disp_h);
    ai_set_loader_enabled(1);
    if(!ai_run(&sa_intro_cfg)) hal_quit=1;
    if(hal_quit){ shutdown(); return; }
    upload_rgb332_palette();
    blit_bezel();                    /* paint the bezel backdrop; gameplay refreshes only the hole */
    /* sound: init the audio Z80 + 2x YM2203 and open Paula. AFTER the boot loop so the
     * soundlatch FIFO starts clean at the attract, and OS-safe (past the warmup). */
    sidearms_audio_init(sidearms_snd);
    sa_audio_amiga_open();
    ok=1;
}

void hal_game_frame(void){
    poll_input();
    if(hal_quit){ shutdown(); return; }
    sa_run_frame(&z);
    if(!ok) return;
    sa_audio_amiga_frame();      /* refill Paula before RTG render work eats into lead */
    sa_render(&z, frame16);
    present();
    frame_pace();
}
