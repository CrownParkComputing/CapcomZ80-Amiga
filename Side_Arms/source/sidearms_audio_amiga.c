/* sidearms_audio_amiga.c -- Amiga Paula playback for the Side Arms sound chain.
 * Continuous-ring Paula playback with EClock refill, matching the Gun.Smoke /
 * Commando interpreter stack. */
#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <stdint.h>

extern void sidearms_audio_render_samples(signed char *out, int nsamples);
extern struct Device *TimerBase;

/* AllocMem-backed malloc/free/calloc for the OPN core (fm.c allocs at YM2203Init).
 * The native AGA build uses the asm slave.s/amiga.s entry (NOT the gcc crt0), so
 * libnix's malloc is never linked -- we must supply our own. (Define SIDEARMS_LIBC_MALLOC
 * to use libc's instead, e.g. a crt0-linked build.) */
#ifndef SIDEARMS_LIBC_MALLOC
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
#define R_AUD2VOL (0x0c8/2)
#define R_AUD3VOL (0x0d8/2)

#define C_SR     8040          /* matches sidearms_audio.c SR */
#define C_PER    441           /* PAL Paula 3546895 / 8040 ~= 441 */
#define C_SPF    (C_SR / 60 + 64)
#define LEAD_FR  10
#define RING_FR  32
#define C_LEAD   (LEAD_FR * C_SPF)
#define C_RING   (RING_FR * C_SPF)

static signed char *ring = 0;
static unsigned long p_play = 0;
static unsigned long p_wrote = 0;
static unsigned long audio_rate = 0;
static unsigned long last_tick = 0;
static unsigned long frac_ticks = 0;

static void aud_setup(volatile uint16_t *c, signed char *p, unsigned int bytes)
{
    uint32_t a = (uint32_t)p;
    unsigned int words = bytes / 2;
    c[R_AUD0LCH] = (uint16_t)(a >> 16); c[R_AUD0LCH+1] = (uint16_t)a;
    c[R_AUD1LCH] = (uint16_t)(a >> 16); c[R_AUD1LCH+1] = (uint16_t)a;
    c[R_AUD0LEN] = words;      c[R_AUD1LEN] = words;
    c[R_AUD0PER] = C_PER;      c[R_AUD1PER] = C_PER;
    c[R_AUD0VOL] = 48;         c[R_AUD1VOL] = 48;
}

static void ring_render(unsigned long n)
{
    while(n){
        unsigned long pos = p_wrote % C_RING;
        unsigned long chunk = C_RING - pos;
        if(chunk > n) chunk = n;
        sidearms_audio_render_samples(ring + pos, (int)chunk);
        p_wrote += chunk;
        n -= chunk;
    }
}

void sa_audio_amiga_close(void);

void sa_audio_amiga_open(void)
{
    volatile uint16_t *c = CUSTOM;
    if(ring) sa_audio_amiga_close();
    ring = (signed char *)AllocMem(C_RING, MEMF_CHIP | MEMF_CLEAR);
    if(!ring) return;

    p_play = 0;
    p_wrote = 0;
    frac_ticks = 0;
    audio_rate = 0;
    last_tick = 0;

    if(TimerBase){
        struct EClockVal ev;
        audio_rate = ReadEClock(&ev);
        last_tick = ev.ev_lo;
    }

    ring_render(C_LEAD);
    CacheClearU();

    c[R_DMACON] = 0x000f;
    c[R_AUD0VOL] = 0; c[R_AUD1VOL] = 0; c[R_AUD2VOL] = 0; c[R_AUD3VOL] = 0;
    aud_setup(c, ring, C_RING);
    c[R_DMACON] = 0x8203;
}

/* Stop Paula + free the chip buffer on exit. WITHOUT this, closing the screen and
 * returning to AGS with AUD0/1 DMA still running on (about-to-be-freed) chip RAM
 * is the classic suspend/guru on exit. */
void sa_audio_amiga_close(void)
{
    volatile uint16_t *c = CUSTOM;
    c[R_DMACON]  = 0x000f;                       /* clear AUD0-3 DMA enable */
    c[R_AUD0VOL] = 0; c[R_AUD1VOL] = 0; c[R_AUD2VOL] = 0; c[R_AUD3VOL] = 0;
    for(volatile int i = 0; i < 20000; ++i) { }
    if(ring){
        FreeMem(ring, C_RING);
        ring = 0;
    }
    p_play = p_wrote = 0;
    frac_ticks = last_tick = audio_rate = 0;
}

void sa_audio_amiga_frame(void)
{
    unsigned long adv = C_SR / 60;
    if(!ring) return;

    /* The game loop is already paced to 60 Hz. Keep the sound Z80/YM timeline
     * frame-stable here; EClock-delta refill made music tempo wobble when RTG
     * rendering jittered even though gameplay stayed smooth. */
    p_play += adv;
    {
        long need = (long)((p_play + C_LEAD) - p_wrote);
        if(need > 0){
            if(need > (long)(C_RING - C_SPF)) need = C_RING - C_SPF;
            ring_render((unsigned long)need);
            CacheClearU();
        }
    }
}
