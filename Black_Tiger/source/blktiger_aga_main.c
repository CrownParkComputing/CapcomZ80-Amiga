/* blktiger_aga_main.c -- Black Tiger fixed RTG/chunky presenter.
 *
 * The renderer emits a 256x224 frame of 10-bit arcade palette indices. Gameplay uses
 * a fixed RGB332 palette, so palette changes only rebuild a cheap index->pen LUT.
 * Per-frame output writes only the centered play rectangle; the full screen is used
 * for loader/clear only. */
#include <exec/exec.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/videocontrol.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <stdint.h>
#include <string.h>
#include "z80emu.h"
#include "blktiger_machine.h"
#include "blktiger_render.h"
#include "arcade_intro.h"
#include "gaplus_menu.h"
#include "blktiger_rtg_bezel.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase = 0;

#define DISP_W BT_RTG_W
#define DISP_H BT_RTG_H
#define OUT_W BT_GAME_W
#define OUT_H BT_GAME_H
#define GX0 BT_GAME_X
#define GY0 BT_GAME_Y

extern const unsigned char blktiger_maincpu[];
extern const unsigned char blktiger_chars[];
extern const unsigned char blktiger_tiles[];
extern const unsigned char blktiger_sprites[];
extern const unsigned char blktiger_snd[];          /* sound Z80 program (bd-06.1l) */
extern const unsigned char ai_default_mod[], ai_default_mod_end[];
extern const unsigned char blktiger_rtg_bezel[], blktiger_rtg_bezel_end[];

/* sound chain: audio Z80 + 2x YM2203 -> Paula (blktiger_audio.c + _amiga.c) */
extern void bt_audio_init(const unsigned char *snd);
extern void bt_audio_amiga_open(void);
extern void bt_audio_amiga_frame(void);
extern void bt_audio_amiga_close(void);
extern void bt_audio_shutdown(void);                /* free the YM2203 OPN state on exit */

static struct Screen *scr;
static struct Window *win;
static struct ScreenBuffer *sb[2];
static int back = 1, ok;
static int pubscreen_locked;
static MY_LITTLE_Z80 z;

static uint16_t frame16[BT_NH][BT_NW];
static uint8_t  frame8[BT_NH][BT_NW];
static uint8_t  chunky[DISP_H][DISP_W];     /* full-screen loader buffer */
static uint8_t  frame_scaled[OUT_H][OUT_W];
static uint8_t *disp_frame;
static unsigned disp_w, disp_h;
static uint32_t agargb[1 + 256*3 + 1];
static uint8_t  lut[1024];
static uint32_t pal_csum = 0xffffffff;   /* last palette-RAM checksum (rebuild only on change) */
static unsigned char keydown[128];
static uint8_t bt_dsw0 = 0xff, bt_dsw1 = 0x6f;
unsigned char hal_quit = 0;

static void clear_screen_buffers(void);

static struct MsgPort *timer_port;
static struct timerequest *timer_io;
static ULONG frame_ticks, next_tick;

static void close_timer(void){
    if(timer_io){
        if(TimerBase) CloseDevice((struct IORequest*)timer_io);
        DeleteIORequest((struct IORequest*)timer_io);
        timer_io=0;
    }
    if(timer_port){ DeleteMsgPort(timer_port); timer_port=0; }
    TimerBase=0; frame_ticks=next_tick=0;
}

static void open_timer(void){
    struct EClockVal ev; ULONG rate;
    timer_port=CreateMsgPort();
    if(!timer_port) return;
    timer_io=(struct timerequest*)CreateIORequest(timer_port,sizeof(*timer_io));
    if(!timer_io){ close_timer(); return; }
    if(OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_ECLOCK,(struct IORequest*)timer_io,0)!=0){
        close_timer(); return;
    }
    TimerBase=timer_io->tr_node.io_Device;
    rate=ReadEClock(&ev);
    frame_ticks=(rate+30)/60;
    if(frame_ticks<1) frame_ticks=1;
    next_tick=ev.ev_lo;
}

static void frame_pace(void){
    struct EClockVal ev; ULONG now;
    if(!TimerBase || !frame_ticks){ WaitTOF(); return; }
    ReadEClock(&ev); now=ev.ev_lo;
    if((LONG)(now-next_tick) > (LONG)frame_ticks){ next_tick=now; return; }
    next_tick += frame_ticks;
    do{
        ReadEClock(&ev);
        now=ev.ev_lo;
    }while((LONG)(now-next_tick) < 0);
}

static int bt_booted;
static void bt_intro_warmup(void *c){
    (void)c;
    if(!bt_booted){
        for(int i=0;i<600;i++) bt_run_frame(&z);
        bt_booted=1;
    }
}
static int bt_intro_ready(void *c){ (void)c; return bt_booted; }
static const char *const bt_intro_keys[] = {
    "ARROWS  MOVE", "SPACE / CTRL  JUMP", "ALT  ATTACK",
    "5 COIN   1 START", "F10 DIP SWITCHES", "ESC EXIT", 0
};
static const char *const bt_intro_pad[] = {
    "STICK  MOVE", "RED  JUMP", "BLUE / YELLOW / GREEN  ATTACK",
    "L / R COIN   PLAY START", "L + R + PLAY DIP SWITCHES", 0
};
static const ai_dip_opt bt_dip_coin_a[] = {
    {0x00,"4C 1C"}, {0x01,"3C 1C"}, {0x02,"2C 1C"}, {0x07,"1C 1C"},
    {0x06,"1C 2C"}, {0x05,"1C 3C"}, {0x04,"1C 4C"}, {0x03,"1C 5C"}
};
static const ai_dip_opt bt_dip_coin_b[] = {
    {0x00,"4C 1C"}, {0x08,"3C 1C"}, {0x10,"2C 1C"}, {0x38,"1C 1C"},
    {0x30,"1C 2C"}, {0x28,"1C 3C"}, {0x20,"1C 4C"}, {0x18,"1C 5C"}
};
static const ai_dip_opt bt_dip_flip[] = { {0x40,"OFF"}, {0x00,"ON"} };
static const ai_dip_opt bt_dip_lives[] = { {0x02,"2"}, {0x03,"3"}, {0x01,"5"}, {0x00,"7"} };
static const ai_dip_opt bt_dip_diff[] = {
    {0x1c,"1 EASIEST"}, {0x18,"2"}, {0x14,"3"}, {0x10,"4"},
    {0x0c,"5 NORMAL"}, {0x08,"6"}, {0x04,"7"}, {0x00,"8 HARDEST"}
};
static const ai_dip_opt bt_dip_demo[] = { {0x00,"OFF"}, {0x20,"ON"} };
static const ai_dip_opt bt_dip_continue[] = { {0x00,"NO"}, {0x40,"YES"} };
static const ai_dip_opt bt_dip_cabinet[] = { {0x00,"UPRIGHT"}, {0x80,"COCKTAIL"} };
static const ai_dip_item bt_dip_items[] = {
    {"COIN A",0,0x07,8,bt_dip_coin_a},
    {"COIN B",0,0x38,8,bt_dip_coin_b},
    {"FLIP SCREEN",0,0x40,2,bt_dip_flip},
    {"LIVES",1,0x03,4,bt_dip_lives},
    {"DIFFICULTY",1,0x1c,8,bt_dip_diff},
    {"DEMO SOUNDS",1,0x20,2,bt_dip_demo},
    {"ALLOW CONTINUE",1,0x40,2,bt_dip_continue},
    {"CABINET",1,0x80,2,bt_dip_cabinet}
};
static void bt_apply_dips(void *ctx){
    (void)ctx;
    bt_set_inputs(0xff, 0xff, 0xff, bt_dsw0, bt_dsw1);
}
static const ai_dip_config bt_dip_cfg = {
    bt_dip_items, (int)(sizeof bt_dip_items / sizeof bt_dip_items[0]),
    &bt_dsw0, &bt_dsw1,
    bt_apply_dips, 0
};
static const ai_config bt_intro_cfg = {
    "BLACK TIGER",
    "WHITTY ARCADE PRESENTS BLACK TIGER    CAPCOM 1987 Z80 HARDWARE    MAIN Z80 INTERPRETED WITH BANKED PROGRAM ROMS BANKED BACKGROUND RAM SPRITES TEXT AND XBRG PALETTE RAM    SOUND BOARD IS Z80 PLUS TWO YM2203 CHIPS MIXED TO PAULA AT 7920 HZ    RTG OUTPUT USES AN 864 BY 486 RGB332 BEZEL WITH PLAY WINDOW REFRESH    PRESS FIRE OR START WHEN READY    ",
    bt_intro_keys, bt_intro_pad,
    ai_default_mod, ai_default_mod_end,
    0, 150,
    bt_intro_ready, bt_intro_warmup, 0,
    &bt_dip_cfg
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
#define JOY0DAT  (*(volatile unsigned short *)0xdff00aUL)
#define PORT1_FIRE 0x80
#define PORT0_FIRE 0x40
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
/* Read a CD32 controller on port 1: drive the fire line as a clock and shift the 7
 * face buttons out on POTINP DATRY. A plain 1/2-button joystick just returns junk here
 * but its red(fire1=CIAA) + blue(fire2=POTINP 0x4000) are still read directly below. */
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

/* ----- chunky->planar (word writes, 16px groups). dx must be 16-aligned. ----- */
static void c2p_region(struct BitMap *bm, const uint8_t *src, int stride, int dx, int dy, int w, int h){
    int bpr = bm->BytesPerRow;     /* unrolled 8-plane c2p (1943-style): no inner plane loop,
                                    * cached plane ptrs, word stores -- ~2x the naive version */
    uint8_t *p0=bm->Planes[0],*p1=bm->Planes[1],*p2=bm->Planes[2],*p3=bm->Planes[3];
    uint8_t *p4=bm->Planes[4],*p5=bm->Planes[5],*p6=bm->Planes[6],*p7=bm->Planes[7];
    for(int y=0;y<h;y++){
        const uint8_t *s = src + (size_t)y*stride;
        int row = (dy+y)*bpr + (dx>>3);
        for(int x=0;x<w;x+=16){
            const uint8_t *q = s + x;
            uint16_t w0=0,w1=0,w2=0,w3=0,w4=0,w5=0,w6=0,w7=0;
            for(int k=0;k<16;k++){
                unsigned v=q[k]; uint16_t bit=(uint16_t)(0x8000u>>k);
                if(v&0x01)w0|=bit; if(v&0x02)w1|=bit; if(v&0x04)w2|=bit; if(v&0x08)w3|=bit;
                if(v&0x10)w4|=bit; if(v&0x20)w5|=bit; if(v&0x40)w6|=bit; if(v&0x80)w7|=bit;
            }
            *(uint16_t*)(p0+row)=w0; *(uint16_t*)(p1+row)=w1; *(uint16_t*)(p2+row)=w2; *(uint16_t*)(p3+row)=w3;
            *(uint16_t*)(p4+row)=w4; *(uint16_t*)(p5+row)=w5; *(uint16_t*)(p6+row)=w6; *(uint16_t*)(p7+row)=w7;
            row += 2;
        }
    }
}
static void aga_flip(const uint8_t *src, int stride, int dx, int dy, int w, int h){
    c2p_region(sb[back]->sb_BitMap, src, stride, dx, dy, w, h);
    while(!ChangeScreenBuffer(scr, sb[back])) ;
    back ^= 1;
}

static void intro_present(void){
    if(win){
        WriteChunkyPixels(win->RPort, 0, 0, DISP_W-1, DISP_H-1, (uint8_t*)chunky, DISP_W);
    } else {
        aga_flip((const uint8_t*)chunky, DISP_W, 0, 0, DISP_W, DISP_H);
    }
}

static void loader_palette(void){
    agargb[0] = (16u << 16) | 0u;
    for(int i=0;i<16;i++){
        agargb[1+i*3+0] = (uint32_t)gm_palette[i][0] * 0x01010101u;
        agargb[1+i*3+1] = (uint32_t)gm_palette[i][1] * 0x01010101u;
        agargb[1+i*3+2] = (uint32_t)gm_palette[i][2] * 0x01010101u;
    }
    agargb[1+16*3] = 0;
    LoadRGB32(&scr->ViewPort, agargb);
}

static void draw_loader_screen(int tick, int ready, int blink){
    memset(chunky, 0, sizeof chunky);
    for(int i=0;i<90;i++){
        int d=1+(i&3), x=(i*97+tick*d*3)%DISP_W, y=10+((i*53+tick)%(DISP_H-96));
        gm_put_px((uint8_t*)chunky,DISP_W,DISP_H,x,y,(d>=3)?15:12);
    }
    gm_draw_text_center((uint8_t*)chunky,DISP_W,DISP_H,"BLACK TIGER",26,4,15);
    gm_draw_text_center((uint8_t*)chunky,DISP_W,DISP_H,"CAPCOM Z80 BOARD",66,1,14);
    gm_draw_text_center((uint8_t*)chunky,DISP_W,DISP_H,"Z80  +  2x YM2203  +  PAULA",84,1,11);
    gm_fill_rect((uint8_t*)chunky,DISP_W,DISP_H,0,158,DISP_W,98,1);
    gm_draw_text_center((uint8_t*)chunky,DISP_W,DISP_H,ready?"READY":"STARTING UP",168,2,10);
    if(ready && blink) gm_draw_text_center((uint8_t*)chunky,DISP_W,DISP_H,"FIRE OR START TO PLAY",192,1,15);
    gm_draw_text((uint8_t*)chunky,DISP_W,DISP_H,"5 COIN    1 START    ESC EXIT",42,214,1,11);
    gm_draw_text((uint8_t*)chunky,DISP_W,DISP_H,"ARROWS MOVE   RED JUMP   BLUE ATTACK",42,228,1,14);
}

static void run_loader(void){
    int tick=0, sf_prev=1;
    loader_palette();
    for(;;){
        struct IntuiMessage *m;
        while(win && win->UserPort && (m=(struct IntuiMessage*)GetMsg(win->UserPort))){
            ULONG cls=m->Class; UWORD raw=m->Code; ReplyMsg((struct Message*)m);
            if(cls==IDCMP_RAWKEY) keydown[raw&0x7f]=(raw&0x80)?0:1;
        }
        if(keydown[RK_ESC]){ hal_quit=1; break; }
        if(!bt_booted) bt_intro_warmup(0);
        unsigned cd32 = read_cd32();
        int ready = bt_booted && tick >= 150;
        int fire = !(CIAA_PRA & PORT1_FIRE) || (cd32&(CD32_RED|CD32_BLUE|CD32_YELLOW|CD32_GREEN))
            || keydown[RK_SPACE] || keydown[RK_LCTRL] || keydown[RK_LALT] || keydown[RK_RALT];
        int start = keydown[RK_1] || (cd32&CD32_PLAY);
        int sf = fire || start;
        if(ready && sf && !sf_prev) break;
        sf_prev = sf;
        draw_loader_screen(tick, ready, ((tick/24)&1)==0);
        intro_present();
        WaitTOF();
        tick++;
    }
    memset(keydown,0,sizeof keydown);
    clear_screen_buffers();
}

static void scale_frame(void){
    for(int y=0;y<OUT_H;y++){
        const uint8_t *s = frame8[(y * BT_NH) / OUT_H];
        uint8_t *d = frame_scaled[y];
        for(int x=0;x<OUT_W;x++) d[x] = s[(x * BT_NW) / OUT_W];
    }
}

static uint8_t rgb332(unsigned r, unsigned g, unsigned b){
    return (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
}

static void draw_bezel(void){
    size_t bn;
    if(!disp_frame || !win) return;
    bn = (size_t)(blktiger_rtg_bezel_end - blktiger_rtg_bezel);
    if(bn >= (size_t)DISP_W * DISP_H)
        memcpy(disp_frame, blktiger_rtg_bezel, (size_t)DISP_W * DISP_H);
    WriteChunkyPixels(win->RPort, 0, 0, DISP_W-1, DISP_H-1, disp_frame, DISP_W);
}

static void upload_rgb332_palette(void){
    agargb[0] = (256u << 16) | 0u;
    for(int i=0;i<256;i++){
        unsigned r = ((unsigned)(i >> 5) * 255u) / 7u;
        unsigned g = ((unsigned)((i >> 2) & 7) * 255u) / 7u;
        unsigned b = ((unsigned)(i & 3) * 255u) / 3u;
        agargb[1+i*3+0] = (uint32_t)r * 0x01010101u;
        agargb[1+i*3+1] = (uint32_t)g * 0x01010101u;
        agargb[1+i*3+2] = (uint32_t)b * 0x01010101u;
    }
    agargb[1+256*3] = 0;
    LoadRGB32(&scr->ViewPort, agargb);
}

/* Rebuild only the index->RGB332 LUT from the live xBRG_444 palette RAM. */
static void rebuild_palette(const unsigned char *pram){
    for(int i=0;i<1024;i++){
        int lo = pram[i], hi = pram[0x400+i];
        unsigned r = (unsigned)((lo >> 4) & 0x0f) * 0x11u;
        unsigned g = (unsigned)(lo & 0x0f) * 0x11u;
        unsigned b = (unsigned)(hi & 0x0f) * 0x11u;
        lut[i] = rgb332(r, g, b);
    }
}

/* ----- present: rebuild the palette LUT only when it changes, then refresh play rect. ----- */
static void present(void){
    const unsigned char *pram = bt_palette(&z);   /* lo[0x400]@d800, hi[0x400]@dc00 */
    uint32_t cs = 2166136261u; const uint32_t *pw = (const uint32_t*)pram;  /* FNV-1a over 2KB pal RAM */
    for(int i=0;i<0x800/4;i++) cs = (cs ^ pw[i]) * 16777619u;
    if(cs != pal_csum){ rebuild_palette(pram); pal_csum = cs; }
    for(int y=0;y<BT_NH;y++) for(int x=0;x<BT_NW;x++)
        frame8[y][x] = lut[frame16[y][x] & 0x3ff];
    if(win && disp_frame){
        for(int y=0;y<OUT_H;y++){
            const uint8_t *src = frame8[(y * BT_NH) / OUT_H];
            uint8_t *dst = disp_frame + (size_t)(GY0 + y) * DISP_W + GX0;
            for(int x=0;x<OUT_W;x++) dst[x] = src[(x * BT_NW) / OUT_W];
        }
        WriteChunkyPixels(win->RPort, GX0, GY0, GX0+OUT_W-1, GY0+OUT_H-1,
                          disp_frame + (size_t)GY0 * DISP_W + GX0, DISP_W);
    } else {
        scale_frame();
        aga_flip((const uint8_t*)frame_scaled, OUT_W, GX0, GY0, OUT_W, OUT_H);
    }
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
            bt_audio_amiga_close();
            ai_dip_open(&bt_dip_cfg);
            upload_rgb332_palette();
            draw_bezel();
            pal_csum = 0xffffffff;
            bt_audio_amiga_open();
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
    int b2 = fire2 || (cd32 & (CD32_BLUE|CD32_YELLOW|CD32_GREEN)) || keydown[RK_LALT] || keydown[RK_RALT]; /* ATTACK */

    uint8_t sys=0xff, p1=0xff, p2=0xff;
    static int ck,sk,ch,sh;
    int coin=keydown[RK_5] || (cd32 & (CD32_LSHOULDER|CD32_RSHOULDER));   /* pad shoulder = COIN  */
    int start=keydown[RK_1] || (cd32 & CD32_PLAY);                        /* pad PLAY     = START */
    if(coin&&!ck)ch=8; if(start&&!sk)sh=8; ck=coin; sk=start;
    if(ch){ sys&=~0x40; ch--; } if(sh){ sys&=~0x01; sh--; }
    if(right)p1&=~0x01; if(left)p1&=~0x02; if(down)p1&=~0x04; if(up)p1&=~0x08;
    if(b1)p1&=~0x10; if(b2)p1&=~0x20;
    bt_set_inputs(sys, p1, p2, bt_dsw0, bt_dsw1);
}

static void shutdown(void){
    static int done; if(done) return; done=1;
    bt_audio_amiga_close();          /* stop Paula AUD0/1 DMA + free chip buffer FIRST */
    WaitTOF();                       /* let the audio DMA fully stop before teardown */
    close_timer();
    if(win){ CloseWindow(win); win=0; }
    if(disp_frame){ FreeMem(disp_frame, (size_t)disp_w * disp_h); disp_frame=0; }
    if(scr && pubscreen_locked){
        UnlockPubScreen(0, scr);
        scr=0;
        pubscreen_locked=0;
    } else if(scr){
        WaitTOF();
        if(sb[0]){ FreeScreenBuffer(scr, sb[0]); sb[0]=0; }
        if(sb[1]){ FreeScreenBuffer(scr, sb[1]); sb[1]=0; }
        CloseScreen(scr); scr=0;
    }
    if(GfxBase){ CloseLibrary((struct Library*)GfxBase); GfxBase=0; }
    if(IntuitionBase){ CloseLibrary((struct Library*)IntuitionBase); IntuitionBase=0; }
    bt_audio_shutdown();             /* free the YM2203 OPN core state (no leak under AGS) */
    ok=0;
}
void hal_cleanup(void){ shutdown(); }   /* called by amiga.s at every exit path */

static void clear_screen_buffers(void){
    if(win && disp_frame){
        memset(chunky, 0, sizeof chunky);
        memset(disp_frame, 0, (size_t)DISP_W * DISP_H);
        WriteChunkyPixels(win->RPort, 0, 0, DISP_W-1, DISP_H-1, disp_frame, DISP_W);
    } else {
        memset(chunky, 0, sizeof chunky);
        if(sb[0]) c2p_region(sb[0]->sb_BitMap, (const uint8_t*)chunky, DISP_W, 0,0, DISP_W, DISP_H);
        if(sb[1]) c2p_region(sb[1]->sb_BitMap, (const uint8_t*)chunky, DISP_W, 0,0, DISP_W, DISP_H);
    }
}

void hal_game_init(void){
    IntuitionBase=(struct IntuitionBase*)OpenLibrary((CONST_STRPTR)"intuition.library",39);
    GfxBase=(struct GfxBase*)OpenLibrary((CONST_STRPTR)"graphics.library",39);
    if(!IntuitionBase||!GfxBase) return;
    open_timer();

    bt_load_maincpu(blktiger_maincpu);
    bt_set_gfx(blktiger_chars, blktiger_tiles, blktiger_sprites);
    bt_render_init();              /* decode-cache gfx ROMs ONCE (the 1943 fix: no per-pixel bitplane fetch) */
    bt_init(&z);
    bt_set_inputs(0xff, 0xff, 0xff, bt_dsw0, bt_dsw1);

    {
        ULONG mode = BestModeID(BIDTAG_NominalWidth, DISP_W,
                                BIDTAG_NominalHeight, DISP_H,
                                BIDTAG_DesiredWidth, DISP_W,
                                BIDTAG_DesiredHeight, DISP_H,
                                BIDTAG_Depth, 8,
                                TAG_DONE);
        if(mode != INVALID_ID)
            scr=OpenScreenTags(0, SA_DisplayID, mode, SA_Width,DISP_W, SA_Height,DISP_H,
                               SA_Depth,8, SA_Quiet,1, SA_Type,CUSTOMSCREEN,
                               SA_ShowTitle,0, TAG_END);
        if(!scr)
            scr=OpenScreenTags(0, SA_Width,DISP_W, SA_Height,DISP_H, SA_Depth,8,
                               SA_Quiet,1, SA_Type,CUSTOMSCREEN, SA_ShowTitle,0, TAG_END);
    }
    if(!scr) return;
    pubscreen_locked=0;
    disp_w=DISP_W; disp_h=DISP_H;
    disp_frame=(uint8_t*)AllocMem((size_t)DISP_W * DISP_H, MEMF_FAST | MEMF_CLEAR);
    if(!disp_frame) disp_frame=(uint8_t*)AllocMem((size_t)DISP_W * DISP_H, MEMF_PUBLIC | MEMF_CLEAR);
    if(!disp_frame){ shutdown(); return; }
    win=OpenWindowTags(0, WA_CustomScreen,(ULONG)scr, WA_Left,0, WA_Top,0,
                       WA_Width,DISP_W, WA_Height,DISP_H, WA_Backdrop,TRUE, WA_Borderless,TRUE,
                       WA_Activate,TRUE, WA_RMBTrap,TRUE, WA_IDCMP,IDCMP_RAWKEY, TAG_END);
    if(win){ ScreenToFront(scr); ActivateWindow(win); }
    if(!win){ shutdown(); return; }
    clear_screen_buffers();
    ai_init(scr, win, disp_frame, DISP_W, DISP_H);
    ai_set_loader_enabled(1);
    if(!ai_run(&bt_intro_cfg)) hal_quit=1;
    if(hal_quit){ shutdown(); return; }
    upload_rgb332_palette();
    draw_bezel();
    pal_csum = 0xffffffff;
    /* sound: init the audio Z80 + 2x YM2203 (registers the machine's soundlatch/mem
     * hooks) and open Paula. AFTER the boot loop so the soundlatch FIFO starts clean
     * at the attract, and OS-safe (past the warmup; the OPN's AllocMem runs here). */
    bt_audio_init(blktiger_snd);
    bt_audio_amiga_open();           /* sound RE-ENABLED (audio now built -O1 -fno-strict-aliasing) */
    ok=1;
}

void hal_game_frame(void){
    poll_input();
    if(hal_quit){ shutdown(); return; }
    bt_run_frame(&z);
    if(!ok) return;
    bt_audio_amiga_frame();      /* refill Paula before RTG render work eats into lead */
    bt_render(&z, frame16);
    present();
    frame_pace();
}
