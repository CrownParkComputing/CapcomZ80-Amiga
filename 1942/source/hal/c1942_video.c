/* src/hal/c1942_video.c -- 1942 Amiga video: 7-bitplane full-320 AGA screen,
 * 320x256, copper-driven, double-buffered. The 256-colour palette is STATIC
 * (1942's palette_bank only re-selects bg tile colours, not the colour table),
 * so it is uploaded once. Adapted from src/hal/pl_video.c. */
#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <stdint.h>
#include "c1942_render.h"

struct GfxBase *GfxBase = 0;
extern void *_SysBase;

#define CUSTOM   ((volatile uint16_t *)0xdff000)
#define R_DMACON  (0x096/2)
#define R_DIWSTRT (0x08E/2)
#define R_DIWSTOP (0x090/2)
#define R_DDFSTRT (0x092/2)
#define R_DDFSTOP (0x094/2)
#define R_BPLCON0 (0x100/2)
#define R_BPLCON1 (0x102/2)
#define R_BPLCON2 (0x104/2)
#define R_BPLCON3 (0x106/2)
#define R_BPL1MOD (0x108/2)
#define R_BPL2MOD (0x10A/2)
#define R_COLOR00 (0x180/2)
#define R_COP1LCH (0x080/2)
#define R_VPOSR   (0x004/2)
#define R_BLTCON0 (0x040/2)
#define R_BLTCON1 (0x042/2)
#define R_BLTDMOD (0x066/2)
#define R_BLTDPTH (0x054/2)
#define R_BLTSIZE (0x058/2)
#define R_INTENA  (0x09A/2)
#define R_INTREQ  (0x09C/2)
#define DMACONR   ((volatile uint16_t *)0xdff002)
#define BPL_PTR_WORDS (C_NPLANES * 4)
#define COPPER_WORDS  (BPL_PTR_WORDS + 2)

static uint8_t  *fb[2] = { 0, 0 };
static uint16_t *copper = 0;

static void copper_point(uint8_t *buf){
    for (int p=0;p<C_NPLANES;p++){
        uint32_t a=(uint32_t)(buf+p*C_PLANE);
        copper[p*4+0]=(uint16_t)(0x00E0+p*4); copper[p*4+1]=(uint16_t)(a>>16);
        copper[p*4+2]=(uint16_t)(0x00E2+p*4); copper[p*4+3]=(uint16_t)(a&0xFFFF);
    }
    copper[BPL_PTR_WORDS+0]=0xFFFF; copper[BPL_PTR_WORDS+1]=0xFFFE;
}
static void wait_blit(void){ unsigned long g=0; (void)*DMACONR; while((*DMACONR&0x4000)&&++g<2000000UL); }
static void blit_clear(unsigned char *buf){
    volatile uint16_t *c=CUSTOM; uint32_t a=(uint32_t)buf;
    int h=(int)(((uint32_t)C_NPLANES*C_PLANE/2)/64);     /* whole display buffer */
    wait_blit();
    c[R_BLTCON0]=0x0100; c[R_BLTCON1]=0; c[R_BLTDMOD]=0;
    c[R_BLTDPTH]=(uint16_t)(a>>16); c[R_BLTDPTH+1]=(uint16_t)(a&0xFFFF);
    c[R_BLTSIZE]=(uint16_t)((h<<6)|0); wait_blit();
}
static void upload_palette(void){
    volatile uint16_t *c=CUSTOM; static uint8_t rgb[256][3];
    c1942_build_palette(rgb);
    /* AGA 8-bit colour needs two passes per bank: LOCT=0 loads the high nibble of
     * each channel, LOCT=1 (BPLCON3 bit9) loads the low nibble. Loading only the
     * high nibble truncates the 1942 PROM shades (e.g. weight 0x0e -> 0 = black),
     * which dropped the dim colours. */
    for (int b=0;b<8;b++){ c[R_BPLCON3]=(uint16_t)(b<<13);          /* hi nibble */
        for (int i=0;i<32;i++){ int e=b*32+i;
            c[R_COLOR00+i]=(uint16_t)(((rgb[e][0]>>4)<<8)|((rgb[e][1]>>4)<<4)|(rgb[e][2]>>4)); } }
    for (int b=0;b<8;b++){ c[R_BPLCON3]=(uint16_t)((b<<13)|0x0200);  /* lo nibble (LOCT) */
        for (int i=0;i<32;i++){ int e=b*32+i;
            c[R_COLOR00+i]=(uint16_t)(((rgb[e][0]&0xf)<<8)|((rgb[e][1]&0xf)<<4)|(rgb[e][2]&0xf)); } }
    c[R_BPLCON3]=0;
}

void c1942_video_open(void){
    volatile uint16_t *c=CUSTOM;
    GfxBase=(struct GfxBase*)OpenLibrary((CONST_STRPTR)"graphics.library",0);
    if (GfxBase){ LoadView(0); WaitTOF(); WaitTOF(); }
    void *chunk=AllocMem((uint32_t)C_PLANE*C_NPLANES*2 + COPPER_WORDS*2, MEMF_CHIP|MEMF_CLEAR);
    if (!chunk) return;
    fb[0]=(uint8_t*)chunk; fb[1]=(uint8_t*)chunk+(uint32_t)C_PLANE*C_NPLANES;
    copper=(uint16_t*)((uint8_t*)chunk+(uint32_t)C_PLANE*C_NPLANES*2);
    c1942_planes=fb[1]; c1942_clear_hook=blit_clear;
    copper_point(fb[0]);
    { int s=0; while(s++<100000){ if((c[R_VPOSR]&0x1FF)>0x80) break; } }
    c[R_DMACON]=0x7FFF;
    c[R_DIWSTRT]=0x2C81; c[R_DIWSTOP]=0x2CC1;
    c[R_DDFSTRT]=0x0038; c[R_DDFSTOP]=0x00D0;
    c[R_BPLCON0]=(uint16_t)(0x0201 | (C_NPLANES << 12));
    c[R_BPLCON1]=0; c[R_BPLCON2]=0; c[R_BPLCON3]=0; c[R_BPL1MOD]=0; c[R_BPL2MOD]=0;
    upload_palette();
    { uint32_t a=(uint32_t)copper; c[R_COP1LCH]=(uint16_t)(a>>16); c[R_COP1LCH+1]=(uint16_t)(a&0xFFFF); }
    c[R_DMACON]=0x83C0;
    c[R_INTENA]=0x7FFF; c[R_INTREQ]=0x7FFF;
    Forbid();
}

static void waitvbl(void){
    /* Wait a full vblank EDGE (leave vblank, then re-enter) = one frame per call,
     * so a fast frame can't free-run multiple times within the vblank window
     * (60fps/vsync cap). Guarded so a host-paused chipset can't freeze us. */
    volatile uint32_t *vp=(volatile uint32_t*)0xdff004; unsigned long g=0;
    for(;;){ uint32_t r=*vp; uint32_t v=(((r>>16)&1)<<8)|((r>>8)&0xff);
             if(v<300)break;  if(++g>600000UL)break; }
    g=0;
    for(;;){ uint32_t r=*vp; uint32_t v=(((r>>16)&1)<<8)|((r>>8)&0xff);
             if(v>=300)break; if(++g>600000UL)break; }
}
void c1942_present(void){
    volatile uint16_t *c=CUSTOM;
    if(!fb[0])return; waitvbl();
    copper_point(c1942_planes);
    c1942_planes=(c1942_planes==fb[0])?fb[1]:fb[0];
    /* re-assert our display every frame: a window click can wake the OS and let
     * it reinstall its own copper / re-enable interrupts over ours. Re-point the
     * copper, re-enable bitplane+copper DMA, and re-clear interrupt enables. */
    { uint32_t a=(uint32_t)copper;
      c[R_COP1LCH]=(uint16_t)(a>>16); c[R_COP1LCH+1]=(uint16_t)(a&0xFFFF); }
    c[R_DMACON]=0x83C0;
    c[R_INTENA]=0x7FFF;
    /* re-load the palette EVERY frame: a click/OS-wake can reset the high colour
     * banks (-> black text / wrong sprite colours). 512 custom-reg writes is
     * negligible now the C2P no longer hammers chip RAM, and it matches pl_video
     * so both ports behave the same. */
    upload_palette();
}

/* Loading screen shown while the slow Z80 interpreter warms up the attract
 * (vram still empty -> a normal render would just be black). Dark-blue backdrop
 * + a grey block that slides across, so it's obviously working, not hung.
 * `phase` is a monotonically increasing frame counter. */
void c1942_loading(int phase)
{
    volatile uint16_t *c = CUSTOM;
    if (!fb[0]) return;
    uint8_t *pl = c1942_planes;
    blit_clear(pl);
    {
        int span = C_GH;                         /* 224 active px across */
        int bw   = 48;                           /* sliding block width  */
        int pos  = (phase * 3) % (span + bw) - bw;
        for (int ry = 118; ry < 138; ry++) {
            uint8_t *row = pl + (unsigned)ry * C_ROW;
            for (int i = 0; i < bw; i++) {
                int x = pos + i; if (x < 0 || x >= span) continue;
                int px = C_XOFF + x; uint8_t *b = row + (px >> 3); uint8_t m = 0x80 >> (px & 7);
                b[0] |= m; b[C_PLANE] |= m; b[C_PLANE*2] |= m;   /* pen 7 -> grey */
            }
        }
    }
    waitvbl();
    copper_point(pl);
    c1942_planes = (pl == fb[0]) ? fb[1] : fb[0];
    { uint32_t a = (uint32_t)copper;
      c[R_COP1LCH] = (uint16_t)(a >> 16); c[R_COP1LCH + 1] = (uint16_t)(a & 0xFFFF); }
    c[R_DMACON] = 0x83C0; c[R_INTENA] = 0x7FFF;
    upload_palette();
    c[R_BPLCON3] = 0; c[R_COLOR00] = 0x114;      /* dark-blue backdrop (after palette) */
}
