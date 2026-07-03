/* src/hal/c1942_audio_native.c -- Paula-native 1942 audio runtime.
 *
 * The build renders the original sound Z80 + AY commands into signed 8-bit PCM
 * clips. Runtime only snoops the main CPU sound latch and mixes those clips into
 * the small Paula frame buffer; no sound Z80 or AY synthesis runs on the Amiga.
 */
#include "z80emu.h"
#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

extern MY_LITTLE_Z80 *c1942_audio_z;
extern void (*c1942_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v);
extern void (*c1942_latch_hook)(unsigned char v);

extern const signed char c1942_sfx_blob[];
extern const unsigned long c1942_sfx_offsets[256];
extern const unsigned long c1942_sfx_lengths[256];
extern const unsigned char c1942_sfx_flags[256];

#define NVOICES 4
#define FLAG_LOOP 1

typedef struct {
    const signed char *p;
    unsigned long len;
    unsigned long pos;
    unsigned char flags;
} voice_t;

static voice_t voices[NVOICES];
static int next_sfx = 1;
static signed char *fast_bank;
static unsigned long fast_bank_len;

static const signed char *sample_bank(void)
{
    return fast_bank ? fast_bank : c1942_sfx_blob;
}

static void prealloc_sample_bank(void)
{
    unsigned long end = 0;
    if (fast_bank) return;
    for (int i = 0; i < 256; i++) {
        unsigned long e = c1942_sfx_offsets[i] + c1942_sfx_lengths[i];
        if (e > end) end = e;
    }
    if (!end) return;
    fast_bank = (signed char *)AllocMem(end, MEMF_FAST);
    if (!fast_bank) return;
    memcpy(fast_bank, c1942_sfx_blob, end);
    fast_bank_len = end;
}

static void start_voice(int idx, unsigned char cmd)
{
    unsigned long len = c1942_sfx_lengths[cmd];
    if (!len) return;
    voices[idx].p = sample_bank() + c1942_sfx_offsets[cmd];
    voices[idx].len = len;
    voices[idx].pos = 0;
    voices[idx].flags = c1942_sfx_flags[cmd];
}

void c1942_audio_command(unsigned char cmd)
{
    if (cmd == 0xff) return;
    if (!c1942_sfx_lengths[cmd]) return;
    if (c1942_sfx_flags[cmd] & FLAG_LOOP) {
        /* Looped captures are the music channel. A new music command, including a
         * restart after death/start, must replace the old loop immediately. */
        start_voice(0, cmd);
        return;
    }
    int idx = next_sfx;
    next_sfx++;
    if (next_sfx >= NVOICES) next_sfx = 1;
    start_voice(idx, cmd);
}

void c1942_audio_init(const unsigned char *snd)
{
    (void)snd;
    prealloc_sample_bank();
    (void)fast_bank_len;
    memset(voices, 0, sizeof voices);
    next_sfx = 1;
    c1942_audio_z = 0;
    c1942_audio_wr_hook = 0;
    c1942_latch_hook = c1942_audio_command;
}

void c1942_audio_frame(signed char *out, int nsamples)
{
    for (int i = 0; i < nsamples; i++) {
        int s = 0;
        for (int v = 0; v < NVOICES; v++) {
            voice_t *q = &voices[v];
            if (!q->p || !q->len) continue;
            s += q->p[q->pos++];
            if (q->pos >= q->len) {
                if (q->flags & FLAG_LOOP) q->pos = 0;
                else q->p = 0;
            }
        }
        if (s > 120) s = 120;
        if (s < -120) s = -120;
        out[i] = (signed char)s;
    }
}
