/* gaplus_menu.c -- shared Tiger-Heli-style boot loader + DIP screen renderer.
 * Draws into a plain 8-bit chunky buffer; layout scales with the buffer width so
 * the same code serves the RTG 864x486 screen and the AGA 320x288 screen.
 * See gaplus_menu.h for the DIP value-space convention. */
#include <string.h>
#include "gaplus_menu.h"

const uint8_t gm_palette[16][3]={
    {0,0,0},{8,10,22},{20,34,70},{45,70,130},
    {92,112,170},{150,76,52},{204,116,44},{238,190,72},
    {36,108,62},{58,166,94},{92,210,140},{180,220,245},
    {110,118,132},{58,62,74},{210,60,86},{250,250,240}
};

static const uint8_t font5x7[][7]={
    {0,0,0,0,0,0,0},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},{14,17,16,23,17,17,14},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{1,1,1,1,17,17,14},{17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},
    {30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},{0,0,0,31,0,0,0},{0,0,0,0,0,12,12}
};
static int glyph_index(char c){
    if(c==' ')return 0; if(c>='A'&&c<='Z')return 1+c-'A';
    if(c>='0'&&c<='9')return 27+c-'0'; if(c=='-')return 37; if(c=='.')return 38;
    return 0;
}

void gm_put_px(uint8_t *buf,int w,int h,int x,int y,uint8_t c){
    if((unsigned)x<(unsigned)w&&(unsigned)y<(unsigned)h) buf[y*w+x]=c;
}
void gm_fill_rect(uint8_t *buf,int w,int h,int x0,int y0,int rw,int rh,uint8_t c){
    int x1=x0+rw,y1=y0+rh; if(rw<=0||rh<=0)return;
    if(x1<=0||y1<=0||x0>=w||y0>=h)return;
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>w)x1=w; if(y1>h)y1=h;
    for(int y=y0;y<y1;y++) memset(buf+y*w+x0,c,x1-x0);
}
void gm_draw_text(uint8_t *buf,int w,int h,const char *s,int x,int y,int scale,uint8_t c){
    for(;*s;s++,x+=6*scale){
        const uint8_t *g=font5x7[glyph_index(*s)];
        for(int gy=0;gy<7;gy++)for(int gx=0;gx<5;gx++)
            if(g[gy]&(1<<(4-gx))) gm_fill_rect(buf,w,h,x+gx*scale,y+gy*scale,scale,scale,c);
    }
}
static int gm_text_width(const char *s,int scale){ int n=0; while(*s++)n++; return n*6*scale-scale; }
void gm_draw_text_center(uint8_t *buf,int w,int h,const char *s,int y,int scale,uint8_t c){
    gm_draw_text(buf,w,h,s,(w-gm_text_width(s,scale))/2,y,scale,c);
}

static const signed char gm_sin32[32]={
    0,12,24,35,45,53,59,63,64,63,59,53,45,35,24,12,
    0,-12,-24,-35,-45,-53,-59,-63,-64,-63,-59,-53,-45,-35,-24,-12 };
static void gm_starfield(uint8_t *buf,int w,int h,int tick){
    int span=(h>200)?150:96, n=(w>=640)?220:90;
    for(int i=0;i<n;i++){
        int depth=1+(i&3);
        int x=(i*97+tick*depth*3)%w;
        int y=10+((i*53+(tick*(depth-1))/2)%(h-span));
        uint8_t c=(depth==4)?15:(depth==3?11:12);
        gm_put_px(buf,w,h,x,y,c); if(depth>=3) gm_put_px(buf,w,h,x-1,y,c);
    }
}
static void gm_wavy_scroller(uint8_t *buf,int w,int h,const char *s,int y,int scale,uint8_t c,int phase){
    int tw=gm_text_width(s,scale);
    int x=w-((phase*3)%(tw+w+80));
    for(;*s;s++,x+=6*scale){
        char one[2];
        if(x<-6*scale||x>=w+6*scale) continue;
        int wave=gm_sin32[(phase+x/(scale?scale:1))&31]/5;
        one[0]=*s; one[1]=0; gm_draw_text(buf,w,h,one,x,y+wave,scale,c);
    }
}
static void gm_wavy_text(uint8_t *buf,int w,int h,const char *s,int x,int y,int scale,uint8_t c,int phase){
    for(;*s;s++,x+=6*scale){
        char one[2];
        int wave;
        if(x<-6*scale||x>=w+6*scale) continue;
        wave=gm_sin32[(phase+x/(scale?scale:1))&31]/5;
        one[0]=*s; one[1]=0; gm_draw_text(buf,w,h,one,x,y+wave,scale,c);
    }
}

/* ---- boot loader (Tiger-Heli style, rebranded GAPLUS, no demo) ---- */
void gm_draw_loader(uint8_t *buf,int w,int h,int tick,int ready,int blink){
    memset(buf,0,(size_t)w*h);                       /* deep-space black */
    gm_starfield(buf,w,h,tick);
    if(w>=640){
        int ky=h-90, lx=120, rx=w/2+72;
        gm_draw_text_center(buf,w,h,"GAPLUS",60,8,15);
        gm_draw_text_center(buf,w,h,"(C) 1984 NAMCO",148,2,14);
        gm_draw_text_center(buf,w,h,"AMIGA RTG ARCADE PORT  2026",178,2,11);
        gm_wavy_scroller(buf,w,h,"WELCOME TO GAPLUS.   WHITTY ARCADE BRINGS ANOTHER NAMCO CLASSIC TO THE AMIGA.   "
                         "INSERT A COIN WITH 5 OR THE LEFT SHOULDER  -  ENJOY THE GAME.",240,5,10,tick);
        gm_fill_rect(buf,w,h,0,h-162,w,162,1);
        gm_draw_text_center(buf,w,h,ready?"READY":"LOADING",h-156,3,10);
        if(ready&&blink) gm_draw_text_center(buf,w,h,"STARTING GAPLUS",h-130,2,15);
        gm_draw_text(buf,w,h,"GAPLUS KEYS",lx,ky-16,1,10);
        gm_draw_text(buf,w,h,"5 COIN 1    6 COIN 2",lx,ky,1,11);
        gm_draw_text(buf,w,h,"1 START 1P  2 START 2P",lx,ky+14,1,11);
        gm_draw_text(buf,w,h,"ARROWS MOVE  SPACE FIRE",lx,ky+28,1,11);
        gm_draw_text(buf,w,h,"P PAUSE   ESC EXIT",lx,ky+42,1,11);
        gm_draw_text(buf,w,h,"GAPLUS PAD",rx,ky-16,1,10);
        gm_draw_text(buf,w,h,"L COIN 1    R COIN 2",rx,ky,1,15);
        gm_draw_text(buf,w,h,"PLAY  START 1P",rx,ky+14,1,15);
        gm_draw_text(buf,w,h,"RED BLUE  FIRE",rx,ky+28,1,15);
    } else {
        /* Tiger-Heli AGA loader layout, rebranded for Gaplus. */
        for(int y=0;y<h;y++) gm_fill_rect(buf,w,h,0,y,w,1,(uint8_t)(1+(y*3)/h));
        gm_wavy_text(buf,w,h,"GAPLUS AGA",22,82,3,15,tick/2);
        gm_wavy_scroller(buf,w,h,"IN   2026   WHITTY   ARCADE   BRINGS   GAPLUS   THE   NAMCO   CLASSIC   TO   AMIGA   AGA.",
                         122,1,10,tick);
        gm_draw_text_center(buf,w,h,ready?"READY":"LOADING",146,2,11);
        if(ready&&blink) gm_draw_text_center(buf,w,h,"STARTING GAPLUS",172,1,15);
        gm_draw_text(buf,w,h,"PAD L COIN 1",42,202,1,11);
        gm_draw_text(buf,w,h,"PAD R COIN 2",42,214,1,11);
        gm_draw_text(buf,w,h,"PLAY START 1P",42,226,1,11);
        gm_draw_text(buf,w,h,"1 2 START",42,238,1,11);
        gm_draw_text(buf,w,h,"F10 DIPS",190,202,1,15);
        gm_draw_text(buf,w,h,"ESC EXIT",190,214,1,15);
        gm_draw_text(buf,w,h,"JOY MOVE",190,226,1,15);
        gm_draw_text(buf,w,h,"FIRE SHOOT",190,238,1,15);
    }
}

/* ---- DIP option model (MAME raw dipvalues, matches published settings 1:1) ---- */
static int gm_find(uint8_t v,uint8_t mask,const uint8_t *opts,int n){
    uint8_t cur=v&mask; for(int i=0;i<n;i++) if(opts[i]==cur)return i; return 0;
}
const char *gm_dip_value_text(int sel,const uint8_t m[4]){
    static const char *coin[]={"3C 1C","2C 1C","1C 1C","1C 2C"};
    static const uint8_t coinv[]={0x0,0x1,0x3,0x2};
    static const char *lives[]={"2","3","4","5"};
    static const uint8_t livesv[]={0x8,0xc,0x4,0x0};
    static const char *bonus[]={"30K 70K 70K","30K 100K 100K","30K 100K 200K","50K 100K 100K",
        "50K 100K 200K","50K 150K 150K","50K 150K 300K","50K 150K"};
    static const uint8_t bonusv[]={0x0,0x1,0x2,0x3,0x4,0x7,0x5,0x6};
    static const char *diff[]={"STD","EASIEST","2","3","4","5","6","HARDEST"};
    static const uint8_t diffv[]={0x7,0x6,0x5,0x4,0x3,0x2,0x1,0x0};
    switch(sel){
        case 0: return coin[gm_find(m[1],0x3,coinv,4)];   /* Coin A  -> DSWA_HIGH */
        case 1: return coin[gm_find(m[3],0x3,coinv,4)];   /* Coin B  -> DSWA_LOW  */
        case 2: return (m[3]&0x8)?"ON":"OFF";             /* Demo Sounds -> DSWA_LOW 0x8 */
        case 3: return lives[gm_find(m[1],0xc,livesv,4)]; /* Lives   -> DSWA_HIGH */
        case 4: return bonus[gm_find(m[2],0x7,bonusv,8)]; /* Bonus   -> DSWB_LOW  */
        case 5: return (m[2]&0x8)?"OFF":"ON";             /* Round Advance -> DSWB_LOW 0x8 */
        default: return diff[gm_find(m[0],0x7,diffv,8)];  /* Difficulty -> DSWB_HIGH */
    }
}
static void gm_cycle(uint8_t *v,uint8_t mask,const uint8_t *opts,int n,int dir){
    int i=gm_find(*v,mask,opts,n)+dir; if(i<0)i=n-1; if(i>=n)i=0;
    *v=(uint8_t)((*v&~mask)|opts[i]);
}
void gm_dip_change(int sel,int dir,uint8_t m[4]){
    static const uint8_t coinv[]={0x0,0x1,0x3,0x2};
    static const uint8_t livesv[]={0x8,0xc,0x4,0x0};
    static const uint8_t bonusv[]={0x0,0x1,0x2,0x3,0x4,0x7,0x5,0x6};
    static const uint8_t diffv[]={0x7,0x6,0x5,0x4,0x3,0x2,0x1,0x0};
    switch(sel){
        case 0: gm_cycle(&m[1],0x3,coinv,4,dir); break;   /* Coin A  */
        case 1: gm_cycle(&m[3],0x3,coinv,4,dir); break;   /* Coin B  */
        case 2: m[3]^=0x8; break;                         /* Demo Sounds */
        case 3: gm_cycle(&m[1],0xc,livesv,4,dir); break;  /* Lives   */
        case 4: gm_cycle(&m[2],0x7,bonusv,8,dir); break;  /* Bonus   */
        case 5: m[2]^=0x8; break;                         /* Round Advance */
        default: gm_cycle(&m[0],0x7,diffv,8,dir); break;  /* Difficulty */
    }
}

void gm_draw_dip(uint8_t *buf,int w,int h,int tick,int sel,const uint8_t m[4]){
    static const char *names[]={"COIN A","COIN B","DEMO SOUNDS","LIVES","BONUS LIFE","ROUND ADVANCE","DIFFICULTY"};
    for(int y=0;y<h;y++) memset(buf+(size_t)y*w,(y<h/2)?1:2,w);
    gm_starfield(buf,w,h,tick);
    if(w>=640){
        gm_fill_rect(buf,w,h,84,48,w-168,h-96,13);
        gm_fill_rect(buf,w,h,92,56,w-184,h-112,1);
        gm_draw_text_center(buf,w,h,"GAPLUS DIP SWITCHES",80,3,10);
        gm_draw_text_center(buf,w,h,"UP DOWN SELECT   LEFT RIGHT CHANGE",116,1,11);
        gm_draw_text_center(buf,w,h,"START APPLY   ESC CANCEL",134,1,11);
        for(int i=0;i<GM_DIP_ITEMS;i++){
            int y=174+i*32; uint8_t c=(i==sel)?15:11;
            if(i==sel) gm_fill_rect(buf,w,h,126,y-7,w-252,28,3);
            gm_draw_text(buf,w,h,names[i],154,y,2,c);
            gm_draw_text(buf,w,h,gm_dip_value_text(i,m),468,y,2,c);
        }
    } else {
        /* compact 320x288 AGA layout */
        gm_fill_rect(buf,w,h,16,16,w-32,h-32,13);
        gm_fill_rect(buf,w,h,20,20,w-40,h-40,1);
        gm_draw_text_center(buf,w,h,"GAPLUS DIP SWITCHES",28,1,10);
        gm_draw_text_center(buf,w,h,"UP DN SEL  L R CHANGE  START APPLY",44,1,11);
        for(int i=0;i<GM_DIP_ITEMS;i++){
            int y=64+i*26; uint8_t c=(i==sel)?15:11;
            if(i==sel) gm_fill_rect(buf,w,h,28,y-3,w-56,20,3);
            gm_draw_text(buf,w,h,names[i],36,y,1,c);
            gm_draw_text(buf,w,h,gm_dip_value_text(i,m),200,y,1,c);
        }
    }
}
