/* c1943_glue.c -- 1943 machine wrapper exposing the simple API the RTG presenter
 * uses (g1943_init/frame/set_inputs/chunky/pal256/dims). Built on the native
 * main-Z80 bridge (c1943_native_rust.c) + software renderer (c1943_render.c).
 * ROMs are embedded via incbin (c1943_romdata.s).
 */
#include <stdint.h>
#include <string.h>
#include "z80emu.h"
#include "c1943_machine.h"
#include "c1943_render.h"

/* embedded ROM regions (c1943_romdata.s); gfx/tilerom/proms are contiguous. */
extern const unsigned char c1943_p0[], c1943_p1[], c1943_p2[];
extern const unsigned char c1943_gfx1[], c1943_gfx2[], c1943_gfx3[], c1943_gfx4[];
extern const unsigned char c1943_tilerom[], c1943_proms[];
extern const unsigned char c1943_snd[];        /* audio Z80 program (bm05.4k) */
extern void c1943_audio_init(const unsigned char *snd);
extern void c1943_audio_command(unsigned char v);

static MY_LITTLE_Z80 Z;
static uint8_t maincpu[0x30000];          /* fixed @0, banks @0x10000 */
static uint8_t fb[C1943_NH][C1943_NW];    /* native chunky frame */
static uint8_t pal_flat[256 * 3];

/* WARMUP-SAFE: g1943_init runs INSIDE the ArcadeIntro loader, which has taken over
 * the OS -- so it must be pure computation. NO AllocMem here. Audio init is
 * deferred to g1943_late_init(), called once the loader exits. */
void g1943_init(void)
{
    memset(maincpu, 0, sizeof maincpu);
    memcpy(maincpu + 0x00000, c1943_p0, 0x08000);
    memcpy(maincpu + 0x10000, c1943_p1, 0x10000);
    memcpy(maincpu + 0x20000, c1943_p2, 0x10000);

    c1943_load_maincpu(maincpu);
    c1943_init(&Z);
    c1943_render_init(c1943_gfx1, c1943_gfx2, c1943_gfx3, c1943_gfx4,
                      c1943_tilerom, c1943_proms);

    { uint8_t pal[256][3]; c1943_build_palette(pal);
      for (int i = 0; i < 256; i++) {
          pal_flat[i*3+0] = pal[i][0];
          pal_flat[i*3+1] = pal[i][1];
          pal_flat[i*3+2] = pal[i][2];
      } }
}

/* Deferred init: AllocMem-backed YM2203 state. Called from the RTG main after
 * the loader exits and the OS is back -- never from the intro warmup. */
void g1943_late_init(void)
{
    c1943_audio_init(c1943_snd);   /* sound Z80 + 2x YM2203 (registers audio hooks) */
    c1943_audio_command(0x20);     /* attract/start music cue safety net */
    /* Hi-score disk I/O is intentionally not linked in this RTG transcode build. */
}

void g1943_frame(void)
{
    c1943_run_frame(&Z);
    c1943_render_frame(&Z, fb);
}

void g1943_run_frame(void)
{
    c1943_run_frame(&Z);
}

void g1943_render_frame(void)
{
    c1943_render_frame(&Z, fb);
}

/* High-score persistence is not linked in this RTG transcode package. */
void g1943_scores_tick(void) { }
void g1943_scores_save(void) { }

void g1943_set_inputs(uint8_t sys, uint8_t p1, uint8_t p2, uint8_t dswa, uint8_t dswb)
{
    c1943_set_inputs(sys, p1, p2, dswa, dswb);
}

void g1943_dev_warp_boss(void)
{
    /* Stage-1 event table thresholds are based on the e411/e412 scroll counter.
     * Slot 7 is the late-stage 0xb368 trigger, just before the big boat path. */
    c1943_poke(&Z, 0xe450, 7);      /* stage event index */
    c1943_poke(&Z, 0xe411, 0x68);   /* scroll low */
    c1943_poke(&Z, 0xe412, 0xb3);   /* scroll high */
    c1943_poke(&Z, 0xe410, 0x00);   /* display scrolly byte */
    c1943_poke(&Z, 0xe402, 0x68);   /* bg2 scroll low */
    c1943_poke(&Z, 0xe403, 0xb3);   /* bg2 scroll high */
}

const unsigned char *g1943_chunky(void)        { return (const unsigned char *)fb; }
const unsigned char *g1943_pal256(int *n)       { if (n) *n = 256; return pal_flat; }
void                 g1943_dims(int *w, int *h) { if (w) *w = C1943_NW; if (h) *h = C1943_NH; }
int                  g1943_bg1_scrollx(void)   { return c1943_bg1_scrollx(&Z); }
int                  g1943_bg1_scrolly(void)   { return c1943_bg1_scrolly(&Z); }
int                  g1943_bg2_scrollx(void)   { return c1943_bg2_scrollx(&Z); }
