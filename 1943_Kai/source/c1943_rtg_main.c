/* c1943_rtg_main.c -- 1943 RTG/chunky presenter (native transcode build).
 * Screen-open mirrors the proven Gaplus/Sky Kid RTG setup: BestModeID() to find an
 * 8-bit ~864x486 RTG mode, fall back to a fixed id, then to no-DisplayID. Runs one
 * 1943 frame (main Z80), then presents the native 256x224 arcade frame.
 *
 * 1943 is a VERTICAL game (ROT270): the renderer outputs the native (un-rotated)
 * 256x224 bitmap and we rotate it 270 deg CW to an upright 224x256 portrait image,
 * NN-scaled to fill the RTG height, centred (pillarboxed) -- the Gaplus pattern.
 *   native(nx,ny) drawn at rotated(ox=ny, oy=255-nx); inverse: nx=255-ry, ny=rx.
 *
 * Keyboard: 5=coin, 1=start, arrows=move, space/ctrl=shot, alt=special, P=pause, Esc=quit.
 */
#include <exec/exec.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <stdint.h>
#include <string.h>
#include "c1943_menu.h"
#include "c1943_render.h"
#include "arcade_intro.h"

/* (diagnostics behind -DC1943_DIAG: screen-open red flash + per-frame alive bar) */

extern void g1943_init(void);
extern void g1943_late_init(void);           /* AllocMem + dos.library init, AFTER the intro */
extern void g1943_frame(void);
extern void g1943_run_frame(void);
extern void g1943_render_frame(void);
extern void g1943_set_inputs(uint8_t sys, uint8_t p1, uint8_t p2, uint8_t dswa, uint8_t dswb);
extern void g1943_dev_warp_boss(void);
extern const unsigned char *g1943_chunky(void);
extern const unsigned char *g1943_pal256(int *n);
extern void g1943_dims(int *w, int *h);
extern void c1943_audio_amiga_open(void);    /* sound Z80 + 2x YM2203 -> Paula */
extern void c1943_audio_amiga_frame(void);
extern void c1943_audio_amiga_close(void);   /* stop Paula + free chip buffer on exit */
extern void c1943_audio_shutdown(void);       /* free YM2203/FM core state */
extern void g1943_scores_save(void);         /* flush persisted high scores */
extern void g1943_scores_tick(void);         /* main-loop only (not in the intro warmup) */

/* ---- ArcadeIntro loader (twister + sine-scroller + ProTracker music): decodes
 * the ROMs + warms 1943 up behind the loader, gates FIRE on the decode. ---- */
extern const unsigned char ai_default_mod[], ai_default_mod_end[];
extern const unsigned char c1943_rtg_bezel[], c1943_rtg_bezel_end[];
static int intro_decoded;
static void c1943_intro_warmup(void *c){ (void)c; if(!intro_decoded){ g1943_init(); intro_decoded=1; } else g1943_run_frame(); }
static int  c1943_intro_ready(void *c){ (void)c; return intro_decoded; }
static const char *const c1943_intro_keys[] = {
    "ARROWS  MOVE", "SPACE / CTRL  SHOOT", "ALT  SPECIAL",
    "5 COIN   1 START", "F10 DIP   P PAUSE   ESC EXIT", 0
};
static const char *const c1943_intro_pad[] = {
    "STICK  MOVE", "RED  SHOOT", "BLUE / YELLOW / GREEN  SPECIAL",
    "L / R COIN   PLAY START", 0
};
static const ai_config c1943_intro_cfg = {
#ifdef C1943_KAI
    "1943 KAI",
    "WHITTY ARCADE PRESENTS 1943 KAI    CAPCOM 1988 Z80 HARDWARE    "
#else
    "1943",
    "WHITTY ARCADE PRESENTS 1943 THE BATTLE OF MIDWAY    CAPCOM 1987 Z80 HARDWARE    "
#endif
    "MAIN Z80 INTERPRETER MATCHING THE GUN SMOKE BUILD WITH BANKED PROGRAM ROMS TWO SCROLLING BACKGROUND LAYERS SPRITES TEXT AND PROM DERIVED COLOUR TABLES    "
    "SOUND BOARD IS NATIVE Z80 TRANSCODE PLUS TWO YM2203 CHIPS MIXED TO PAULA AT 8040 HZ    "
    "RTG OUTPUT USES AN 864 BY 486 BEZEL WITH PLAY WINDOW REFRESH LOCAL HIGH SCORE SAVES AND F10 DIP SWITCHES    "
    "PRESS FIRE OR START WHEN READY    ",
    c1943_intro_keys, c1943_intro_pad,
    ai_default_mod, ai_default_mod_end,
    0, 220,
    c1943_intro_ready, c1943_intro_warmup, 0
};

struct IntuitionBase *IntuitionBase = 0;
struct GfxBase *GfxBase = 0;
struct Device *TimerBase = 0;

/* 864x486 custom 8-bit RTG screen. We tried Green Beret's tiny 320x256 screen
 * (native blit + WinUAE up-scale) but in this WinUAE config that small RTG mode is
 * a 35Hz mode -- WaitTOF() then caps the game at 35fps regardless of CPU headroom
 * (confirmed: 35fps even while paused). 864x486 is a 50Hz mode here (the HDF ran at
 * 50). So: keep 864x486, scale the rotated game ASPECT-CORRECT into a centred
 * play-rect (fills the height, correct 7:8 shape -- not the old too-wide stretch),
 * and fill the side margins with the bezel, drawn once. */
#define RTG_W 864
#define RTG_H 486
#define RTG_MODE_ID 0x50FF1000UL
static struct Screen *scr;
static struct Window *win;
static struct MsgPort *timer_port;
static struct timerequest *timer_io;
static int pubscreen_locked;
static uint8_t *rtg_frame;                  /* the 864x486 composite (bezel + game) */
static uint8_t *disp_frame;                 /* legacy public-screen buffer; unused in fixed RTG mode */
static unsigned disp_w, disp_h;
#define DISP_MAXW 2048
#define DISP_MAXH 1536
static int disp_colmap[DISP_MAXW], disp_rowmap[DISP_MAXH];
static uint32_t loadrgb[1 + 256*3 + 1];
static uint8_t game_pen[256];
static int SW_, SH_, rtg_ok, rtg_w, rtg_h;
static int gx0, gy0, playW, playH;           /* centred, aspect-correct game play-rect */
static unsigned render_gate;
#define C1943_RENDER_DIV 1                   /* full-rate presentation */
static unsigned char keydown[128];
static int g_pause, decoded;
static ULONG eclock_rate, frame_ticks, next_tick;
/* DSWA default: difficulty nibble 0x0f -> 0x0 = "16 (Hardest)"; other DSWA/DSWB
 * bits at MAME defaults. Adjustable live via the F10 DIP menu. */
static uint8_t g_dswa = 0xf0, g_dswb = 0xff;
static int g_menu = 0, g_menu_sel = 0, g_menu_tick = 0, g_need_bezel = 0, g_audio_paused = 0;

static void *c1943_fast_cache_alloc(uint32_t bytes){
    void *p = AllocMem(bytes, MEMF_FAST | MEMF_CLEAR);
    if(!p) p = AllocMem(bytes, MEMF_PUBLIC | MEMF_CLEAR);
    return p;
}

static void c1943_fast_cache_free(void *ptr, uint32_t bytes){
    if(ptr) FreeMem(ptr, bytes);
}

static uint8_t rgb332(unsigned r, unsigned g, unsigned b);

#define JOY1DAT  (*(volatile unsigned short *)0xdff00cUL)
#define CIAA_PRA (*(volatile unsigned char  *)0xbfe001UL)
#define CIAA_DDRA (*(volatile unsigned char *)0xbfe201UL)
#define POTGO    (*(volatile unsigned short *)0xdff034UL)
#define POTINP   (*(volatile unsigned short *)0xdff016UL)
#define PORT1_FIRE   0x80
#define PORT1_DATRY  0x4000
#define POTGO_PORT1  0x6f00
#define POTGO_RESET  0xff00
#define JOY0DAT  (*(volatile unsigned short *)0xdff00aUL)   /* port-2 (DB9 port 0) joystick */
#define PORT0_FIRE  0x40    /* CIAA PRA bit6 = port-0 fire 1 */
#define PORT0_DATLY 0x0100  /* POTINP bit8  = port-0 fire 2 */
#define CD32_RSHOULDER 0x08
#define CD32_LSHOULDER 0x04
#define CD32_GREEN 0x10
#define CD32_YELLOW 0x20
#define CD32_RED 0x40
#define CD32_BLUE 0x80
#define CD32_PLAY 0x02

/* CD32 pad on joystick port 1 read as a serial shift register (asman/wepl/JOTD). */
static unsigned read_cd32_port1(void){
    unsigned out=0; int i; volatile unsigned char t;
    CIAA_DDRA |= PORT1_FIRE;
    CIAA_PRA  &= (unsigned char)~PORT1_FIRE;
    POTGO = POTGO_PORT1;
    for(i=7;i>=0;i--){
        t=CIAA_PRA;t=CIAA_PRA;t=CIAA_PRA;t=CIAA_PRA;t=CIAA_PRA;t=CIAA_PRA;(void)t;
        if(!(POTINP & PORT1_DATRY)) out |= (1u<<i);
        CIAA_PRA |= PORT1_FIRE;
        CIAA_PRA &= (unsigned char)~PORT1_FIRE;
    }
    CIAA_DDRA &= (unsigned char)~PORT1_FIRE;
    POTGO = POTGO_RESET;
    CIAA_PRA |= 0xC0;
    return out;
}

unsigned char hal_quit = 0;

static void close_timer(void){
    if(timer_io){
        if(TimerBase) CloseDevice((struct IORequest*)timer_io);
        DeleteIORequest((struct IORequest*)timer_io);
        timer_io=0;
    }
    if(timer_port){ DeleteMsgPort(timer_port); timer_port=0; }
    TimerBase=0; eclock_rate=frame_ticks=next_tick=0;
}

static void open_timer(void){
    struct EClockVal ev;
    timer_port=CreateMsgPort();
    if(!timer_port) return;
    timer_io=(struct timerequest*)CreateIORequest(timer_port, sizeof(*timer_io));
    if(!timer_io){ close_timer(); return; }
    if(OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest*)timer_io, 0)!=0){
        close_timer(); return;
    }
    TimerBase=timer_io->tr_node.io_Device;
    eclock_rate=ReadEClock(&ev);
    frame_ticks=(eclock_rate + 30) / 60;
    if(frame_ticks<1) frame_ticks=1;
    next_tick=ev.ev_lo;
}

static void frame_pace(void){
    struct EClockVal ev;
    ULONG now;
    if(!TimerBase || !frame_ticks) return;
    ReadEClock(&ev);
    now=ev.ev_lo;
    if((LONG)(now-next_tick) > (LONG)frame_ticks){
        next_tick=now;
        return;
    }
    next_tick += frame_ticks;
    do{
        ReadEClock(&ev);
        now=ev.ev_lo;
    }while((LONG)(now-next_tick)<0);
}

static void shutdown_rtg(void){
    static int done; if(done) return; done=1;
    c1943_audio_amiga_close();    /* stop Paula DMA before we tear down / return to AGS */
    c1943_audio_shutdown();
    c1943_render_shutdown();
    close_timer();
    if(win){ CloseWindow(win); win=0; }
    if(disp_frame){ FreeMem(disp_frame,(size_t)disp_w*disp_h); disp_frame=0; }
    if(rtg_frame){ FreeMem(rtg_frame,(size_t)rtg_w*rtg_h); rtg_frame=0; }
    if(scr && pubscreen_locked){ UnlockPubScreen(0, scr); scr=0; pubscreen_locked=0; }
    else if(scr){ CloseScreen(scr); scr=0; }
    if(GfxBase){ CloseLibrary((struct Library*)GfxBase); GfxBase=0; }
    if(IntuitionBase){ CloseLibrary((struct Library*)IntuitionBase); IntuitionBase=0; }
    rtg_ok=0;
    (void)pubscreen_locked;
}

void hal_cleanup(void){ shutdown_rtg(); }

/* Rotate the native SW_xSH_ (256x224) frame 270deg CW into an upright portrait
 * image and NN-scale it ASPECT-CORRECT into the centred play-rect (playW x playH at
 * gx0,gy0) of rtg_frame. Orientation map (verified): play(dx,dy) = native(
 * nx = SW_-1 - dy*SW_/playH, ny = dx*SH_/playW). Only the play-rect is written; the
 * bezel margins persist. LUTs (geometry fixed) remove a divide+mul per pixel. */
static void scale_to_play(void){
    const unsigned char *src = g1943_chunky();     /* native fb[ny*SW_ + nx] */
    const int NW=SW_;                              /* 256 */
    static int luts_ready=0;
    static int col_ofs[1024], row_nx[1024];        /* playW<=864, playH<=486 */
    if(!luts_ready){
        for(int dx=0; dx<playW; dx++) col_ofs[dx] = ((dx*SH_)/playW) * NW;   /* ny*NW */
        for(int dy=0; dy<playH; dy++) row_nx[dy] = (NW-1) - (dy*NW)/playH;   /* nx   */
        luts_ready=1;
    }
    int last_nx = -1;
    for(int dy=0; dy<playH; dy++){
        uint8_t *dst = rtg_frame + (size_t)(gy0+dy)*rtg_w + gx0;
        if(row_nx[dy] == last_nx){
            memcpy(dst, dst - rtg_w, playW);
            continue;
        }
        last_nx = row_nx[dy];
        const unsigned char *row = src + last_nx;
        for(int dx=0; dx<playW; dx++) dst[dx] = game_pen[row[col_ofs[dx]]];
    }
}

static void present_win_full(void){
    if(!win || !rtg_frame) return;
    WriteChunkyPixels(win->RPort, 0, 0, rtg_w-1, rtg_h-1, rtg_frame, rtg_w);
}

static void present_play_rect(void){
    if(!win || !rtg_frame) return;
    WriteChunkyPixels(win->RPort, gx0, gy0, gx0+playW-1, gy0+playH-1,
                      rtg_frame + (size_t)gy0*rtg_w + gx0, rtg_w);
}

static uint8_t rgb332(unsigned r, unsigned g, unsigned b){
    return (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

/* Draw the Bezel Project RGB332 backdrop once. The per-frame game blit only touches
 * the centred play rect, so this is not part of the scrolling cost. */
static void draw_bezel(void){
    if(!rtg_frame) return;
    if((size_t)(c1943_rtg_bezel_end - c1943_rtg_bezel) >= (size_t)RTG_W * RTG_H)
        memcpy(rtg_frame, c1943_rtg_bezel, (size_t)RTG_W * RTG_H);
    else
        memset(rtg_frame, 0, (size_t)rtg_w * rtg_h);
}

static void upload_palette(void){
    int n; const unsigned char *pal=g1943_pal256(&n);
    (void)n;
    loadrgb[0]=(256u<<16)|0;
    for(int i=0;i<256;i++){
        unsigned r=((unsigned)(i>>5)*255u)/7u;
        unsigned g=((unsigned)((i>>2)&7)*255u)/7u;
        unsigned b=((unsigned)(i&3)*255u)/3u;
        loadrgb[1+i*3+0]=(uint32_t)r*0x01010101u;
        loadrgb[1+i*3+1]=(uint32_t)g*0x01010101u;
        loadrgb[1+i*3+2]=(uint32_t)b*0x01010101u;
    }
    for(int i=0;i<n && i<256;i++){
        game_pen[i]=rgb332(pal[i*3+0], pal[i*3+1], pal[i*3+2]);
    }
    loadrgb[1+256*3]=0;
    LoadRGB32(&scr->ViewPort, loadrgb);
}

/* load the 16-colour DIP-menu palette into indices 0..15 (game palette is
 * re-asserted per frame by upload_palette() once the menu closes) */
static void menu_load_palette(void){
    uint32_t lr[1 + 16*3 + 1];
    lr[0]=((uint32_t)16<<16)|0;
    for(int i=0;i<16;i++){
        lr[1+i*3+0]=(uint32_t)c1943_menu_pal[i][0]*0x01010101u;
        lr[1+i*3+1]=(uint32_t)c1943_menu_pal[i][1]*0x01010101u;
        lr[1+i*3+2]=(uint32_t)c1943_menu_pal[i][2]*0x01010101u;
    }
    lr[1+16*3]=0;
    LoadRGB32(&scr->ViewPort, lr);
}

static void menu_open(void){
    g_menu = 1;
    g_menu_sel = 0;
    if(!g_audio_paused){
        c1943_audio_amiga_close();
        g_audio_paused = 1;
    }
    menu_load_palette();
}

static void menu_close(void){
    g_menu = 0;
    g_need_bezel = 1;
    if(g_audio_paused){
        c1943_audio_amiga_open();
        g_audio_paused = 0;
    }
}

/* raw Amiga key codes */
#define RK_1 0x01
#define RK_5 0x05
#define RK_P 0x19
#define RK_SPACE 0x40
#define RK_ESC 0x45
#define RK_LCTRL 0x63
#define RK_LALT 0x64
#define RK_RALT 0x65
#define RK_UP 0x4C
#define RK_DOWN 0x4D
#define RK_RIGHT 0x4E
#define RK_LEFT 0x4F
#define RK_F7 0x56
#define RK_F10 0x59
/* player 2: WASD + Right-Shift (shoot) + Return (special), coin 6 / start 2 */
#define RK_2 0x02
#define RK_6 0x06
#define RK_RETURN 0x44
#define RK_W 0x11
#define RK_A 0x20
#define RK_S 0x21
#define RK_D 0x22
#define RK_RSHIFT 0x61

static void poll_input(void){
    static int k_p_prev, coin_kp, start_kp, coin_hold, start_hold;
    static int coin2_kp, start2_kp, coin2_hold, start2_hold;
    static int kf7, kf10, kup, kdn, klf, krt, kesc, kst;
    static unsigned cd32_cached;
    static int cd32_tick;
    struct IntuiMessage *m;
    if(win && win->UserPort)
        while((m=(struct IntuiMessage*)GetMsg(win->UserPort))){
            ULONG cls=m->Class; UWORD raw=m->Code; ReplyMsg((struct Message*)m);
            if(cls==IDCMP_RAWKEY) keydown[raw&0x7f]=(raw&0x80)?0:1;
        }

    /* F10 toggles the DIP-switch overlay (keyboard only -- no hardware-pad banging,
     * which hangs the live OS under the AGS desktop). */
    if(keydown[RK_F10] && !kf10){
        if(g_menu) menu_close(); else menu_open();
    }
    kf10=keydown[RK_F10];
    if(g_menu){
        if(keydown[RK_UP]    && !kup) g_menu_sel=(g_menu_sel+C1943_MENU_ITEMS-1)%C1943_MENU_ITEMS;
        if(keydown[RK_DOWN]  && !kdn) g_menu_sel=(g_menu_sel+1)%C1943_MENU_ITEMS;
        if(keydown[RK_RIGHT] && !krt) c1943_menu_change(g_menu_sel,+1,&g_dswa,&g_dswb);
        if(keydown[RK_LEFT]  && !klf) c1943_menu_change(g_menu_sel,-1,&g_dswa,&g_dswb);
        if(keydown[RK_1] && !kst) menu_close();   /* Start closes; F10 toggles; Esc is quit-only */
        kup=keydown[RK_UP]; kdn=keydown[RK_DOWN]; klf=keydown[RK_LEFT];
        krt=keydown[RK_RIGHT]; kesc=keydown[RK_ESC]; kst=keydown[RK_1];
        return;                                /* swallow game input while the menu is up */
    }
    kup=keydown[RK_UP]; kdn=keydown[RK_DOWN]; klf=keydown[RK_LEFT];
    krt=keydown[RK_RIGHT]; kesc=keydown[RK_ESC]; kst=keydown[RK_1];

    if(keydown[RK_P] && !k_p_prev) g_pause=!g_pause;
    k_p_prev=keydown[RK_P];
    if(keydown[RK_ESC]) hal_quit=1;
    if(keydown[RK_F7] && !kf7) g1943_dev_warp_boss();
    kf7=keydown[RK_F7];

    unsigned cd32 = cd32_cached;
    if((cd32_tick++ & 3) == 0)
        cd32 = cd32_cached = read_cd32_port1();

    /* P1: joystick/CD32 port 1 + keyboard. */
    unsigned v=JOY1DAT;
    int right=(v>>1)&1, left=(v>>9)&1, down=((v>>1)^v)&1, up=((v>>9)^(v>>8))&1;
    if(keydown[RK_RIGHT])right=1; if(keydown[RK_LEFT])left=1;
    if(keydown[RK_DOWN])down=1;   if(keydown[RK_UP])up=1;

    int shot    = !(CIAA_PRA & PORT1_FIRE) || keydown[RK_SPACE] || keydown[RK_LCTRL];
    int special = (cd32 & (CD32_BLUE|CD32_YELLOW|CD32_GREEN)) || keydown[RK_LALT] || keydown[RK_RALT];

    int coin_key  = keydown[RK_5] || (cd32 & (CD32_LSHOULDER|CD32_RSHOULDER));
    int start_key = keydown[RK_1] || (cd32 & CD32_PLAY);
    if(coin_key  && !coin_kp)  coin_hold  = 8;
    if(start_key && !start_kp) start_hold = 8;
    coin_kp=coin_key; start_kp=start_key;

    /* ---- player 2: read-only joystick port 2 (DB9 port 0) + WASD / R-Shift / Return ---- */
    unsigned v2=JOY0DAT;
    int right2=(v2>>1)&1, left2=(v2>>9)&1, down2=((v2>>1)^v2)&1, up2=((v2>>9)^(v2>>8))&1;
    if(keydown[RK_D])right2=1; if(keydown[RK_A])left2=1;
    if(keydown[RK_S])down2=1;  if(keydown[RK_W])up2=1;
    int shot2    = !(CIAA_PRA & PORT0_FIRE) || keydown[RK_RSHIFT];
    int special2 = keydown[RK_RETURN];
    int coin2_key  = keydown[RK_6];
    int start2_key = keydown[RK_2];
    if(coin2_key  && !coin2_kp)  coin2_hold  = 8;
    if(start2_key && !start2_kp) start2_hold = 8;
    coin2_kp=coin2_key; start2_kp=start2_key;

    /* active-low 1943 input bytes */
    uint8_t sys=0xff, p1=0xff, p2=0xff;
    if(coin_hold)   { sys &= ~0x40; coin_hold--;   }  /* COIN1  (SYSTEM bit6) */
    if(start_hold)  { sys &= ~0x01; start_hold--;  }  /* START1 (SYSTEM bit0) */
    if(coin2_hold)  { sys &= ~0x80; coin2_hold--;  }  /* COIN2  (SYSTEM bit7) */
    if(start2_hold) { sys &= ~0x02; start2_hold--; }  /* START2 (SYSTEM bit1) */
    if(right) p1 &= ~0x01;
    if(left)  p1 &= ~0x02;
    if(down)  p1 &= ~0x04;
    if(up)    p1 &= ~0x08;
    if(shot)    p1 &= ~0x10;   /* P1 BUTTON1 */
    if(special) p1 &= ~0x20;   /* P1 BUTTON2 */
    if(right2) p2 &= ~0x01;
    if(left2)  p2 &= ~0x02;
    if(down2)  p2 &= ~0x04;
    if(up2)    p2 &= ~0x08;
    if(shot2)    p2 &= ~0x10;  /* P2 BUTTON1 */
    if(special2) p2 &= ~0x20;  /* P2 BUTTON2 */
    g1943_set_inputs(sys, p1, p2, g_dswa, g_dswb); /* live DIP settings */
}

void hal_game_init(void){
    g1943_dims(&SW_,&SH_);
    c1943_render_set_allocators(c1943_fast_cache_alloc, c1943_fast_cache_free);
    g1943_init();                 /* decode ROMs + machine + render init (no intro loader) */
    IntuitionBase=(struct IntuitionBase*)OpenLibrary((CONST_STRPTR)"intuition.library",39);
    GfxBase=(struct GfxBase*)OpenLibrary((CONST_STRPTR)"graphics.library",40);
    if(!IntuitionBase||!GfxBase) return;
    open_timer();

    {
        ULONG mode = BestModeID(BIDTAG_NominalWidth, RTG_W,
                                BIDTAG_NominalHeight, RTG_H,
                                BIDTAG_DesiredWidth, RTG_W,
                                BIDTAG_DesiredHeight, RTG_H,
                                BIDTAG_Depth, 8,
                                TAG_DONE);
        if(mode == INVALID_ID) mode = RTG_MODE_ID;
        if(mode != INVALID_ID)
            scr = OpenScreenTags(0, SA_DisplayID, mode, SA_Width, RTG_W, SA_Height, RTG_H,
                                 SA_Depth, 8, SA_Quiet, 1, SA_Type, CUSTOMSCREEN,
                                 SA_ShowTitle, 0, TAG_END);
        if(!scr)
            scr = OpenScreenTags(0, SA_Width, RTG_W, SA_Height, RTG_H, SA_Depth, 8,
                                 SA_Quiet, 1, SA_Type, CUSTOMSCREEN, SA_ShowTitle, 0,
                                 TAG_END);
    }
    if(!scr) return;
    pubscreen_locked = 0;
    disp_w = RTG_W; disp_h = RTG_H;

    rtg_w = RTG_W; rtg_h = RTG_H;            /* fixed 864x486 render buffer */
    playH = rtg_h;
    playW = (SH_ * rtg_h) / SW_;             /* 224*486/256 = 425, aspect-correct */
    if(playW > rtg_w){ playW = rtg_w; playH = (SW_ * rtg_w) / SH_; }
    gx0=(rtg_w-playW)/2; if(gx0<0)gx0=0;
    gy0=(rtg_h-playH)/2; if(gy0<0)gy0=0;

    rtg_frame=(uint8_t*)AllocMem((size_t)rtg_w*rtg_h,MEMF_FAST|MEMF_CLEAR);
    if(!rtg_frame) rtg_frame=(uint8_t*)AllocMem((size_t)rtg_w*rtg_h,MEMF_PUBLIC|MEMF_CLEAR);
    if(!rtg_frame) return;

    win=OpenWindowTags(0, WA_CustomScreen,(ULONG)scr, WA_Left,0, WA_Top,0,
                       WA_Width,RTG_W, WA_Height,RTG_H,
                       WA_Borderless,TRUE, WA_Backdrop,TRUE,
                       WA_Activate,TRUE, WA_RMBTrap,TRUE, WA_IDCMP,IDCMP_RAWKEY, TAG_END);
    if(!win) return;
    ScreenToFront(scr); ActivateWindow(win);
    rtg_ok=1;

    intro_decoded=1;
    ai_init(scr, win, rtg_frame, rtg_w, rtg_h);
    ai_set_loader_enabled(1);
    if(!ai_run(&c1943_intro_cfg)){ hal_quit=1; return; }
    memset(keydown,0,sizeof keydown);

    g1943_late_init();            /* audio (AllocMem) + scores */
    upload_palette();             /* static game palette + bezel indices (on the pub screen) */
    draw_bezel();                 /* bezel into rtg_frame (present scales the whole frame) */
    present_win_full();           /* one full present; gameplay only refreshes the play rect */
    c1943_audio_amiga_open();     /* YM2203 -> Paula */
}

void hal_game_frame(void){
    poll_input();
    if(hal_quit){ shutdown_rtg(); return; }   /* (hi-score disk save disabled -- see glue) */
    if(g_menu){                   /* DIP-switch overlay (F10) */
        if(!rtg_ok) return;
        c1943_menu_draw(rtg_frame, rtg_w, rtg_h, ++g_menu_tick, g_menu_sel, g_dswa, g_dswb);
        present_win_full();         /* DIP menu is full-screen */
        frame_pace();
        return;
    }
    if(g_pause){ frame_pace(); return; }
    g1943_run_frame();
    c1943_audio_amiga_frame();    /* run sound Z80 + 2x YM2203, feed Paula */
    if(!rtg_ok) return;
    /* palette uploaded once (Ikari/GB); re-assert only when the DIP menu closes. */
    if(g_need_bezel){ upload_palette(); draw_bezel(); present_win_full(); g_need_bezel=0; render_gate=0; }
    if((render_gate++ % C1943_RENDER_DIV) != 0){ frame_pace(); return; }
    g1943_render_frame();
    scale_to_play();              /* rotate + aspect-correct scale game into rtg_frame play-rect */
    present_play_rect();          /* only refresh the scaled play area; static bezel persists */
    frame_pace();
}
