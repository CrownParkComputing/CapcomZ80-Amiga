#include "z80emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void c1942_audio_init(const unsigned char *snd);
extern void c1942_audio_frame(signed char *out, int nsamples);
extern void (*c1942_latch_hook)(unsigned char v);

#define CMD_COUNT 256
#define SR 18750
#define FPS 50
#define SPF (SR / FPS)
#define WARM_FRAMES 24
#define CAP_FRAMES 180
#define CAP_SAMPLES (SPF * CAP_FRAMES)
#define TAIL_SAMPLES 2400
#define PRE_SAMPLES 80
#define ACTIVE_THRESH 5
#define FLAG_LOOP 1
#define MUSIC_LOOP_MIN_SAMPLES ((SR * 33) / 10)

static signed char capture[CAP_SAMPLES];
static unsigned long offsets[CMD_COUNT], lengths[CMD_COUNT];
static unsigned char flags[CMD_COUNT];

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void read_file(const char *path, unsigned char *dst, size_t len)
{
    FILE *f = fopen(path, "rb");
    if (!f) die(path);
    size_t n = fread(dst, 1, len, f);
    fclose(f);
    if (n != len) {
        fprintf(stderr, "%s: got %zu bytes, expected %zu\n", path, n, len);
        exit(1);
    }
}

static int active_sample(int v)
{
    return v > ACTIVE_THRESH || v < -ACTIVE_THRESH;
}

static void write_dc_l(FILE *f, const unsigned long *vals)
{
    for (int i = 0; i < CMD_COUNT; i++) {
        if ((i & 7) == 0) fputs("\tdc.l\t", f);
        fprintf(f, "%lu", vals[i]);
        if ((i & 7) == 7) fputc('\n', f);
        else fputc(',', f);
    }
}

static void write_dc_b(FILE *f, const unsigned char *vals)
{
    for (int i = 0; i < CMD_COUNT; i++) {
        if ((i & 15) == 0) fputs("\tdc.b\t", f);
        fprintf(f, "%u", vals[i]);
        if ((i & 15) == 15) fputc('\n', f);
        else fputc(',', f);
    }
}

int main(int argc, char **argv)
{
    static unsigned char snd[0x4000];
    signed char frame[SPF];

    if (argc != 4) {
        fprintf(stderr, "usage: %s snd.bin out.bin out.s\n", argv[0]);
        return 2;
    }
    read_file(argv[1], snd, sizeof snd);

    FILE *bin = fopen(argv[2], "wb");
    if (!bin) die(argv[2]);

    unsigned long blob_pos = 0;
    int live_cmds = 0, loop_cmds = 0;

    for (int cmd = 0; cmd < CMD_COUNT; cmd++) {
        c1942_audio_init(snd);
        for (int f = 0; f < WARM_FRAMES; f++) c1942_audio_frame(frame, SPF);
        if (c1942_latch_hook) c1942_latch_hook((unsigned char)cmd);

        for (int f = 0; f < CAP_FRAMES; f++) {
            c1942_audio_frame(capture + f * SPF, SPF);
        }

        int first = -1, last = -1;
        for (int i = 0; i < CAP_SAMPLES; i++) {
            if (active_sample(capture[i])) {
                if (first < 0) first = i;
                last = i;
            }
        }
        if (first < 0) continue;

        first -= PRE_SAMPLES;
        if (first < 0) first = 0;
        /* Do not auto-loop command captures. The Z80 sound program often leaves
         * AY state active at the capture edge for decays/sustains; treating those
         * as loop points makes rough, stuck playback. Runtime retriggers commands
         * from the real latch stream, so clips should be finite one-shots unless a
         * loop is explicitly curated later. */
        last += TAIL_SAMPLES;
        if (last >= CAP_SAMPLES) last = CAP_SAMPLES - 1;

        unsigned long clip_len = (unsigned long)(last - first + 1);
        if (clip_len >= MUSIC_LOOP_MIN_SAMPLES) {
            flags[cmd] = FLAG_LOOP;
            loop_cmds++;
        }

        offsets[cmd] = blob_pos;
        lengths[cmd] = clip_len;
        for (int i = first; i <= last; i++) {
            signed char v = active_sample(capture[i]) ? capture[i] : 0;
            if (fwrite(&v, 1, 1, bin) != 1) die(argv[2]);
        }
        blob_pos += lengths[cmd];
        live_cmds++;
    }
    fclose(bin);

    FILE *asmf = fopen(argv[3], "w");
    if (!asmf) die(argv[3]);
    fputs("\tXDEF\t_c1942_sfx_offsets,_c1942_sfx_lengths,_c1942_sfx_flags,_c1942_sfx_blob\n", asmf);
    fputs("\tSECTION\tdata,DATA\n\tCNOP\t0,4\n_c1942_sfx_offsets:\n", asmf);
    write_dc_l(asmf, offsets);
    fputs("\tCNOP\t0,4\n_c1942_sfx_lengths:\n", asmf);
    write_dc_l(asmf, lengths);
    fputs("\tCNOP\t0,4\n_c1942_sfx_flags:\n", asmf);
    write_dc_b(asmf, flags);
    fputs("\tCNOP\t0,4\n_c1942_sfx_blob:\n\tincbin\t\"c1942_sfx_bank.bin\"\n\tCNOP\t0,4\n\tEND\n", asmf);
    fclose(asmf);

    printf("1942 Paula bank: %d commands, %d looped, %lu bytes\n", live_cmds, loop_cmds, blob_pos);
    return 0;
}
