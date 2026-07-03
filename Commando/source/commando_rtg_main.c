/* Commando RTG presenter: interpreted main/audio Z80s, YM2203 audio,
 * cached software renderer, fixed RGB332 864x486 bezel screen. */
#include <exec/exec.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdint.h>
#include <string.h>
#include "z80emu.h"
#ifndef COMMANDO_NO_EMBEDDED_INTRO
#include "arcade_intro.h"
#endif
#include "commando_rtg_bezel.h"
#include "commando_rtg_render.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase = 0;
struct DosLibrary *DOSBase = 0;

extern const unsigned char commando_rom_main[], commando_rom_snd[];
extern const unsigned char commando_rtg_bezel[], commando_rtg_bezel_end[];
extern const unsigned char ai_default_mod[], ai_default_mod_end[];
extern void ccommando_load(const unsigned char *maincpu);
extern void ccommando_init(MY_LITTLE_Z80 *z);
extern void ccommando_run_frame(MY_LITTLE_Z80 *z);
extern void ccommando_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2);
extern void ccommando_set_dsw(unsigned char dsw0, unsigned char dsw1);
extern unsigned char ccommando_peek(MY_LITTLE_Z80 *z, unsigned a);
extern void commando_audio_init(const unsigned char *snd);
extern void commando_audio_shutdown(void);
extern void commando_audio_amiga_open(void);
extern void commando_audio_amiga_frame(void);

extern void commando_audio_amiga_close(void);

static struct Screen *scr;
static struct Window *win;
static MY_LITTLE_Z80 z;
static uint8_t *rtg_frame;
static unsigned disp_w, disp_h;
static int bezel_active, gx, gy, gw, gh, ok;
static uint32_t loadrgb[1 + 256 * 3 + 1];
static unsigned char keydown[128];
static uint8_t cmd_dsw0 = 0xff, cmd_dsw1 = 0x1f;
unsigned char hal_quit = 0;
static int game_paused;

static struct MsgPort *timer_port;
static struct timerequest *timer_io;
static ULONG frame_ticks, next_tick;

static void close_timer(void){
    if(timer_io){ if(TimerBase) CloseDevice((struct IORequest*)timer_io); DeleteIORequest((struct IORequest*)timer_io); timer_io=0; }
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
    next_tick += frame_ticks;
    do{ ReadEClock(&ev); now=ev.ev_lo; }while((LONG)(now-next_tick) < 0);
}

#ifndef COMMANDO_NO_EMBEDDED_INTRO
static int cmd_booted;
static void cmd_intro_warmup(void *c){
    (void)c;
    if(!cmd_booted){
        for(int i=0;i<600;i++) ccommando_run_frame(&z);
        cmd_booted=1;
    }
}
static int cmd_intro_ready(void *c){ (void)c; return cmd_booted; }
static const char *const cmd_intro_keys[] = {
    "ARROWS  MOVE", "SPACE / CTRL  SHOOT", "ALT  GRENADE",
    "5 COIN   1 START", "P PAUSE   F10 DIP SWITCHES", "ESC EXIT", 0
};
static const char *const cmd_intro_pad[] = {
    "STICK  MOVE", "RED  SHOOT", "BLUE / YELLOW / GREEN  GRENADE",
    "L / R COIN   PLAY START", "L + R + PLAY DIP SWITCHES", 0
};
static const ai_dip_opt cmd_dip_area[] = {
    {0x03,"0 FOREST 1"}, {0x01,"2 DESERT 1"}, {0x02,"4 FOREST 2"}, {0x00,"6 DESERT 2"}
};
static const ai_dip_opt cmd_dip_lives[] = { {0x04,"2"}, {0x0c,"3"}, {0x08,"4"}, {0x00,"5"} };
static const ai_dip_opt cmd_dip_coin_b[] = {
    {0x00,"4C 1C"}, {0x20,"3C 1C"}, {0x10,"2C 1C"}, {0x30,"1C 1C"}
};
static const ai_dip_opt cmd_dip_coin_a[] = {
    {0x00,"2C 1C"}, {0xc0,"1C 1C"}, {0x40,"1C 2C"}, {0x80,"1C 3C"}
};
static const ai_dip_opt cmd_dip_bonus[] = {
    {0x07,"10K 50K+"}, {0x03,"10K 60K+"}, {0x05,"20K 60K+"}, {0x01,"20K 70K+"},
    {0x06,"30K 70K+"}, {0x02,"30K 80K+"}, {0x04,"40K 100K+"}, {0x00,"NONE"}
};
static const ai_dip_opt cmd_dip_demo[] = { {0x00,"OFF"}, {0x08,"ON"} };
static const ai_dip_opt cmd_dip_diff[] = { {0x10,"NORMAL"}, {0x00,"DIFFICULT"} };
static const ai_dip_opt cmd_dip_flip[] = { {0x00,"OFF"}, {0x20,"ON"} };
static const ai_dip_opt cmd_dip_cabinet[] = {
    {0x00,"UPRIGHT"}, {0x40,"UPRIGHT 2P"}, {0xc0,"COCKTAIL"}
};
static const ai_dip_item cmd_dip_items[] = {
    {"STARTING AREA",0,0x03,4,cmd_dip_area},
    {"LIVES",0,0x0c,4,cmd_dip_lives},
    {"COIN B",0,0x30,4,cmd_dip_coin_b},
    {"COIN A",0,0xc0,4,cmd_dip_coin_a},
    {"BONUS LIFE",1,0x07,8,cmd_dip_bonus},
    {"DEMO SOUNDS",1,0x08,2,cmd_dip_demo},
    {"DIFFICULTY",1,0x10,2,cmd_dip_diff},
    {"FLIP SCREEN",1,0x20,2,cmd_dip_flip},
    {"CABINET",1,0xc0,3,cmd_dip_cabinet}
};
static void cmd_apply_dips(void *ctx){
    (void)ctx;
    ccommando_set_dsw(cmd_dsw0, cmd_dsw1);
}
static const ai_dip_config cmd_dip_cfg = {
    cmd_dip_items, (int)(sizeof cmd_dip_items / sizeof cmd_dip_items[0]),
    &cmd_dsw0, &cmd_dsw1,
    cmd_apply_dips, 0
};
static const ai_config cmd_intro_cfg = {
    "COMMANDO",
    "WHITTY ARCADE PRESENTS COMMANDO    CAPCOM 1985 Z80 HARDWARE    MAIN Z80 INTERPRETER AT SIXTY HERTZ WITH SCROLLING TILEMAP SPRITES FOREGROUND TEXT AND PROM PALETTE    SOUND BOARD IS Z80 PLUS TWO YM2203 CHIPS MIXED TO PAULA AT 8040 HZ    RTG OUTPUT USES AN 864 BY 486 BEZEL WITH A REFRESHED 224 BY 256 PLAY WINDOW    PRESS FIRE OR START WHEN READY    ",
    cmd_intro_keys, cmd_intro_pad,
    ai_default_mod, ai_default_mod_end,
    0, 150,
    cmd_intro_ready, cmd_intro_warmup, 0,
    &cmd_dip_cfg
};
#endif

#define RK_1 0x01
#define RK_5 0x05
#define RK_SPACE 0x40
#define RK_ESC 0x45
#define RK_F10 0x59
#define RK_P 0x19
#define RK_LCTRL 0x63
#define RK_LALT 0x64
#define RK_RALT 0x65
#define RK_UP 0x4C
#define RK_DOWN 0x4D
#define RK_RIGHT 0x4E
#define RK_LEFT 0x4F
#define CIAA_PRA (*(volatile unsigned char *)0xbfe001UL)
#define CIAA_DDRA (*(volatile unsigned char  *)0xbfe201UL)
#define JOY1DAT  (*(volatile unsigned short *)0xdff00cUL)
#define POTGO     (*(volatile unsigned short *)0xdff034UL)
#define POTINP    (*(volatile unsigned short *)0xdff016UL)
#define PORT1_FIRE 0x80
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

static void blit_bezel(void){
    if(!win || !rtg_frame) return;
    if(bezel_active){
        size_t n = (size_t)(commando_rtg_bezel_end - commando_rtg_bezel);
        if(n >= (size_t)CMD_RTG_W * CMD_RTG_H) memcpy(rtg_frame, commando_rtg_bezel, (size_t)CMD_RTG_W * CMD_RTG_H);
        WriteChunkyPixels(win->RPort, 0,0, CMD_RTG_W-1, CMD_RTG_H-1, rtg_frame, CMD_RTG_W);
    } else {
        memset(rtg_frame, 0, (size_t)disp_w * disp_h);
        SetRast(win->RPort, 0);
    }
}

static void present(void){
    if(!win || !rtg_frame) return;
    commando_rtg_render(&z, rtg_frame + (size_t)gy * disp_w + gx, (int)disp_w, gw, gh);
    WriteChunkyPixels(win->RPort, gx, gy, gx+gw-1, gy+gh-1,
                      rtg_frame + (size_t)gy * disp_w + gx, disp_w);
}

static void poll_input(void){
    struct IntuiMessage *m;
    if(win && win->UserPort)
        while((m=(struct IntuiMessage*)GetMsg(win->UserPort))){
            ULONG cls=m->Class; UWORD raw=m->Code; ReplyMsg((struct Message*)m);
            if(cls==IDCMP_RAWKEY) keydown[raw&0x7f]=(raw&0x80)?0:1;
        }
    if(keydown[RK_ESC]) hal_quit=1;

    static int potinit=0;
    if(!potinit){ POTGO=0xff00; potinit=1; }
    unsigned cd32 = read_cd32();
#ifndef COMMANDO_NO_EMBEDDED_INTRO
    {
        static int kf10, pdip;
        int dip_now = ai_cd32_dip_combo(cd32);
        if((keydown[RK_F10] && !kf10) || (dip_now && !pdip)){
            commando_audio_amiga_close();
            ai_dip_open(&cmd_dip_cfg);
            upload_rgb332_palette();
            blit_bezel();
            if(!game_paused) commando_audio_amiga_open();
            keydown[RK_F10] = 0;
        }
        kf10 = keydown[RK_F10];
        pdip = dip_now;
    }
#endif
    unsigned v=JOY1DAT;
    int right=(v>>1)&1,left=(v>>9)&1,down=((v>>1)^v)&1,up=((v>>9)^(v>>8))&1;
    if(keydown[RK_RIGHT])right=1; if(keydown[RK_LEFT])left=1;
    if(keydown[RK_DOWN])down=1;   if(keydown[RK_UP])up=1;
    int fire1 = !(CIAA_PRA & PORT1_FIRE);
    int fire2 = !(POTINP & CD32_DATRY);
    int shoot = fire1 || (cd32 & (CD32_RED|CD32_BLUE)) || keydown[RK_SPACE] || keydown[RK_LCTRL];
    int grenade = fire2 || (cd32 & (CD32_YELLOW|CD32_GREEN)) || keydown[RK_LALT] || keydown[RK_RALT];
    int playing = ccommando_peek(&z, 0xeda0) != 0;

    uint8_t sys=0xff, p1=0xff;
    static int ck, sk, ch, sh;
    int coin = keydown[RK_5] || (cd32 & (CD32_LSHOULDER|CD32_RSHOULDER));
    int start = keydown[RK_1] || (cd32 & CD32_PLAY);
    if(!playing){
        if(coin && !ck) ch=10;
        if(start && !sk) sh=40;
    }
    ck=coin; sk=start;
    if(ch){ sys&=~0x80; ch--; }
    if(sh){ sys&=~0x01; sh--; }
    if(right)p1&=~0x01; if(left)p1&=~0x02; if(down)p1&=~0x04; if(up)p1&=~0x08;
    if(shoot)p1&=~0x10; if(grenade)p1&=~0x20;
    if(ccommando_peek(&z, 0xe000) == 0) ccommando_set_inputs(0xff, 0xff, 0xff);
    else ccommando_set_inputs(sys, p1, 0xff);
}

static void shutdown(void){
    static int done; if(done) return; done=1;
    commando_audio_amiga_close();
    frame_pace();
    close_timer();
    if(win){ CloseWindow(win); win=0; }
    if(rtg_frame){ FreeMem(rtg_frame, (size_t)disp_w * disp_h); rtg_frame=0; }
    if(scr){ CloseScreen(scr); scr=0; }
    if(GfxBase){ CloseLibrary((struct Library*)GfxBase); GfxBase=0; }
    if(IntuitionBase){ CloseLibrary((struct Library*)IntuitionBase); IntuitionBase=0; }
    if(DOSBase){ CloseLibrary((struct Library*)DOSBase); DOSBase=0; }
    commando_audio_shutdown();
    ok=0;
}
void hal_cleanup(void){ shutdown(); }

static int try_open(int W, int H){
    ULONG modeid=BestModeID(BIDTAG_NominalWidth,(ULONG)W, BIDTAG_NominalHeight,(ULONG)H,
                            BIDTAG_DesiredWidth,(ULONG)W, BIDTAG_DesiredHeight,(ULONG)H,
                            BIDTAG_Depth,8, TAG_DONE);
    if(modeid!=INVALID_ID){
        scr=OpenScreenTags(0, SA_DisplayID, modeid, SA_Width,(ULONG)W, SA_Height,(ULONG)H,
                           SA_Depth,8, SA_Type,CUSTOMSCREEN, SA_Quiet,TRUE, SA_ShowTitle,FALSE, TAG_END);
        if(scr) return 1;
    }
    scr=OpenScreenTags(0, SA_Width,(ULONG)W, SA_Height,(ULONG)H,
                       SA_Depth,8, SA_Type,CUSTOMSCREEN, SA_Quiet,TRUE, SA_ShowTitle,FALSE, TAG_END);
    return scr != 0;
}

void hal_game_init(void){
    IntuitionBase=(struct IntuitionBase*)OpenLibrary((CONST_STRPTR)"intuition.library",39);
    GfxBase=(struct GfxBase*)OpenLibrary((CONST_STRPTR)"graphics.library",39);
    if(!IntuitionBase||!GfxBase){ hal_quit=1; return; }
    open_timer();

    ccommando_load(commando_rom_main);
    ccommando_set_inputs(0xff, 0xff, 0xff);
    ccommando_set_dsw(cmd_dsw0, cmd_dsw1);
    ccommando_init(&z);
    ccommando_set_dsw(cmd_dsw0, cmd_dsw1);
    commando_rtg_render_init();

    if(try_open(CMD_RTG_W, CMD_RTG_H) && scr->Width==CMD_RTG_W && scr->Height==CMD_RTG_H){
        bezel_active=1;
    } else {
        if(scr){ CloseScreen(scr); scr=0; }
        if(!try_open(640,480) && !try_open(800,600) && !try_open(640,512)){ hal_quit=1; return; }
        bezel_active=0;
    }
    disp_w=(unsigned)scr->Width;
    disp_h=(unsigned)scr->Height;
    if(bezel_active){
        gx=CMD_GAME_X; gy=CMD_GAME_Y; gw=CMD_GAME_W; gh=CMD_GAME_H;
    } else {
        int fw=(int)disp_w-32, fh=(int)disp_h-32;
        if(fw<1) fw=(int)disp_w; if(fh<1) fh=(int)disp_h;
        if((long)fw*CMD_NH <= (long)fh*CMD_NW){ gw=fw; gh=(CMD_NH*gw)/CMD_NW; }
        else                                  { gh=fh; gw=(CMD_NW*gh)/CMD_NH; }
        gx=((int)disp_w-gw)/2; gy=((int)disp_h-gh)/2;
    }
    rtg_frame=(uint8_t*)AllocMem((size_t)disp_w*disp_h, MEMF_FAST|MEMF_CLEAR);
    if(!rtg_frame) rtg_frame=(uint8_t*)AllocMem((size_t)disp_w*disp_h, MEMF_ANY|MEMF_CLEAR);
    if(!rtg_frame){ shutdown(); hal_quit=1; return; }
    win=OpenWindowTags(0, WA_CustomScreen,(ULONG)scr, WA_Left,0, WA_Top,0,
                       WA_Width,(ULONG)disp_w, WA_Height,(ULONG)disp_h, WA_Backdrop,TRUE,
                       WA_Borderless,TRUE, WA_Activate,TRUE, WA_RMBTrap,TRUE, WA_IDCMP,IDCMP_RAWKEY, TAG_END);
    if(win){ ScreenToFront(scr); ActivateWindow(win); }
    if(!win){ shutdown(); hal_quit=1; return; }
    SetRast(win->RPort, 0);
#ifndef COMMANDO_NO_EMBEDDED_INTRO
    ai_init(scr, win, rtg_frame, (int)disp_w, (int)disp_h);
    ai_set_loader_enabled(1);
    if(!ai_run(&cmd_intro_cfg)) hal_quit=1;
    if(hal_quit){ shutdown(); return; }
#endif
    upload_rgb332_palette();
    blit_bezel();
    commando_audio_init(commando_rom_snd);
    commando_audio_amiga_open();
    ok=1;
}

void hal_game_frame(void){
    static int kp;
    poll_input();
    if(hal_quit){ shutdown(); return; }
    if(keydown[RK_P] && !kp){
        game_paused = !game_paused;
        if(game_paused) commando_audio_amiga_close();
        else commando_audio_amiga_open();
        keydown[RK_P] = 0;
    }
    kp = keydown[RK_P];
    if(game_paused){
        frame_pace();
        return;
    }
    ccommando_run_frame(&z);
    if(!ok) return;
    commando_audio_amiga_frame();
    present();
    frame_pace();
}
