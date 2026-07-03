/* src/hal/c1942_audio_amiga.c -- Amiga Paula playback for the 1942 sound chain.
 * Renders one frame of mixed AY PCM (c1942_audio_frame) into a chip-RAM double
 * buffer and plays it on Paula channels 0 (left) + 1 (right), looping. Direct
 * register banging in OS-takeover mode (matches c1942_video.c). */
#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdint.h>

extern void c1942_audio_frame(signed char *out, int nsamples);

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

#define C_SR     18750
#define C_SPF    375           /* The offline AY renderer is calibrated for 10
                                * AY ticks/sample at 18.75 kHz. */
#define C_BYTES  ((C_SPF + 1) & ~1)
#define C_PER    189           /* PAL 3546895 / 18750 */

static signed char *abuf[2] = { 0, 0 };
static int afront = 0;

static void render_frame(signed char *b)
{
    c1942_audio_frame(b, C_SPF);
    if (C_BYTES != C_SPF) b[C_SPF] = b[C_SPF - 1];  /* Paula length is word-sized */
}

static void point_paula(signed char *b)
{
    volatile uint16_t *c = CUSTOM;
    uint32_t a = (uint32_t)b;
    c[R_AUD0LCH] = (uint16_t)(a >> 16); c[R_AUD0LCH+1] = (uint16_t)a;
    c[R_AUD1LCH] = (uint16_t)(a >> 16); c[R_AUD1LCH+1] = (uint16_t)a;
    c[R_AUD0LEN] = C_BYTES / 2; c[R_AUD1LEN] = C_BYTES / 2;
    c[R_AUD0PER] = C_PER;       c[R_AUD1PER] = C_PER;
    c[R_AUD0VOL] = 64;          c[R_AUD1VOL] = 64;
}

void c1942_audio_amiga_open(void)
{
    volatile uint16_t *c = CUSTOM;
    void *chunk = AllocMem(C_BYTES * 2, MEMF_CHIP | MEMF_CLEAR);
    if (!chunk) return;
    abuf[0] = (signed char *)chunk;
    abuf[1] = (signed char *)chunk + C_BYTES;
    /* prime both buffers */
    render_frame(abuf[0]);
    render_frame(abuf[1]);
    c[R_DMACON] = 0x0003;                       /* stop AUD0/1 while we set up */
    point_paula(abuf[0]);                         /* mono: both channels same buffer */
    c[R_DMACON] = 0x8203;                       /* SET | DMAEN | AUD0 | AUD1 */
    afront = 0;
}

/* Render the next frame of audio into the back buffer and hand it to Paula; it
 * is picked up at the next DMA loop (~1 frame). */
void c1942_audio_amiga_frame(void)
{
    volatile uint16_t *c = CUSTOM;
    if (!abuf[0]) return;
    int back = afront ^ 1;
    render_frame(abuf[back]);
    point_paula(abuf[back]);
    c[R_DMACON] = 0x8203;                       /* survive display DMA reasserts */
    afront = back;
}
