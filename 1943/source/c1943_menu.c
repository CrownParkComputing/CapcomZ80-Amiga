/* c1943_menu.c -- 1943 DIP-switch overlay (F10), Tiger-Heli/Gaplus style. Self-
 * contained chunky-buffer renderer (font + helpers adapted from gaplus_menu.c) with
 * a 1943-specific option model over DSWA (0xc003) / DSWB (0xc004).
 *
 * DSWA: difficulty 0x0f (0x0f=1 easiest .. 0x00=16 hardest), flip 0x20 (0x20=off).
 * DSWB: coin A 0x07, allow-continue 0x40 (0x40=yes), demo sounds 0x80 (0x80=on).
 */
#include <string.h>
#include "c1943_menu.h"

const uint8_t c1943_menu_pal[16][3] = {
    {0,0,0},{8,10,22},{20,34,70},{45,70,130},
    {92,112,170},{150,76,52},{204,116,44},{238,190,72},
    {36,108,62},{58,166,94},{92,210,140},{180,220,245},
    {110,118,132},{58,62,74},{210,60,86},{250,250,240}
};

static const uint8_t font5x7[][7] = {
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
static void mrect(uint8_t *b,int w,int h,int x0,int y0,int rw,int rh,uint8_t c){
    int x1=x0+rw,y1=y0+rh; if(rw<=0||rh<=0)return;
    if(x1<=0||y1<=0||x0>=w||y0>=h)return;
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>w)x1=w; if(y1>h)y1=h;
    for(int y=y0;y<y1;y++) memset(b+(size_t)y*w+x0,c,x1-x0);
}
static void mtext(uint8_t *b,int w,int h,const char *s,int x,int y,int scale,uint8_t c){
    for(;*s;s++,x+=6*scale){
        const uint8_t *g=font5x7[glyph_index(*s)];
        for(int gy=0;gy<7;gy++)for(int gx=0;gx<5;gx++)
            if(g[gy]&(1<<(4-gx))) mrect(b,w,h,x+gx*scale,y+gy*scale,scale,scale,c);
    }
}
static int twid(const char *s,int scale){ int n=0; while(*s++)n++; return n*6*scale-scale; }
static void mtext_c(uint8_t *b,int w,int h,const char *s,int y,int scale,uint8_t c){
    mtext(b,w,h,s,(w-twid(s,scale))/2,y,scale,c);
}
static void starfield(uint8_t *b,int w,int h,int tick){
    int n=(w>=640)?180:80;
    for(int i=0;i<n;i++){
        int depth=1+(i&3);
        int x=(i*97+tick*depth*3)%w;
        int y=10+((i*53)%(h-20));
        uint8_t c=(depth==4)?15:(depth==3?11:12);
        if((unsigned)x<(unsigned)w&&(unsigned)y<(unsigned)h) b[(size_t)y*w+x]=c;
    }
}

void c1943_loader_draw(uint8_t *b, int w, int h, int tick, int ready, int blink){
    memset(b,0,(size_t)w*h);
    starfield(b,w,h,tick);
#ifdef C1943_KAI
    mtext_c(b,w,h,"1943 KAI",26,4,15);
    mtext_c(b,w,h,"CAPCOM Z80 BOARD",68,1,14);
#else
    mtext_c(b,w,h,"1943",26,5,15);
    mtext_c(b,w,h,"CAPCOM Z80 BOARD",68,1,14);
#endif
    mtext_c(b,w,h,"MAIN Z80  SOUND Z80  2x YM2203",86,1,11);
    mrect(b,w,h,0,h-98,w,98,1);
    mtext_c(b,w,h,ready?"READY":"STARTING UP",h-88,2,10);
    if(ready && blink) mtext_c(b,w,h,"FIRE OR START",h-64,1,15);
    mtext_c(b,w,h,"5 / L R COIN   1 / PLAY START",h-44,1,11);
    mtext_c(b,w,h,"ARROWS MOVE   RED FIRE   BLUE SPECIAL",h-30,1,14);
    mtext_c(b,w,h,"PAULA AUDIO   F10 DIPS   ESC EXIT",h-16,1,11);
}

/* ---- 1943 DIP option model ---- */
static const char *const difftxt[16] = {
    "1 EASIEST","2","3","4","5","6","7","8 NORMAL",
    "9","10","11","12","13","14","15","16 HARDEST" };
static const char *const cointxt[8] = { "4C 1C","3C 1C","2C 1C","1C 5C","1C 4C","1C 3C","1C 2C","1C 1C" };
static const uint8_t coinv[8]        = {  0x00,  0x01,  0x02,  0x03,  0x04,  0x05,  0x06,  0x07 };

static int coin_idx(uint8_t dswb){ uint8_t c=dswb&0x07; for(int i=0;i<8;i++) if(coinv[i]==c)return i; return 7; }

static const char *value_text(int sel, uint8_t dswa, uint8_t dswb){
    switch(sel){
        case 0: return difftxt[15-(dswa&0x0f)];          /* difficulty */
        case 1: return cointxt[coin_idx(dswb)];           /* coin A */
        case 2: return (dswb&0x40)?"YES":"NO";            /* allow continue */
        case 3: return (dswb&0x80)?"ON":"OFF";            /* demo sounds */
        default:return (dswa&0x20)?"OFF":"ON";            /* flip screen */
    }
}

void c1943_menu_change(int sel, int dir, uint8_t *dswa, uint8_t *dswb){
    switch(sel){
        case 0: {                                         /* difficulty (idx 0=easy..15=hard) */
            int idx = 15-(*dswa&0x0f) + dir; if(idx<0)idx=0; if(idx>15)idx=15;
            *dswa = (uint8_t)((*dswa&~0x0f) | (15-idx));
        } break;
        case 1: {                                         /* coin A */
            int i = coin_idx(*dswb)+dir; if(i<0)i=7; if(i>7)i=0;
            *dswb = (uint8_t)((*dswb&~0x07) | coinv[i]);
        } break;
        case 2: *dswb ^= 0x40; break;                     /* continue */
        case 3: *dswb ^= 0x80; break;                     /* demo sounds */
        default:*dswa ^= 0x20; break;                     /* flip screen */
    }
}

void c1943_menu_draw(uint8_t *b, int w, int h, int tick, int sel, uint8_t dswa, uint8_t dswb){
    static const char *const names[C1943_MENU_ITEMS] =
        { "DIFFICULTY","COIN A","ALLOW CONTINUE","DEMO SOUNDS","FLIP SCREEN" };
    for(int y=0;y<h;y++) memset(b+(size_t)y*w,(y<h/2)?1:2,w);
    starfield(b,w,h,tick);
    if(w>=640){
        mrect(b,w,h,84,40,w-168,h-80,13);
        mrect(b,w,h,92,48,w-184,h-96,1);
        mtext_c(b,w,h,"1943  DIP SWITCHES",62,4,7);
        mtext_c(b,w,h,"UP DOWN SELECT     LEFT RIGHT CHANGE",128,1,11);
        mtext_c(b,w,h,"F10 / START / ESC  CLOSE",150,1,11);
        for(int i=0;i<C1943_MENU_ITEMS;i++){
            int y=196+i*40; uint8_t c=(i==sel)?15:11;
            if(i==sel) mrect(b,w,h,126,y-8,w-252,32,3);
            mtext(b,w,h,names[i],158,y,2,c);
            mtext(b,w,h,value_text(i,dswa,dswb),498,y,2,c);
        }
    } else {
        mrect(b,w,h,12,12,w-24,h-24,13);
        mrect(b,w,h,16,16,w-32,h-32,1);
        mtext_c(b,w,h,"1943 DIP SWITCHES",24,1,7);
        mtext_c(b,w,h,"UP DN SEL  L R CHANGE",40,1,11);
        for(int i=0;i<C1943_MENU_ITEMS;i++){
            int y=64+i*26; uint8_t c=(i==sel)?15:11;
            if(i==sel) mrect(b,w,h,24,y-3,w-48,20,3);
            mtext(b,w,h,names[i],32,y,1,c);
            mtext(b,w,h,value_text(i,dswa,dswb),188,y,1,c);
        }
    }
}
