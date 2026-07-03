/* c1943_audio_amiga.c -- Paula playback for the 1943 native-transcode sound board.
 *
 * The sound Z80 music driver advances on four IRQs per video frame. Paula is fed
 * with four-frame blocks. The next block is scheduled halfway through the
 * current one, so render/frame jitter cannot make Paula briefly repeat a tiny
 * 134-sample fragment and wobble the music speed. */
#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdint.h>

extern void c1943_audio_frame(signed char *out, int nsamples);

#ifndef C1943_LIBC_MALLOC
static void *amem(unsigned long n, unsigned long fl){
    unsigned long *p=(unsigned long*)AllocMem(n+8, MEMF_FAST|fl);
    if(!p) p=(unsigned long*)AllocMem(n+8, MEMF_ANY|fl);
    if(!p) return 0; p[0]=n+8; return p+2;
}
void *malloc(unsigned long n){ return amem(n,0); }
void *calloc(unsigned long a,unsigned long b){ return amem(a*b,MEMF_CLEAR); }
void  free(void *q){ if(!q)return; unsigned long *p=(unsigned long*)q-2; FreeMem(p,p[0]); }
#endif

#define CUSTOM ((volatile uint16_t *)0xdff000)
#define R_DMACON  (0x096/2)
#define R_AUD0LCH (0x0a0/2)
#define R_AUD0LEN (0x0a4/2)
#define R_AUD0PER (0x0a6/2)
#define R_AUD0VOL (0x0a8/2)
#define R_AUD1LCH (0x0b0/2)
#define R_AUD1LEN (0x0b4/2)
#define R_AUD1PER (0x0b6/2)
#define R_AUD1VOL (0x0b8/2)
#define R_AUD2LEN (0x0c4/2)
#define R_AUD2VOL (0x0c8/2)
#define R_AUD3LEN (0x0d4/2)
#define R_AUD3VOL (0x0d8/2)

#define C_SR     8040
#define C_SPF    (C_SR / 60)       /* 134 samples per 60 Hz arcade frame */
#define C_PER    441               /* PAL Paula 3546895 / 8040 ~= 441 */
#define C_BLOCK_FRAMES 4
#define C_BLOCK_SAMPLES (C_SPF * C_BLOCK_FRAMES)

static signed char *abuf[2] = { 0, 0 };
static int afront = 0;
static int frame_phase = 0;
static int next_prepared = 0;

static void set_aud_ptr(volatile uint16_t *c, signed char *p)
{
    uint32_t a = (uint32_t)p;
    c[R_AUD0LCH] = (uint16_t)(a >> 16); c[R_AUD0LCH+1] = (uint16_t)a;
    c[R_AUD1LCH] = (uint16_t)(a >> 16); c[R_AUD1LCH+1] = (uint16_t)a;
}

void c1943_audio_amiga_open(void)
{
    volatile uint16_t *c = CUSTOM;
    void *chunk;
    if (abuf[0]) return;
    chunk = AllocMem(C_BLOCK_SAMPLES * 2, MEMF_CHIP | MEMF_CLEAR);
    if (!chunk) return;
    abuf[0] = (signed char *)chunk;
    abuf[1] = (signed char *)chunk + C_BLOCK_SAMPLES;

    for (int i = 0; i < C_BLOCK_FRAMES; i++) c1943_audio_frame(abuf[0] + i * C_SPF, C_SPF);
    for (int i = 0; i < C_BLOCK_FRAMES; i++) c1943_audio_frame(abuf[1] + i * C_SPF, C_SPF);
    CacheClearU();

    c[R_DMACON] = 0x000f;
    c[R_AUD0VOL] = 0; c[R_AUD1VOL] = 0; c[R_AUD2VOL] = 0; c[R_AUD3VOL] = 0;
    c[R_AUD2LEN] = 0; c[R_AUD3LEN] = 0;
    set_aud_ptr(c, abuf[0]);
    c[R_AUD0LEN] = C_BLOCK_SAMPLES / 2; c[R_AUD1LEN] = C_BLOCK_SAMPLES / 2;
    c[R_AUD0PER] = C_PER;     c[R_AUD1PER] = C_PER;
    c[R_AUD0VOL] = 64;        c[R_AUD1VOL] = 64;
    c[R_DMACON] = 0x8203;
    afront = 0;
    frame_phase = 0;
    next_prepared = 1;
}

void c1943_audio_amiga_close(void)
{
    volatile uint16_t *c = CUSTOM;
    c[R_DMACON]  = 0x000f;
    c[R_AUD0VOL] = 0; c[R_AUD1VOL] = 0; c[R_AUD2VOL] = 0; c[R_AUD3VOL] = 0;
    for (volatile unsigned i = 0; i < 50000; i++) ;
    if (abuf[0]) {
        FreeMem(abuf[0], C_BLOCK_SAMPLES * 2);
        abuf[0] = 0;
        abuf[1] = 0;
    }
    afront = 0;
    frame_phase = 0;
    next_prepared = 0;
}

void c1943_audio_amiga_frame(void)
{
    volatile uint16_t *c = CUSTOM;
    int back;
    if (!abuf[0]) return;
    frame_phase++;
    if (frame_phase == (C_BLOCK_FRAMES / 2)) {
        back = afront ^ 1;
        if (!next_prepared) {
            for (int i = 0; i < C_BLOCK_FRAMES; i++) c1943_audio_frame(abuf[back] + i * C_SPF, C_SPF);
        }
        CacheClearU();
        set_aud_ptr(c, abuf[back]);
        next_prepared = 0;
    }
    if (frame_phase >= C_BLOCK_FRAMES) {
        afront ^= 1;
        frame_phase = 0;
    }
}
