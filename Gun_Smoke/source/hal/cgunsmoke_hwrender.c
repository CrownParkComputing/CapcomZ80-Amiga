/* src/hal/cgunsmoke_hwrender.c -- native Amiga AGA renderer for Gun.Smoke (Capcom 1985).
 *
 * FAST PATH (replaces the first-light per-pixel C2P rebuild). Mirrors c1943_hwrender.c
 * (the same Capcom dual-Z80 family): a SINGLE hardware-scrolled 7-bitplane (128-colour
 * indirect) background playfield + blitter cookie-cut bobs for sprites and fg/HUD text.
 *
 *   - The bg is a STATIC ROM tilemap (gunsmoke_rom_bgmap: 2048 cols x 8 rows of 32x32
 *     4bpp tiles) scrolled by the 16-bit scrollx along the long display axis. It lives
 *     in a hardware-scrolled ring buffer driven by the COPPER (hwscroll_set); only the
 *     tile-columns NEWLY scrolled into view are redrawn each frame (~1 col/frame steady
 *     state), and each column is composed in a FAST-RAM pen row then written as bulk
 *     planar WORDS (no per-pixel chip RMW). Because the bgmap is immutable ROM, no
 *     per-column dirty-redraw is needed (unlike Terra Cresta's RAM tilemap); only a
 *     scrolly / bg-disable change forces a window refill.
 *   - Sprites (16x16 4bpp, pix0 transparent) are drawn by the BLITTER as cookie-cut bobs.
 *     Foreground chars are saved/restored like bobs but drawn as direct masked planar
 *     words; the attract title can contain ~300 glyphs, and 8 blits per glyph was too
 *     slow on target.
 *
 * BEFORE: ~57k 8-plane read-modify-write putpix/frame (rebuild native[][] every frame,
 *         then rotate-270 every visible pixel into chip RAM).
 * AFTER:  ~0-1 bg tile-columns redrawn/frame (bulk planar words) + the copper scroll +
 *         ~N blitter bobs (DMA, pre-decoded & cached gfx). Orders of magnitude cheaper.
 *
 * gfx decode + PROM indirect palette are IDENTICAL math to tools/cgunsmoke_shot.c
 * (MAME-verified): bg ind 0x00-0x3f, fg 0x40-0x4f. The 7-plane Amiga build keeps
 * bg/fg exact, reserves 0x50 as black, and compresses sprite pens into 0x51-0x7f.
 *
 * ROT270 cabinet mapping (host-validated): display(x,y) = native[arcadeY=x+16][arcadeX=255-y]
 *   - display VERTICAL axis (y) <-> arcade X (the scrollx scroll axis, reversed)
 *   - display HORIZONTAL axis (x) <-> arcade Y (= short axis, arcadeY = x+16)
 */
#include "z80emu.h"
#include "hwscroll.h"
#include <exec/exec.h>
#include <proto/exec.h>
#include <string.h>

#define DISPW   224              /* display width  = arcade Y visible (after ROT270)   */
#define DISPH   256              /* display height = arcade X window (after ROT270)     */
#define VIS_X0  16               /* crop arcade-Y border (0..15) off the screen edge    */
#define RING    512              /* logical scroll-axis ring (px) = 16 tiles of 32      */
#define BUFW    256              /* short axis: 224 display + bob margin (16-px aligned) */
#define BUFH    768              /* RING + DISPH: top 256 rows mirrored at 512..767      */
#define NPLANES 7                /* 128-colour AGA playfield: saves one bitplane fetch  */
#define NCOLORS (1 << NPLANES)
#define WPR     (BUFW/16)        /* words per bitplane row (16)                          */
#define DISPWORDS (DISPW/16)     /* plane-words covering the display (14)                */
#define BLACK_PEN 0x50           /* unused indirect index -> forced black (bg off)       */
#define SPR_PEN_BASE 0x51        /* compressed sprite palette lives in 0x51..0x7f        */
#define SPR_PEN_COUNT (NCOLORS - SPR_PEN_BASE)
#define NTILE   512              /* bg tile codes (262144-byte tile ROM / 512B per tile) */
#define NSPR    2048             /* sprite codes (262144-byte sprite ROM / 128B per tile)*/
#define NCHAR   1024             /* fg char codes (16384-byte char ROM / 16B per char)   */

/* ---- tunables ---------------------------------------------------------------- */
#ifndef HWIN_ADJ
#define HWIN_ADJ (0)             /* horizontal window nudge (lores px). 0 = the canonical
                                  * mathematically-centred window (hstart=0x81+(320-224)/2),
                                  * IDENTICAL to 1943 / Terra Cresta (which leave hwin_adj=0
                                  * and read centred). The prior -8 over-corrected and shifted
                                  * the playfield. EVEN keeps hstart odd so DDFSTRT is exact. */
#endif
/* Credits card (Fix 1): the warning screen is replaced by a typewriter-revealed card.
 * CARD_CHAR_FRAMES = host frames between revealing each successive glyph CELL (incl
 * spaces). 2 => ~30 cells/s, so the ~152-cell card finishes drawing around frame 300.
 * Raise it to slow the typing, lower it to speed up. */
#ifndef CARD_CHAR_FRAMES
#define CARD_CHAR_FRAMES 2
#endif
/* Dense attract/title/wanted screens can carry hundreds of foreground glyphs. Draw
 * them correctly, but hold every other rendered frame so the Z80/audio keep time. */
#ifndef GS_DENSE_FG_SKIP_THRESHOLD
#define GS_DENSE_FG_SKIP_THRESHOLD 120
#endif
/* Gun.Smoke's useful scroll in this rotated renderer is the long axis. Treat the
 * short-axis register as fixed so incidental changes do not force a full ring refill. */
#ifndef GS_LOCK_SHORT_SCROLL
#define GS_LOCK_SHORT_SCROLL 1
#endif

extern const unsigned char gunsmoke_rom_chars[], gunsmoke_rom_tiles[],
                           gunsmoke_rom_sprites[], gunsmoke_rom_bgmap[],
                           gunsmoke_rom_proms[];
extern int gunsmoke_scrollx(void), gunsmoke_scrolly(void);
extern int gunsmoke_chon(void), gunsmoke_bgon(void), gunsmoke_objon(void), gunsmoke_sprite3bank(void);
extern volatile int hwscroll_no_preroll;
extern volatile int hwscroll_hwin_adj;

static const unsigned char *chars, *tiles, *sprites, *bgmap, *proms;
static hwscroll_t S;

/* Static score/header band. Gun.Smoke's fg tile columns 29..31 map to display
 * rows 16..0 after ROT270, so keep them in a dedicated top HUD buffer and let
 * the scrolling background start below row 24. */
#define HUDH         DISPH
#define HUD_PLAY_TOP 24
static uint8_t *hud_bpl;
static long     hud_plane_sz;
static int      s_hud_ok;
static unsigned hud_last_hash;
static const uint8_t hud_top_cols[3] = {29,30,31};
static int dense_fg_phase;

/* ---- gfx pixel decoders (identical math to tools/cgunsmoke_shot.c) ---- */
static int gbit(const unsigned char *p, unsigned o){ return (p[o>>3] >> (7-(o&7))) & 1; }
/* 8x8 2bpp char: planes {4,0}, x{11,10,9,8,3,2,1,0}, y{112,96,80,64,48,32,16,0} */
static int char_pix(int code,int x,int y){
    static const int xo[8]={11,10,9,8,3,2,1,0}, yo[8]={112,96,80,64,48,32,16,0};
    unsigned o=(unsigned)code*128+yo[y]+xo[x];
    return (gbit(chars,o+4)<<1)|gbit(chars,o);
}
/* 32x32 4bpp bg tile: planes {H+4,H,4,0}, H=0x100000 bits */
static const int txo[32]={0,1,2,3,8,9,10,11, 512,513,514,515,520,521,522,523,
                          1024,1025,1026,1027,1032,1033,1034,1035, 1536,1537,1538,1539,1544,1545,1546,1547};
static int tile_pix(int code,int x,int y){
    const unsigned H=0x100000;
    unsigned o=(unsigned)code*2048 + (unsigned)(y*16) + txo[x];
    return (gbit(tiles,o+H+4)<<3)|(gbit(tiles,o+H)<<2)|(gbit(tiles,o+4)<<1)|gbit(tiles,o);
}
/* 16x16 4bpp sprite: planes {H+4,H,4,0}, H=0x100000 bits */
static int spr_pix(int code,int x,int y){
    static const int sxo[16]={0,1,2,3,8,9,10,11, 256,257,258,259,264,265,266,267};
    const unsigned H=0x100000;
    unsigned o=(unsigned)code*512 + (unsigned)(y*16) + sxo[x];
    return (gbit(sprites,o+H+4)<<3)|(gbit(sprites,o+H)<<2)|(gbit(sprites,o+4)<<1)|gbit(sprites,o);
}
static int w4(int v){ v&=0xf; return (v<<4)|v; }   /* pal4bit -> 8-bit channel */

/* ---- pre-decoded gfx caches (pen 0..15 per pixel) in FAST RAM: a byte read instead
 * of per-pixel bit extraction. Decoded ONCE at open. Plus the small PROM indirect
 * colour LUTs so the per-pixel pen resolve is a 2D array read, not a PROM index. ---- */
static uint8_t *dtile, *dspr, *dchar;            /* NULL => on-the-fly decode fallback */
static uint8_t spr_blank[NSPR], char_blank[NCHAR];
static uint8_t bg_ctab[16][16];                  /* [color][pix] -> bg pen   0x00-0x3f */
static uint8_t spr_ctab[16][16];                 /* [color][pix] -> remapped sprite pen */
static uint8_t fg_lut[32][4];                    /* [color][pix] -> fg pen 0x40-0x4f / 0xff=transparent */
static uint8_t pal256[256*3];

static uint8_t spr_remap_pen(int original)
{
    return (uint8_t)(SPR_PEN_BASE + ((original & 0x7f) % SPR_PEN_COUNT));
}

static void *fast_alloc(unsigned long n){
    void *p = AllocMem(n, MEMF_FAST | MEMF_CLEAR);
    if (!p) p = AllocMem(n, MEMF_ANY | MEMF_CLEAR);
    return p;
}
static void decode_gfx(void){
    dtile = (uint8_t*)fast_alloc((unsigned long)NTILE*1024);
    dspr  = (uint8_t*)fast_alloc((unsigned long)NSPR*256);
    dchar = (uint8_t*)fast_alloc((unsigned long)NCHAR*64);
    if (dtile) for (int c=0;c<NTILE;c++) for (int y=0;y<32;y++) for (int x=0;x<32;x++)
        dtile[c*1024+y*32+x]=(uint8_t)tile_pix(c,x,y);
    if (dspr)  for (int c=0;c<NSPR;c++)  for (int y=0;y<16;y++) for (int x=0;x<16;x++)
        dspr[c*256+y*16+x]=(uint8_t)spr_pix(c,x,y);
    for (int c=0;c<NCHAR;c++){ int any=0;
        for (int y=0;y<8;y++) for (int x=0;x<8;x++){ int p=char_pix(c,x,y);
            if (dchar) dchar[c*64+y*8+x]=(uint8_t)p; if (p) any=1; }
        char_blank[c]=(uint8_t)!any; }
    for (int c=0;c<NSPR;c++){ int any=0;
        for (int i=0;i<256 && !any;i++) if (dspr ? dspr[c*256+i] : spr_pix(c,i&15,i>>4)) any=1;
        spr_blank[c]=(uint8_t)!any; }
}
static void build_ctabs(void){
    for (int color=0;color<16;color++) for (int pix=0;pix<16;pix++){
        bg_ctab[color][pix]=(uint8_t)((proms[0x400+color*16+pix]&0xf)|((proms[0x500+color*16+pix]&3)<<4));
        int original = 0x80 | (proms[0x600+color*16+pix]&0xf) | ((proms[0x700+color*16+pix]&7)<<4);
        spr_ctab[color][pix]=spr_remap_pen(original);
    }
    for (int color=0;color<32;color++) for (int pix=0;pix<4;pix++){
        int lut=proms[0x300+color*4+pix]&0xf;
        fg_lut[color][pix]=(uint8_t)((lut==0x0f)?0xff:(0x40|lut));
    }
}
static inline int TILE(int code,int x,int y){ return dtile?dtile[(code&(NTILE-1))*1024+y*32+x]:tile_pix(code,x,y); }
static inline int SPR (int code,int x,int y){ return dspr ?dspr [(code&(NSPR-1))*256 +y*16+x]:spr_pix (code,x,y); }
static inline int CHAR(int code,int x,int y){ return dchar?dchar[(code&(NCHAR-1))*64 +y*8 +x]:char_pix(code,x,y); }

/* ---- background hardware-scroll ring ---- */
static int s_open;
static int cont, last_raw;          /* monotonic accumulated scrollx (scroll axis) */
static int cur_sy, cur_bgdis;       /* this frame's short-axis scroll + bg-disable  */
static uint8_t *buf[2];             /* buf[0]=engine's, buf[1]=ours (double buffer)  */
static int curdisp, s_db;
static int bhead[2];                /* per-buffer forward fill cursor (tile column)  */
static int buf_cfg[2];              /* per-buffer (sy<<1)|bgdisable the ring holds    */

static inline uint16_t *planebase(int p){ return (uint16_t*)(S.bpl[0] + (long)p*S.plane_sz); }

/* write one composed DISPW-wide pen row into the AGA planes of buffer row R as bulk
 * planar WORDS (no per-pixel chip RMW). penrow is fast RAM; only 14 words/plane hit
 * chip -- ~16x fewer chip accesses than the old per-pixel hwscroll_putpix path. */
static inline void put_planar_row(int R, const uint8_t *penrow){
    uint8_t *base = S.bpl[0] + (long)R*S.stride;
    for (int p=0;p<NPLANES;p++){
        uint16_t *dst=(uint16_t*)(base+(long)p*S.plane_sz);
        const uint8_t *pr=penrow;
        for (int wd=0;wd<DISPWORDS;wd++){
            unsigned v=0;
            for (int i=0;i<16;i++) v|=(unsigned)((pr[i]>>p)&1)<<(15-i);
            dst[wd]=(uint16_t)v; pr+=16;
        }
    }
}
/* draw one 32px bg tile-column at scroll-axis world position wx0 into the ring,
 * mirroring the top 256 rows. The ring runs backward so increasing scroll moves
 * content top->bottom (matches the ROT270 mapping arcadeX = 255 - display_y). */
static void draw_col(int wx0){
    uint8_t penrow[DISPW];
    for (int dy=0; dy<32; dy++){
        int wx=wx0+dy, by=(-(wx0+dy))&(RING-1);
        int col=(wx>>5)&2047, xin=wx&31;
        if (cur_bgdis){
            for (int x=0;x<DISPW;x++) penrow[x]=BLACK_PEN;
        } else {
            int prevrow=-1, code=0, color=0, fxflip=0, fyflip=0;
            for (int x=0;x<DISPW;x++){
                int wy=(x+VIS_X0+cur_sy)&0xff;
                int row=wy>>5, yin=wy&31;
                if (row!=prevrow){                  /* re-decode tile only per 32px row */
                    int off=(col*8+row)*2;
                    int attr=bgmap[off+1];
                    code=bgmap[off]+((attr&1)<<8);
                    color=(attr&0x3c)>>2;
                    fxflip=(attr&0x40)?1:0; fyflip=(attr&0x80)?1:0;
                    prevrow=row;
                }
                int fx=fxflip?31-xin:xin, fy=fyflip?31-yin:yin;
                penrow[x]=bg_ctab[color][TILE(code,fx,fy)];
            }
        }
        put_planar_row(by, penrow);
        if (by<DISPH) put_planar_row(by+RING, penrow);
    }
}
/* draw tile-columns newly scrolled into the window (forward edge), ~1 col/frame. */
static void fill_ahead(int *head){
    int wcol=cont>>5, lo=wcol-1; if (lo<0) lo=0;
    if (*head<lo || *head>wcol+(RING>>5)) *head=lo;       /* resync on big jump */
    int need=wcol+(DISPH>>5)+2;
    while (*head<need){ draw_col((*head)<<5); (*head)++; }
}
/* full window refill (open + whenever the short-axis scrolly / bg-disable flag
 * changes, which invalidates every already-drawn column). */
static void refill_full(int b){
    S.bpl[0]=buf[b];
    int wcol=cont>>5, lo=wcol-1; if (lo<0) lo=0;
    int need=wcol+(DISPH>>5)+2;
    for (int c=lo;c<need;c++) draw_col(c<<5);
    bhead[b]=need;
}
static void advance(int raw){
    raw&=0xffff;
    int d=raw-last_raw;
    if (d<-0x8000) d+=0x10000; else if (d>0x8000) d-=0x10000;
    cont+=d; last_raw=raw;
    if (cont<0) cont+=0x10000;
}

/* ---- blitter bobs (sprites + fg text), 128-colour, any count ---- */
#define CUSTOMB ((volatile uint16_t *)0xdff000)
#define RB_DMACONR (0x002/2)
#define RB_BLTCON0 (0x040/2)
#define RB_BLTCON1 (0x042/2)
#define RB_BLTAFWM (0x044/2)
#define RB_BLTALWM (0x046/2)
#define RB_BLTCPT  (0x048/2)
#define RB_BLTBPT  (0x04C/2)
#define RB_BLTAPT  (0x050/2)
#define RB_BLTDPT  (0x054/2)
#define RB_BLTSIZE (0x058/2)
#define RB_BLTCMOD (0x060/2)
#define RB_BLTBMOD (0x062/2)
#define RB_BLTAMOD (0x064/2)
#define RB_BLTDMOD (0x066/2)
static inline void blit_wait(void){ volatile uint16_t *c=CUSTOMB;
    (void)c[RB_DMACONR]; while (c[RB_DMACONR] & 0x4000); }   /* BLTBUSY */

#define MAXBOB 320
#define KIND_SPR 0
#define KIND_CHR 1
typedef struct { int bx, by, code, col, flip, kind; uint16_t save[16*2*8]; } bob_t;
static bob_t bobs[2][MAXBOB]; static int nbob[2];   /* per display buffer */
static uint16_t *bscratch;                          /* chip: per-frame bob source fallback */

/* pre-built bob-image cache (chip RAM, blitter reads it): each unique (kind,code,col,
 * flip) sprite/char is packed to its 288-word blit image ([0..31]=mask, [32+p*32]=plane
 * p) ONCE and reused -- per-frame cost is just the blits, not a per-pixel rebuild. */
#define BOBIMG_WORDS (32 + NPLANES * 32)
#define NBOBCACHE 512
static uint16_t *bobpool;
static int bobkey[NBOBCACHE];

static inline int bob_h(bob_t *b){ return b->kind==KIND_CHR ? 8 : 16; }
/* a bob's final pen (0..255) at local (col c short axis, row r scroll axis); -1=transparent.
 * The local->source rotation matches the ROT270 bg mapping:
 *   sprites (16x16): src px = 15-r, py = flipy ? 15-c : c   (Gun.Smoke sprites flip Y only)
 *   chars   (8x8):   src cx = 7-r,  cy = c. */
static int bob_pen(bob_t *b, int c, int r){
    if (b->kind==KIND_CHR){
        int cx=7-r, cy=c;
        if ((unsigned)cx>=8 || (unsigned)cy>=8) return -1;
        int v=fg_lut[b->col & 31][CHAR(b->code,cx,cy)];
        return (v==0xff) ? -1 : v;
    } else {
        int px=15-r, py=(b->flip&1)?15-c:c;
        if ((unsigned)px>=16 || (unsigned)py>=16) return -1;
        int pix=SPR(b->code,px,py);
        if (!pix) return -1;
        return spr_ctab[b->col & 0xf][pix];
    }
}
/* restore the clean backgrounds saved under last frame's bobs (all saves captured CLEAN
 * bg before any draw, so restore order is irrelevant; overlapping bobs leave no trails). */
static void restore_bobs(int bi){
    for (int i=0;i<nbob[bi];i++){
        bob_t *b=&bobs[bi][i]; int w=b->bx>>4, h=bob_h(b);
        int w0=(w>=0&&w<WPR), w1=(w+1>=0&&w+1<WPR);
        for (int r=0;r<h;r++){ int R=b->by+r; if((unsigned)R>=BUFH) continue;
            uint16_t *s=&b->save[r*16];
            for (int p=0;p<NPLANES;p++){ uint16_t *pp=planebase(p)+R*WPR+w;
                if(w0) pp[0]=s[p]; if(w1) pp[1]=s[8+p]; } }
    }
}
static void save_bob(bob_t *b){
    int w=b->bx>>4, h=bob_h(b), w0=(w>=0&&w<WPR), w1=(w+1>=0&&w+1<WPR);
    for (int r=0;r<h;r++){ int R=b->by+r; uint16_t *sv=&b->save[r*16];
        if ((unsigned)R>=BUFH) continue;
        for (int p=0;p<NPLANES;p++){ uint16_t *pp=planebase(p)+R*WPR+w;
            sv[p]=w0?pp[0]:0; sv[8+p]=w1?pp[1]:0; } }
}
/* draw bob via the BLITTER cookie-cut (DMA): build (or fetch cached) the 16px image
 * (mask + NPLANES planes, 2 words/row), then one cookie-cut blit per plane (minterm 0xCA:
 * D=(A&B)|(~A&C), A=mask, B=plane data, C/D=playfield) shifted by bx&15. */
static void blit_bob(bob_t *b){
    int w=b->bx>>4, sh=b->bx&15, h=bob_h(b);
    if (w<0 || w+1>=WPR || b->by<0 || b->by+h>BUFH) return;

    uint16_t *img = bscratch;
    int build = 1;
    if (bobpool){
        int key=(b->kind<<24)|((b->col&0x1f)<<14)|((b->flip&1)<<13)|(b->code&0x1fff);
        int slot=(key ^ (key>>9) ^ (key>>17)) & (NBOBCACHE-1);
        img=bobpool + (long)slot*BOBIMG_WORDS;
        build=(bobkey[slot]!=key);
        if (build) bobkey[slot]=key;
    }
    if (build){
        for (int r=0;r<h;r++){
            uint16_t mask=0, pat[8]={0,0,0,0,0,0,0,0};
            for (int c=0;c<16;c++){ int pen=bob_pen(b,c,r); if(pen<0) continue;
                uint16_t m=(uint16_t)(0x8000u>>c); mask|=m;
                for (int p=0;p<NPLANES;p++) if(pen&(1<<p)) pat[p]|=m; }
            img[r*2]=mask; img[r*2+1]=0;
            for (int p=0;p<NPLANES;p++){ img[32+p*32+r*2]=pat[p]; img[32+p*32+r*2+1]=0; }
        }
    }
    volatile uint16_t *c=CUSTOMB;
    blit_wait();
    c[RB_BLTCON0]=(uint16_t)((sh<<12)|0x0FCA);
    c[RB_BLTCON1]=(uint16_t)(sh<<12);
    c[RB_BLTAFWM]=0xFFFF; c[RB_BLTALWM]=0xFFFF;
    c[RB_BLTAMOD]=0; c[RB_BLTBMOD]=0;
    c[RB_BLTCMOD]=(uint16_t)(BUFW/8-4); c[RB_BLTDMOD]=(uint16_t)(BUFW/8-4);
    for (int p=0;p<NPLANES;p++){
        uint32_t a=(uint32_t)img, bd=(uint32_t)(img+32+p*32);
        uint32_t d=(uint32_t)(planebase(p)+b->by*WPR+w);
        blit_wait();
        c[RB_BLTAPT]=(uint16_t)(a>>16); c[RB_BLTAPT+1]=(uint16_t)a;
        c[RB_BLTBPT]=(uint16_t)(bd>>16); c[RB_BLTBPT+1]=(uint16_t)bd;
        c[RB_BLTCPT]=(uint16_t)(d>>16); c[RB_BLTCPT+1]=(uint16_t)d;
        c[RB_BLTDPT]=(uint16_t)(d>>16); c[RB_BLTDPT+1]=(uint16_t)d;
        c[RB_BLTSIZE]=(uint16_t)((h<<6)|2);
    }
    blit_wait();
}
/* ---- fg-glyph planar-row CACHE -------------------------------------------------
 * draw_char_direct ran bob_pen() per PIXEL per GLYPH every frame -- with ~hundreds of
 * fg glyphs on the text/poster screens that per-pixel decode dominated the frame. The
 * glyph bitmap for a given (code,col) is constant, so decode each glyph's 8 rows ONCE
 * into unshifted planar bytes (bit7 = leftmost pixel c=0) and reuse. At draw time a
 * single 32-bit shift per row places the 8px glyph into the two destination words --
 * no per-pixel work. Visually identical to the old path (same pens, same mask). ----- */
#define NCHARCACHE 512
static int     charkey[NCHARCACHE];
static uint8_t charmask[NCHARCACHE][8];
static uint8_t charpat [NCHARCACHE][8][NPLANES];
static int char_cache_get(int code, int col){
    int key = ((code & (NCHAR-1)) << 5) | (col & 31);
    int slot = (key ^ (key>>7) ^ (key>>13)) & (NCHARCACHE-1);
    if (charkey[slot] == key) return slot;
    charkey[slot] = key;
    for (int r=0;r<8;r++){
        uint8_t m=0, pat[NPLANES]; for (int p=0;p<NPLANES;p++) pat[p]=0;
        int cx=7-r;
        for (int c=0;c<8;c++){
            int v=fg_lut[col & 31][CHAR(code,cx,c)];   /* cy=c */
            if (v==0xff) continue;                      /* transparent */
            uint8_t bit=(uint8_t)(0x80>>c);             /* c=0 -> bit7 (leftmost) */
            m|=bit;
            for (int p=0;p<NPLANES;p++) if (v&(1<<p)) pat[p]|=bit;
        }
        charmask[slot][r]=m; for (int p=0;p<NPLANES;p++) charpat[slot][r][p]=pat[p];
    }
    return slot;
}
static void init_char_cache(void){ for (int i=0;i<NCHARCACHE;i++) charkey[i]=-1; }

static void draw_char_direct(bob_t *b){
    if (b->kind != KIND_CHR) return;
    int w=b->bx>>4, sh=b->bx&15;
    if (w<0 || w+1>=WPR || b->by<0 || b->by+8>BUFH) return;
    int slot = char_cache_get(b->code, b->col);
    int s = 24 - sh;                                    /* c=0 lands at 32-bit bit 31-sh */
    for (int r=0;r<8;r++){
        uint8_t cm=charmask[slot][r];
        if (!cm) continue;                              /* fully transparent row */
        uint32_t mm=(uint32_t)cm << s;
        uint16_t mask0=(uint16_t)(mm>>16), mask1=(uint16_t)mm;
        int R=b->by+r;
        for (int p=0;p<NPLANES;p++){
            uint32_t pp=(uint32_t)charpat[slot][r][p] << s;
            uint16_t *q=planebase(p)+R*WPR+w;
            q[0]=(uint16_t)((q[0]&~mask0)|(uint16_t)(pp>>16));
            if (mask1) q[1]=(uint16_t)((q[1]&~mask1)|(uint16_t)pp);
        }
    }
}

static void hud_setpix(int x, int y, uint8_t pen)
{
    if ((unsigned)x >= (unsigned)BUFW || (unsigned)y >= (unsigned)HUDH) return;
    uint8_t *row = hud_bpl + (long)y*S.stride + (x >> 3);
    uint8_t m = (uint8_t)(0x80 >> (x & 7));
    for (int p=0; p<NPLANES; p++){
        uint8_t *q = row + (long)p*hud_plane_sz;
        if (pen & (1 << p)) *q |= m; else *q &= (uint8_t)~m;
    }
}

static void hud_clear_rows(int y0, int y1)
{
    if (!hud_bpl) return;
    if (y0 < 0) y0 = 0;
    if (y1 > HUDH) y1 = HUDH;
    for (int y=y0; y<y1; y++){
        uint8_t *row = hud_bpl + (long)y*S.stride;
        for (int p=0; p<NPLANES; p++) memset(row + (long)p*hud_plane_sz, 0, S.stride);
    }
}

static void hud_draw_glyph(const unsigned char *mem, int crow, int ccol)
{
    int idx = crow*32 + ccol;
    int attr = mem[0xd400+idx];
    int code = mem[0xd000+idx] + ((attr & 0xe0) << 2);
    if (char_blank[code & (NCHAR-1)]) return;
    int bx = crow*8 - VIS_X0;
    int by = 248 - ccol*8;
    for (int r=0; r<8; r++) for (int c=0; c<8; c++){
        int cx = 7-r, cy = c;
        int v = fg_lut[attr & 31][CHAR(code,cx,cy)];
        if (v != 0xff) hud_setpix(bx+c, by+r, (uint8_t)v);
    }
}

static void draw_hud(MY_LITTLE_Z80 *z)
{
    const unsigned char *mem = z->memory;
    hud_clear_rows(0, HUD_PLAY_TOP);
    if (!gunsmoke_chon()) return;
    for (int i=0; i<3; i++)
        for (int crow=2; crow<30; crow++)
            hud_draw_glyph(mem, crow, hud_top_cols[i]);
}

static unsigned hud_hash(MY_LITTLE_Z80 *z)
{
    const unsigned char *mem = z->memory;
    unsigned h = 2166136261u ^ (unsigned)gunsmoke_chon();
    for (int i=0; i<3; i++){
        int ccol = hud_top_cols[i];
        for (int crow=2; crow<30; crow++){
            int idx = crow*32 + ccol;
            h = (h ^ mem[0xd000+idx]) * 16777619u;
            h = (h ^ mem[0xd400+idx]) * 16777619u;
        }
    }
    return h ? h : 1;
}

static int fg_visible_count(MY_LITTLE_Z80 *z, int ccol_hi)
{
    const unsigned char *mem = z->memory;
    int n = 0;
    if (!gunsmoke_chon()) return 0;
    for (int crow=2; crow<30; crow++) for (int ccol=0; ccol<ccol_hi; ccol++) {
        int idx = crow*32 + ccol;
        int attr = mem[0xd400+idx];
        int code = mem[0xd000+idx] + ((attr & 0xe0) << 2);
        if (!char_blank[code & (NCHAR-1)]) n++;
    }
    return n;
}

/* ===================== AGA HARDWARE SPRITES (attached-pair engine) =============
 * Route the game's 16x16 4bpp sprites to the AGA wide attached-pair hw-sprite engine
 * (hwscroll_aspr_*, the same one Commando/Terra Cresta use) INSTEAD of blitter bobs:
 * hardware composites them over the playfield for free (no blit, no bg save/restore),
 * which both speeds up sprite-heavy frames and removes bob trails/flicker. A sprite
 * that the engine refuses (4 pairs full at that scanline, a palette conflict on a
 * shared scanline, or below the copper's 255-line palette-reload band) FALLS BACK to a
 * bob -- exactly the JOTD "hw overflow degrades, never drops" rule. Build -DGS_NOHWSPR
 * to force the old all-bobs path. ----------------------------------------------------*/
#ifndef GS_NOHWSPR
#define GS_HWSPR 1
#endif
#ifndef GS_HWSPR_XADJ
#define GS_HWSPR_XADJ 0          /* display-X nudge for hw sprites (lores px) -- tune on device */
#endif
#ifndef GS_HWSPR_YADJ
#define GS_HWSPR_YADJ 0          /* display-Y nudge for hw sprites (lines)    -- tune on device */
#endif
extern volatile int hwscroll_vcrop_top;

/* per colour-bank (attr&0x0f) 15-colour hw-sprite palette: pen p (1..15) = the PROM-
 * resolved sprite colour for pixel value p. spr_pal15[color][p-1] is rgb12. */
static uint16_t spr_pal15[16][15];
static int s_hwspr_ok;
static void build_spr_pal15(void){
    for (int color=0; color<16; color++) for (int p=1; p<16; p++){
        int idx = 0x80 | (proms[0x600+color*16+p]&0xf) | ((proms[0x700+color*16+p]&7)<<4);
        spr_pal15[color][p-1] = (uint16_t)(((proms[0x000+idx]&0xf)<<8)
                                         | ((proms[0x100+idx]&0xf)<<4)
                                         |  (proms[0x200+idx]&0xf));
    }
}
#ifndef HWS_SPR_W
#define HWS_SPR_W 16
#endif
#define ASPR_WORDS     (HWS_SPR_W/16)
#define ASPR_IMG_WORDS (16*4*ASPR_WORDS)

typedef struct { short code; unsigned char flipy, present; } aspr_col_t;

/* Pack one possibly-wide attached-pair sprite into the engine img4 layout:
 * 16 lines, each 4 planes x ASPR_WORDS words, plane-major, MSB-left. Gun.Smoke
 * sprite pen 0 is transparent; pens 1..15 map directly to COLOR17..31. */
static uint16_t s_asprimg[ASPR_IMG_WORDS];
static void build_aspr_img4(const aspr_col_t *cols){
    for (int line=0; line<16; line++){
        int px=15-line;
        for (int wc=0; wc<ASPR_WORDS; wc++){
            uint16_t w0=0,w1=0,w2=0,w3=0;
            if (cols[wc].present){
                int code=cols[wc].code, flipy=cols[wc].flipy;
                for (int c=0; c<16; c++){
                    int py=flipy?15-c:c;
                    int pix=SPR(code,px,py);
                    if (!pix) continue;
                    uint16_t bit=(uint16_t)(0x8000>>c);
                    if (pix&1) w0|=bit; if (pix&2) w1|=bit; if (pix&4) w2|=bit; if (pix&8) w3|=bit;
                }
            }
            int b=line*4*ASPR_WORDS;
            s_asprimg[b+0*ASPR_WORDS+wc]=w0; s_asprimg[b+1*ASPR_WORDS+wc]=w1;
            s_asprimg[b+2*ASPR_WORDS+wc]=w2; s_asprimg[b+3*ASPR_WORDS+wc]=w3;
        }
    }
}

#define MAXSPR_CAND 128
typedef struct {
    short code, bx, by, dispx, dispy;
    unsigned char color, flip, hw, used;
} sprcand_t;

/* collect every on-screen sprite + fg char, SAVE all clean bg, then DRAW all (so
 * overlaps leave no trails and later bobs sit on top). V positions the window. */
static void draw_sprites(MY_LITTLE_Z80 *z, int V, int bi){
    const unsigned char *mem = z->memory;
    int objon=gunsmoke_objon(), chon=gunsmoke_chon(), s3=gunsmoke_sprite3bank();
    bob_t *bb=bobs[bi]; int n=0;
    sprcand_t sc[MAXSPR_CAND]; int ns=0;

    /* sprites: spriteram 0xf000-0xffff, 32 bytes/entry. High-to-low offset so the
     * lowest entry is appended LAST and draws on top (matches the arcade overwrite). */
    if (objon) for (int offs=0x1000-32; offs>=0 && ns<MAXSPR_CAND; offs-=32){
        unsigned a=0xf000+offs;
        int attr=mem[a+1];
        int bank=(attr&0xc0)>>6;
        int code=mem[a];
        int color=attr&0x0f;
        int flipy=(attr&0x10)?1:0;
        int sx0=mem[a+3]-((attr&0x20)<<3);    /* arcade X (scroll axis), 9th bit = -256 */
        int sy0=mem[a+2];                       /* arcade Y (short axis)                 */
        if (bank==3) bank+=s3;
        code+=256*bank;
        if (sx0<=-16 || sx0>=256 || sy0<=0 || sy0>=240) continue;   /* off-screen */
        if (spr_blank[code & (NSPR-1)]) continue;                    /* ROM blank tile  */
        sprcand_t *c=&sc[ns++];
        c->code=(short)code; c->color=(unsigned char)color; c->flip=(unsigned char)flipy;
        c->bx=(short)(sy0-VIS_X0); c->by=(short)(V+(240-sx0));
        c->dispx=(short)(sy0 - VIS_X0 + GS_HWSPR_XADJ);
        c->dispy=(short)(240 - sx0 - hwscroll_vcrop_top + GS_HWSPR_YADJ);
        c->hw=0; c->used=0;
    }
#ifdef GS_HWSPR
    if (s_hwspr_ok && ns>0){
        unsigned char ord[MAXSPR_CAND];
        for (int i=0; i<ns; i++) ord[i]=(unsigned char)i;
        for (int a=1; a<ns; a++){
            unsigned char t=ord[a]; int b=a-1;
            while (b>=0 && (sc[ord[b]].dispy > sc[t].dispy ||
                   (sc[ord[b]].dispy == sc[t].dispy && sc[ord[b]].dispx > sc[t].dispx))){
                ord[b+1]=ord[b]; b--;
            }
            ord[b+1]=t;
        }
        for (int oi=0; oi<ns; oi++){
            int i=ord[oi]; if (sc[i].used) continue;
            aspr_col_t cols[ASPR_WORDS];
            unsigned char unit[ASPR_WORDS]; int nu=1;
            for (int k=0; k<ASPR_WORDS; k++){ cols[k].present=0; unit[k]=0; }
            cols[0].code=sc[i].code; cols[0].flipy=sc[i].flip; cols[0].present=1;
            unit[0]=(unsigned char)i; sc[i].used=1;
            for (int k=1; k<ASPR_WORDS; k++){
                int wantx=sc[i].dispx + 16*k, found=-1;
                for (int oj=oi+1; oj<ns; oj++){
                    int j=ord[oj]; if (sc[j].used) continue;
                    if (sc[j].dispy != sc[i].dispy) {
                        if (sc[j].dispy > sc[i].dispy) break;
                        continue;
                    }
                    if (sc[j].color == sc[i].color && sc[j].dispx == wantx){ found=j; break; }
                }
                if (found<0) break;
                sc[found].used=1;
                cols[nu].code=sc[found].code; cols[nu].flipy=sc[found].flip; cols[nu].present=1;
                unit[nu++]=(unsigned char)found;
            }
            build_aspr_img4(cols);
            if (hwscroll_aspr_add(&S, sc[i].dispx, sc[i].dispy, s_asprimg,
                                  sc[i].color, spr_pal15[sc[i].color])) {
                for (int k=0; k<nu; k++) sc[unit[k]].hw=1;
            }
        }
    }
#endif
    for (int i=0; i<ns && n<MAXBOB; i++){
        if (sc[i].hw) continue;
        bob_t *b=&bb[n++];
        b->bx=sc[i].bx; b->by=sc[i].by; b->code=sc[i].code; b->col=sc[i].color; b->flip=sc[i].flip; b->kind=KIND_SPR;
    }
    /* fg chars: 32x32 cells of 8x8 (transparent LUT 0xf), on top of sprites. cell idx =
     * crow*32+ccol with crow = arcadeY tile (short), ccol = arcadeX tile (scroll). */
    int ccol_hi = s_hud_ok ? 29 : 32;
    if (chon) for (int crow=2; crow<30 && n<MAXBOB; crow++) for (int ccol=0; ccol<ccol_hi && n<MAXBOB; ccol++){
        int idx=crow*32+ccol;
        int attr=mem[0xd400+idx];
        int code=mem[0xd000+idx]+((attr&0xe0)<<2);
        if (char_blank[code & (NCHAR-1)]) continue;                  /* blank glyph */
        bob_t *b=&bb[n++];
        b->bx=crow*8-VIS_X0; b->by=V+248-ccol*8; b->code=code; b->col=attr&0x1f; b->flip=0; b->kind=KIND_CHR;
    }
    nbob[bi]=n;
    for (int i=0;i<n;i++) save_bob(&bb[i]);    /* all saves capture clean bg first */
    for (int i=0;i<n;i++) if (bb[i].kind==KIND_SPR) blit_bob(&bb[i]);
    for (int i=0;i<n;i++) if (bb[i].kind==KIND_CHR) draw_char_direct(&bb[i]);
}

/* ====================== CREDITS CARD (Fix 1) =================================
 * Replace Capcom's WARNING screen (the boot phase where objon==0) with a credits
 * card drawn ONE GLYPH CELL AT A TIME, in reading order (left->right per line, then
 * the next line) -- a typewriter/plot effect. We do NOT touch the brown scrolling
 * desert background the warning rides on; we only suppress the game's own warning
 * text (by not drawing the game fg-char layer this phase) and plot our own glyphs as
 * blitter bobs, exactly like the game's HUD chars, at a fixed display position that
 * tracks the bg scroll. Once the title begins (objon flips to 1) the card is retired
 * for good and normal rendering resumes.
 *
 * Glyph codes use the game's own font (VERIFIED against CAPCOM 1985 / RANKING TABLE
 * in videoram): digit '0'-'9' -> 0x00-0x09, 'A'-'Z' -> 0x0a-0x23, ' ' -> 0x24. The
 * card uses warning-text colour attr=10 (the green the warning is drawn in). */
#define CARD_LINES 3
#define CARD_PITCH 16                 /* display-y px between card lines (8px glyph + gap) */
#define CARD_COLS  (DISPW/8)          /* 28 display columns                                */
#define CARD_COL   10                 /* fg colour attr (the warning green)                */
#define CARD_SPACE 0x24               /* font code for ' ' (not drawn, only advances)      */
#define CARD_MAXCELLS 256
/* 3-line credits card, each line horizontally CENTRED within the 28-col playfield by
 * build_card (x0=(28-len)*4). The Capcom font has no '.' glyph (card_code returns
 * blank), so the period is dropped from "JOTD666" to keep the line truly centred. */
static const char *card_text[CARD_LINES] = {
    "CONVERSION BY",
    "WHITTY ARCADE 2026",
    "CREDIT TO JOTD666",
};
/* ASCII -> Gun.Smoke fg tile code (see above). -1 for unsupported chars. */
static int card_code(char ch){
    if (ch>='0'&&ch<='9') return ch-'0';
    if (ch>='A'&&ch<='Z') return 0x0a+(ch-'A');
    if (ch==' ')          return CARD_SPACE;
    return -1;
}
/* flattened card: one entry per glyph CELL in reading order (incl spaces), with its
 * fixed display position. by is V-relative (added per frame so it tracks the scroll). */
static struct { short bx, dy; short code; } card_cell[CARD_MAXCELLS];
static int card_ncell, card_built;
static void build_card(void){
    if (card_built) return;
    int Y0 = (DISPH - CARD_LINES*CARD_PITCH)/2;        /* vertically centred block */
    int n=0;
    for (int L=0; L<CARD_LINES; L++){
        const char *s=card_text[L]; int len=0; while (s[len]) len++;
        int x0=(CARD_COLS-len)*4;                       /* horizontally centre this line */
        int dy=Y0 + L*CARD_PITCH;
        for (int i=0; i<len && n<CARD_MAXCELLS; i++){
            int c=card_code(s[i]);
            card_cell[n].bx=(short)(x0+i*8);
            card_cell[n].dy=(short)dy;
            card_cell[n].code=(short)(c<0?CARD_SPACE:c);
            n++;
        }
    }
    card_ncell=n; card_built=1;
}
/* draw the first `reveal` cells of the card as char bobs (spaces skipped, just advance).
 * Mirrors draw_sprites' save-all-then-draw-all so restore leaves no trails. */
static void draw_card(int V, int bi, int reveal){
    bob_t *bb=bobs[bi]; int n=0;
    if (reveal>card_ncell) reveal=card_ncell;
    for (int i=0; i<reveal && n<MAXBOB; i++){
        if (card_cell[i].code==CARD_SPACE) continue;    /* space: advance only, no glyph */
        bob_t *b=&bb[n++];
        b->bx=card_cell[i].bx; b->by=V+card_cell[i].dy;
        b->code=card_cell[i].code; b->col=CARD_COL; b->flip=0; b->kind=KIND_CHR;
    }
    nbob[bi]=n;
    for (int i=0;i<n;i++) save_bob(&bb[i]);
    for (int i=0;i<n;i++) draw_char_direct(&bb[i]);
}
static int card_frame, card_done;     /* card_frame: ticks while card active; card_done: latched */

/* ---- public renderer API (matches the c1943/commando contract used by *_hwmain.c) ---- */
void gunsmoke_hw_splash(void)
{
    chars   = gunsmoke_rom_chars;   tiles = gunsmoke_rom_tiles;
    sprites = gunsmoke_rom_sprites; bgmap = gunsmoke_rom_bgmap; proms = gunsmoke_rom_proms;
    /* Gun.Smoke is a pure VERTICAL scroller (no h fine scroll) -- drop the preroll word. */
    hwscroll_no_preroll = 1;
    /* Centre the playfield with the canonical DIWSTRT (hstart=0x81+(320-DISPW)/2), the
     * exact same centred window 1943 / Terra Cresta use -> equal left/right margins.
     * HWIN_ADJ defaults 0 (no nudge); override at build time only if a specific capture
     * needs a tweak. EVEN value keeps hstart odd so DDFSTRT stays exact (content flush). */
    hwscroll_hwin_adj = HWIN_ADJ;
    if (!hwscroll_open(&S, 1, NPLANES, DISPW, DISPH, BUFW, BUFH)) return;
    buf[0]=S.bpl[0];
    /* 2nd display buffer FIRST so double-buffering wins its chip RAM (avoids tear). */
    buf[1]=(uint8_t*)AllocMem((unsigned long)NPLANES*S.plane_sz, MEMF_CHIP|MEMF_CLEAR);
    s_db = (buf[1]!=0);
    if (!s_db) buf[1]=buf[0];
    bscratch=(uint16_t*)AllocMem(BOBIMG_WORDS*2UL, MEMF_CHIP|MEMF_CLEAR);
    bobpool =(uint16_t*)AllocMem((unsigned long)NBOBCACHE*BOBIMG_WORDS*2, MEMF_CHIP|MEMF_CLEAR);
    for (int i=0;i<NBOBCACHE;i++) bobkey[i]=-1;
    init_char_cache();
    hud_plane_sz = (long)S.stride * HUDH;
    hud_bpl = (uint8_t*)AllocMem((unsigned long)NPLANES*hud_plane_sz, MEMF_CHIP|MEMF_CLEAR);
    s_hud_ok = (hud_bpl != 0);
    hud_last_hash = 0;
    /* boot palette: index 0 = dark (blank screen) until the real palette loads. */
    { uint8_t rgb[3]={0x10,0x10,0x18}; hwscroll_palette8(&S, 0, rgb, 1); }
    hwscroll_set(&S, 0, 0, 0);
    hwscroll_frame(&S);
}

void gunsmoke_hw_open(void)
{
    if (!S.ok) return;
    decode_gfx();
    build_ctabs();
    /* 128-colour 24-bit Amiga palette. Bg/fg are exact; sprite colours are compressed
     * into SPR_PEN_BASE..127 by the same remap used in build_ctabs(). */
    for (int i=0;i<NCOLORS;i++){
        pal256[i*3+0]=(uint8_t)w4(proms[0x000+i]);
        pal256[i*3+1]=(uint8_t)w4(proms[0x100+i]);
        pal256[i*3+2]=(uint8_t)w4(proms[0x200+i]);
    }
    for (int color=0;color<16;color++) for (int pix=0;pix<16;pix++){
        int original = 0x80 | (proms[0x600+color*16+pix]&0xf) | ((proms[0x700+color*16+pix]&7)<<4);
        int dst = spr_remap_pen(original);
        pal256[dst*3+0]=(uint8_t)w4(proms[0x000+original]);
        pal256[dst*3+1]=(uint8_t)w4(proms[0x100+original]);
        pal256[dst*3+2]=(uint8_t)w4(proms[0x200+original]);
    }
    pal256[BLACK_PEN*3+0]=pal256[BLACK_PEN*3+1]=pal256[BLACK_PEN*3+2]=0;
    hwscroll_palette8(&S, 0, pal256, NCOLORS);
#ifdef GS_HWSPR
    build_spr_pal15();              /* per-bank 15-colour hw-sprite palettes (rgb12) */
    s_hwspr_ok = S.ok;              /* AGA hw sprites available -> route sprites there */
#endif

    cur_sy=GS_LOCK_SHORT_SCROLL ? 0 : (gunsmoke_scrolly()&0xff);
    cur_bgdis=gunsmoke_bgon()?0:1;
    last_raw=gunsmoke_scrollx()&0xffff; cont=last_raw;
    int V=(-(cont+255))&(RING-1);
    int cfg=(cur_sy<<1)|cur_bgdis;
    for (int b=0;b<(s_db?2:1);b++){ refill_full(b); buf_cfg[b]=cfg; nbob[b]=0; }
    S.bpl[0]=buf[0]; curdisp=0;
    hwscroll_set(&S, 0, 0, V);
    s_open=1;
}

void gunsmoke_hw_frame(MY_LITTLE_Z80 *z)
{
    if (!s_open) return;
    cur_sy=GS_LOCK_SHORT_SCROLL ? 0 : (gunsmoke_scrolly()&0xff);
    cur_bgdis=gunsmoke_bgon()?0:1;
    advance(gunsmoke_scrollx());
    int V=(-(cont+255))&(RING-1);
    int cfg=(cur_sy<<1)|cur_bgdis;
    int dense_fg = card_done && fg_visible_count(z, s_hud_ok ? 29 : 32) > GS_DENSE_FG_SKIP_THRESHOLD;
    if (dense_fg) {
        dense_fg_phase ^= 1;
        if (!dense_fg_phase) {
            hwscroll_frame_lateok(&S);
            return;
        }
    } else {
        dense_fg_phase = 0;
    }

    int back = s_db ? (curdisp^1) : 0;
    S.bpl[0]=buf[back];                            /* copper points here via hwscroll_set */
    restore_bobs(back);
#ifdef GS_HWSPR
    if (s_hwspr_ok) hwscroll_aspr_clear(&S);       /* new hw-sprite frame (cleared each frame) */
#endif
    if (cfg != buf_cfg[back]){ refill_full(back); buf_cfg[back]=cfg; }   /* scrolly/bg-off change */
    else                       fill_ahead(&bhead[back]);                 /* steady: ~1 col/frame */
    /* WARNING phase (objon==0, before the title): show the typewriter credits card
     * instead of the game's warning text + sprites. Latched off once objon first
     * flips to 1 (the title), so attract/gameplay that follows is untouched. */
    int use_hud = 0;
    if (!card_done && !gunsmoke_objon()){
        build_card();
        card_frame++;
        draw_card(V, back, card_frame / CARD_CHAR_FRAMES);
    } else {
        if (gunsmoke_objon()) card_done=1;          /* title started -> retire the card */
        draw_sprites(z, V, back);
        use_hud = s_hud_ok;
        if (use_hud) {
            unsigned hh = hud_hash(z);
            if (hh != hud_last_hash) { draw_hud(z); hud_last_hash = hh; }
        }
    }
    /* TEAR-FREE SWAP ORDER (poster-gallery flicker fix).
     * hwscroll_set() rewrites the 8 bitplane-pointer MOVEs inside the SINGLE shared copper
     * list. Those MOVEs are re-read by the copper at every frame TOP. The old order wrote
     * them BEFORE the vblank wait, i.e. at whatever raster line the per-frame drawing
     * happened to finish on. For LIGHT, uniform frames (normal gameplay) that landed in a
     * safe band, so gameplay never flickered. But the attract poster gallery is HEAVY and
     * VARIABLE (a scrolling bg poster PLUS ~297 static fg-char bobs for the GUN.SMOKE
     * banner + text): its drawing time drifts across the few rasterlines between the
     * bottom-of-frame and the next top, so on many frames set_ptrs() rewrote the pointer
     * MOVEs *while the copper was reading them at the top* -> the 8 pointers tore for one
     * frame (half old buffer / half new) -> the size-correlated flicker (bigger/denser
     * posters = more fill time = more frames landing in the race window).
     * Fix: wait for vblank FIRST (copper still shows the front buffer, its top MOVEs
     * already executed), THEN write the back-buffer pointers in that safe window (vpos~300;
     * the next top is ~13 lines away, far more than the ~9us the 33-word write needs). The
     * copper latches a fully-consistent pointer set at the next top. No tear, no added
     * latency, GunSmoke-only (shared hwscroll.c untouched, so 1943/Terra Cresta unaffected). */
#ifdef GS_HWSPR
    if (s_hwspr_ok) hwscroll_aspr_finish(&S);      /* commit hw-sprite list + copper palette reloads */
#endif
    hwscroll_frame_lateok(&S);                     /* wait vblank unless drawing already landed there */
    hwscroll_set(&S, 0, 0, V);                     /* now write back-buffer ptrs, race-free  */
    if (use_hud)
        hwscroll_hud_bands(&S, buf[back], hud_bpl, hud_plane_sz, V, HUD_PLAY_TOP, DISPH);
    curdisp=back;
}

/* no-op kept for API parity with c1943_hwmain (render-skip path); unused here */
void gunsmoke_hw_wait(void){ if (s_open) hwscroll_frame(&S); }
