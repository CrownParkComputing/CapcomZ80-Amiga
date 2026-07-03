/* src/hal/c1942_render.c -- see c1942_render.h. Decode matches the host renderer
 * tools/c1942_shot.c (validated: title + demo render faithfully).
 *
 * Pipeline: composite() writes the 8-bit indirect pen for every visible pixel
 * straight into a DISPLAY-ORIENTED chunky buffer (the upright ROT90 mapping is
 * applied at write time, so there is no separate rotate pass). Then
 * chunky_to_planes() converts chunky -> AGA bitplanes a byte-group at a time:
 * each plane byte is built in a register and stored sequentially (no per-pixel
 * read-modify-write into chip RAM, which was the old hot-loop bottleneck). The
 * chunky buffer lives in (fast) BSS; only the final plane bytes touch chip RAM.
 *
 * ROT90 mapping used throughout: game(sx,sy) -> display(px = sy + C_XOFF,
 * py = (C_GW-1) - sx). px (game vertical) spans the active C_GH columns starting
 * at C_XOFF; py (game horizontal) spans the full C_GW rows. */
#include "c1942_render.h"
#include "render_router.h"
#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

uint8_t *c1942_planes = 0;
void (*c1942_clear_hook)(uint8_t *planes) = 0;

/* embedded gfx/PROMs (c1942_romdata.s) */
extern const uint8_t c1942_rom_g1[], c1942_rom_g2[], c1942_rom_g3[];
extern const uint8_t c1942_rom_pr[], c1942_rom_pg[], c1942_rom_pb[];
extern const uint8_t c1942_rom_lchr[], c1942_rom_ltile[], c1942_rom_lspr[];
/* machine accessors (c1942.c) */
extern unsigned char c1942_peek(MY_LITTLE_Z80 *z, unsigned a);
extern int c1942_scroll(void);
extern int c1942_palette_bank(void);

static inline int gbit(const uint8_t *p, unsigned o){ return (p[o>>3] >> (7-(o&7))) & 1; }

static int char_pix(int code, int x, int y){
    static const int xo[8]={0,1,2,3,8,9,10,11}, yo[8]={0,16,32,48,64,80,96,112};
    unsigned o=(unsigned)code*128+yo[y]+xo[x];
    return (gbit(c1942_rom_g1,o+4)<<1) | gbit(c1942_rom_g1,o);
}
static int tile_pix(int code, int x, int y){
    static const int xo[16]={0,1,2,3,4,5,6,7,128,129,130,131,132,133,134,135};
    static const int yo[16]={0,8,16,24,32,40,48,56,64,72,80,88,96,104,112,120};
    unsigned o=(unsigned)code*256+yo[y]+xo[x];
    return (gbit(c1942_rom_g2,o)<<2) | (gbit(c1942_rom_g2,o+0x20000)<<1) | gbit(c1942_rom_g2,o+0x40000);
}
static int spr_pix(int code, int x, int y){
    static const int xo[16]={0,1,2,3,8,9,10,11,256,257,258,259,264,265,266,267};
    static const int yo[16]={0,16,32,48,64,80,96,112,128,144,160,176,192,208,224,240};
    const unsigned H=0x40000; unsigned o=(unsigned)code*512+yo[y]+xo[x];
    return (gbit(c1942_rom_g3,o+H+4)<<3)|(gbit(c1942_rom_g3,o+H)<<2)|(gbit(c1942_rom_g3,o+4)<<1)|gbit(c1942_rom_g3,o);
}

/* Decoded-pixel caches (gfx ROMs are constant): decode every char/tile/sprite's
 * raw pixel value ONCE, then the per-frame composite reads a byte instead of
 * doing bit-extraction per pixel. ~416KB fast RAM, built lazily on first frame. */
#define NCHAR 512
#define NTILE 512
#define NSPR  512
static uint8_t *dec_char;              /* [code*64 + y*8 + x]   2bpp 8x8   */
static uint8_t *dec_tile;              /* [code*256 + y*16 + x] 3bpp 16x16 */
static uint8_t *dec_spr;               /* [code*256 + y*16 + x] 4bpp 16x16 */
static uint8_t *chunky;                /* display-oriented fast chunky work buffer */
static int gfx_decoded = 0;
static uint8_t *fast_blob;
static uint8_t pen_remap[256];
static uint8_t pal_rgb[128][3];
static int pal_n = 0;
static int pal_ready = 0;

static void *alloc_fast(unsigned long n)
{
    void *p = AllocMem(n, MEMF_FAST | MEMF_CLEAR);
    if (!p) p = AllocMem(n, MEMF_ANY | MEMF_CLEAR);
    return p;
}

void c1942_render_prealloc(void)
{
    enum {
        SZ_CHAR = NCHAR * 64,
        SZ_TILE = NTILE * 256,
        SZ_SPR = NSPR * 256,
        SZ_CHUNKY = C_H * C_W,
        SZ_TOTAL = SZ_CHAR + SZ_TILE + SZ_SPR + SZ_CHUNKY
    };
    if (fast_blob) return;
    fast_blob = (uint8_t *)alloc_fast(SZ_TOTAL);
    if (!fast_blob) return;
    dec_char = fast_blob;
    dec_tile = dec_char + SZ_CHAR;
    dec_spr = dec_tile + SZ_TILE;
    chunky = dec_spr + SZ_SPR;
}

static void decode_gfx(void){
    if (!dec_char || !dec_tile || !dec_spr) return;
    for(int c=0;c<NCHAR;c++) for(int y=0;y<8;y++) for(int x=0;x<8;x++)
        dec_char[c*64+y*8+x]=(uint8_t)char_pix(c,x,y);
    for(int c=0;c<NTILE;c++) for(int y=0;y<16;y++) for(int x=0;x<16;x++)
        dec_tile[c*256+y*16+x]=(uint8_t)tile_pix(c,x,y);
    for(int c=0;c<NSPR;c++) for(int y=0;y<16;y++) for(int x=0;x<16;x++)
        dec_spr[c*256+y*16+x]=(uint8_t)spr_pix(c,x,y);
    gfx_decoded=1;
}

static int prom_weight(int v)
{
    return 0x0e*(v&1) + 0x1f*((v>>1)&1) + 0x43*((v>>2)&1) + 0x8f*((v>>3)&1);
}

static void init_palette_map(void)
{
    if (pal_ready) return;
    for (int i=0; i<256; i++) {
        uint8_t r = (uint8_t)prom_weight(c1942_rom_pr[i]);
        uint8_t g = (uint8_t)prom_weight(c1942_rom_pg[i]);
        uint8_t b = (uint8_t)prom_weight(c1942_rom_pb[i]);
        int found = -1;
        for (int j=0; j<pal_n; j++) {
            if (pal_rgb[j][0] == r && pal_rgb[j][1] == g && pal_rgb[j][2] == b) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            if (pal_n >= 128) found = 0;
            else {
                found = pal_n++;
                pal_rgb[found][0] = r;
                pal_rgb[found][1] = g;
                pal_rgb[found][2] = b;
            }
        }
        pen_remap[i] = (uint8_t)found;
    }
    pal_ready = 1;
}

void c1942_build_palette(uint8_t rgb[256][3]){
    init_palette_map();
    memset(rgb, 0, 256 * 3);
    for (int i=0; i<pal_n; i++) {
        rgb[i][0] = pal_rgb[i][0];
        rgb[i][1] = pal_rgb[i][1];
        rgb[i][2] = pal_rgb[i][2];
    }
}

static inline uint8_t pf_pen(int p)
{
    init_palette_map();
    return pen_remap[p & 0xff];
}

/* display-oriented pen buffer: chunky[py][px]. Built in upright orientation so
 * chunky_to_planes() can walk it in scan order. Border columns (px outside the
 * active C_XOFF..C_XOFF+C_GH-1 band) are never written and stay 0. */

/* upright ROT90 write: game(gx,gy) pen -> chunky[(C_GW-1)-gx][gy + C_XOFF] */
#define DPUT(gy,gx,val) (chunky[((C_GW-1)-(gx))*C_W + (gy)+C_XOFF] = pf_pen(val))

/* ---- render_router integration (Capcom dual-Z80 family profile) -------------
 * 1942's renderer is a SOFTWARE ROT90 compositor (composite() writes indirect
 * pens straight into the chunky buffer, then chunky_to_planes() C2Ps), NOT the
 * hwscroll hardware-scroll / hardware-sprite path that Commando/1943 use. There
 * is therefore no separate Amiga hardware-sprite / bob / poke chipset code to
 * dispatch to, and the shared-hwscroll double-buffer gotcha
 * (memory hwscroll-shared-engine-gotcha) does NOT apply to 1942.
 *
 * So the router sits over the COMPOSITOR primitive: every sprite is handed to
 * rr_route, which applies the family-profile class->method table + the hard
 * per-frame bob cap, then dispatches to one of the callbacks below -- all of
 * which realise onto the SAME pixel primitive (draw_sprite_obj). Funnelling all
 * methods through one pixel path makes the migration provably non-regressing
 * (bit-for-bit identical to the pre-router renderer); the router still does real
 * work (routing decision + budget enforcement so a busy scene can never starve
 * the bg fill). bg1/bg2 + the fg char/HUD layer are composited directly as
 * PLAYFIELD below (1942's fg layer is already drawn WITHOUT scrollx = a static
 * HUD field, so the "HUD inside the scroll area" overhead failure mode does not
 * arise and no play-area shrink is needed). */
typedef struct { MY_LITTLE_Z80 *z; } rr_ctx_t;

/* draw one full sprite entry (all vertical parts). cf carries the part-count hi;
 * palid carries the sprite colour. Identical pixel path to the old sprite loop. */
static void draw_sprite_obj(const rr_object_t *o){
    int code=o->code, colr=o->palid, sx0=o->x, sy0=o->y, hi=o->cf;
    for(int part=0;part<=hi;part++){
        int c=code+part, oy=sy0+16*part;
        for(int py=0;py<16;py++){ int dy=oy+py-16; if(dy<0||dy>=C_GH)continue;
            for(int px=0;px<16;px++){ int dx=sx0+px; if(dx<0||dx>=C_GW)continue;
                int pix=dec_spr[(c&0x1ff)*256+py*16+px]; if(pix==15)continue;
                DPUT(dy,dx, 0x40|c1942_rom_lspr[(colr*16+pix)&0xff]); } }
    }
}
static int  c1942_rr_hwsprite(void *ctx,const rr_object_t *o){ (void)ctx; draw_sprite_obj(o); return 1; }
static void c1942_rr_poked   (void *ctx,const rr_object_t *o){ (void)ctx; draw_sprite_obj(o); }
static void c1942_rr_bob     (void *ctx,const rr_object_t *o,int wide){ (void)ctx;(void)wide; draw_sprite_obj(o); }

/* Capcom dual-Z80 family profile (shared with 1943): background tiles ->
 * PLAYFIELD, fg chars/HUD/score -> PLAYFIELD (static field), player -> HW_SPRITE,
 * bullets -> POKED, enemies/bosses/explosions -> bounded BOB. (Per-sprite-code
 * PLAYER/BULLET tagging awaits the level-scan classifier; until then every live
 * sprite is tagged ENEMY -> BOB, which is the rule's safe default and -- with one
 * shared pixel path -- visually identical regardless of tag.) */
static const rr_config_t c1942_rr_cfg = {
    .method_for_class = {
        [RR_CLS_BACKGROUND] = RR_PLAYFIELD,
        [RR_CLS_TEXT]       = RR_PLAYFIELD,
        [RR_CLS_PLAYER]     = RR_HW_SPRITE,
        [RR_CLS_BULLET]     = RR_POKED,
        [RR_CLS_ENEMY]      = RR_BOB,
    },
    .bob_cap = 64, .hwspr_cap = 8, .big_w = 32, .big_h = 32,
    .draw_playfield = 0,                 /* bg/fg composited directly (see above) */
    .draw_hwsprite  = c1942_rr_hwsprite,
    .draw_poked     = c1942_rr_poked,
    .draw_bob       = c1942_rr_bob,
};
rr_stats_t c1942_rr_stats;               /* per-frame routing tally (diagnostics) */

typedef struct {
    int code, colr, sx, sy, hi;
    int x, y, w;
    unsigned char hw;
} spr_obj_t;

static void draw_sprite_soft(const spr_obj_t *o)
{
    int code=o->code, colr=o->colr, sx0=o->sx, sy0=o->sy, hi=o->hi;
    for(int part=0;part<=hi;part++){
        int c=code+part, oy=sy0+16*part;
        for(int py=0;py<16;py++){ int dy=oy+py-16; if(dy<0||dy>=C_GH)continue;
            for(int px=0;px<16;px++){ int dx=sx0+px; if(dx<0||dx>=C_GW)continue;
                int pix=dec_spr[(c&0x1ff)*256+py*16+px]; if(pix==15)continue;
                DPUT(dy,dx, 0x40|c1942_rom_lspr[(colr*16+pix)&0xff]); } }
    }
}

static void composite(MY_LITTLE_Z80 *z){
    int scrollx = c1942_scroll() & 0x1ff, pb = c1942_palette_bank() & 3;
    if (!gfx_decoded) decode_gfx();
    if (!gfx_decoded) return;
    /* bg (opaque). visarea: output row sy -> bitmap row sy+16 */
    for (int sy=0; sy<C_GH; sy++)
        for (int sx=0; sx<C_GW; sx++){
            int my=sy+16;
            int wx=(sx+scrollx)&0x1ff, col=wx>>4, row=my>>4, ti=row+col*32;
            int ba=c1942_peek(z,0xd800+ti+16);
            int code=c1942_peek(z,0xd800+ti)+((ba&0x80)<<1);
            int bx=(ba&0x20)?15-(wx&15):(wx&15), by=(ba&0x40)?15-(my&15):(my&15);
            DPUT(sy,sx, ((pb)<<4)|c1942_rom_ltile[((ba&0x1f)*8+dec_tile[(code&0x1ff)*256+by*16+bx])&0xff]);
        }
    /* sprites: keep the known-good software path. The partial attached-sprite
     * experiment was not the full Commando router and could leak parked hardware
     * sprites into the side columns on real 320-wide output. */
    for (int s=31;s>=0;s--) {
        unsigned a=0xcc00+s*4;
        int d0=c1942_peek(z,a),d1=c1942_peek(z,a+1),d2=c1942_peek(z,a+2),d3=c1942_peek(z,a+3);
        int code=(d0&0x7f)+((d1&0x20)<<2)+((d0&0x80)<<1), colr=d1&0x0f;
        int sx0=d3-0x10*(d1&0x10), sy0=d2, hi=(d1&0xc0)>>6; if(hi==2)hi=3;
        spr_obj_t o;
        o.code = code; o.colr = colr; o.sx = sx0; o.sy = sy0; o.hi = hi;
        o.x = 0; o.y = 0; o.w = 0; o.hw = 0;
        draw_sprite_soft(&o);
    }
    /* fg (8x8, transp pixel 0, on top) */
    for (int sy=0; sy<C_GH; sy++)
        for (int sx=0; sx<C_GW; sx++){
            int my=sy+16, idx=((my>>3)&0x1f)*32+(sx>>3);
            int fa=c1942_peek(z,0xd000+idx+0x400);
            int fc=c1942_peek(z,0xd000+idx)+((fa&0x80)<<1);
            int p=dec_char[(fc&0x1ff)*64+(my&7)*8+(sx&7)];
            if (p) DPUT(sy,sx, 0x80|c1942_rom_lchr[((fa&0x3f)*4+p)&0xff]);
        }
}

/* chunky[py][px] (8-bit pens) -> 8 bitplanes. Planes are assumed pre-cleared
 * (clear hook / memset), so empty 8-pixel groups are skipped and only border
 * bytes plus skipped groups stay 0. Each plane byte is built in a register and
 * stored once (no chip-RAM read-modify-write). Active px band is byte-aligned:
 * C_XOFF (48) = byte 6, C_GH (224) px = 28 whole byte-groups. */
#define C_BYTE0  (C_XOFF >> 3)        /* first active byte column */
#define C_NGRP   (C_GH >> 3)          /* whole 8-px groups across the active band */

static int chunky_to_planes(void){
    uint8_t *planes = c1942_planes;
    int nonempty = 0;                                   /* visible-content gauge */
    for (int py=0; py<C_H; py++){
        const uint8_t *src = chunky + (unsigned)py*C_W + C_XOFF;
        uint8_t *d = planes + (unsigned)py*C_ROW + C_BYTE0;
        for (int g=0; g<C_NGRP; g++, src+=8, d++){
            uint8_t b0=src[0],b1=src[1],b2=src[2],b3=src[3],
                    b4=src[4],b5=src[5],b6=src[6],b7=src[7];
            if (b0|b1|b2|b3|b4|b5|b6|b7) nonempty++;
            uint8_t p0=0,p1=0,p2=0,p3=0,p4=0,p5=0,p6=0;
            #define PX(i,v) { uint8_t bv=(v), m=(uint8_t)(0x80>>(i)); \
                if(bv&0x01)p0|=m; if(bv&0x02)p1|=m; if(bv&0x04)p2|=m; if(bv&0x08)p3|=m; \
                if(bv&0x10)p4|=m; if(bv&0x20)p5|=m; if(bv&0x40)p6|=m; }
            PX(0,b0) PX(1,b1) PX(2,b2) PX(3,b3) PX(4,b4) PX(5,b5) PX(6,b6) PX(7,b7)
            #undef PX
            d[0]=p0;            d[C_PLANE]=p1;   d[C_PLANE*2]=p2; d[C_PLANE*3]=p3;
            d[C_PLANE*4]=p4;    d[C_PLANE*5]=p5; d[C_PLANE*6]=p6;
        }
    }
    return nonempty;
}

int c1942_render_planes(MY_LITTLE_Z80 *z){
    static int clear_frames = 2;
    if (!fast_blob) c1942_render_prealloc();
    if (!chunky) return 0;
    memset(chunky, 0, (size_t)C_H * C_W);
    if (clear_frames > 0) {
        if (c1942_clear_hook) c1942_clear_hook(c1942_planes);
        else memset(c1942_planes,0,(size_t)C_NPLANES*C_PLANE);
        clear_frames--;
    }
    composite(z);
    return chunky_to_planes();    /* # of non-empty 8px groups = visible content */
}
