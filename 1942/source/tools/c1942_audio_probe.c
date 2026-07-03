#include "z80emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void c1942_load(const unsigned char *maincpu);
extern void c1942_init(MY_LITTLE_Z80 *z);
extern void c1942_run_frame(MY_LITTLE_Z80 *z);
extern void c1942_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2);
extern unsigned c1942_pc(MY_LITTLE_Z80 *z);
extern int c1942_soundlatch(void);

extern void c1942_audio_init(const unsigned char *snd);
extern void c1942_audio_frame(signed char *out, int nsamples);
extern unsigned c1942_audio_pc(void);
extern int c1942_ay_writes;
extern const unsigned char *c1942_ay_regs(int chip);
extern void (*c1942_latch_hook)(unsigned char v);

#ifdef TRACE_LATCH
static int trace_frame;
static void trace_latch(unsigned char v)
{
    printf("latch f=%04d cmd=%02x\n", trace_frame, v);
}
#endif

static void read_exact(const char *path, unsigned char *dst, size_t len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    size_t n = fread(dst, 1, len, f);
    fclose(f);
    if (n != len) {
        fprintf(stderr, "%s: got %zu, expected %zu\n", path, n, len);
        exit(1);
    }
}

static void analyse_pcm(const signed char *pcm, int n, int *minv, int *maxv, long *abs_sum, int *nonzero)
{
    int mn = 127, mx = -128, nz = 0;
    long sum = 0;
    for (int i = 0; i < n; i++) {
        int v = pcm[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        if (v) nz++;
        sum += v < 0 ? -v : v;
    }
    *minv = mn;
    *maxv = mx;
    *abs_sum = sum;
    *nonzero = nz;
}

int main(void)
{
    static unsigned char maincpu[0x20000];
    static unsigned char snd[0x4000];
    static signed char pcm[375];
    MY_LITTLE_Z80 z;

    read_exact("build/r1942/main.bin", maincpu, sizeof maincpu);
    read_exact("build/r1942/snd.bin", snd, sizeof snd);

    c1942_load(maincpu);
    c1942_init(&z);
#ifndef TRACE_LATCH
    c1942_audio_init(snd);
#else
    (void)snd;
    c1942_latch_hook = trace_latch;
#endif

    for (int f = 0; f < 1200; f++) {
#ifdef TRACE_LATCH
        trace_frame = f;
#endif
        unsigned char sys = 0xff;
        if (f >= 120 && f < 132) sys &= ~(0x80 | 0x40);
        if (f >= 180 && f < 240) sys &= ~0x01;
        c1942_set_inputs(sys, 0xff, 0xff);

        c1942_run_frame(&z);
#ifdef TRACE_LATCH
        continue;
#endif
        int before = c1942_ay_writes;
        c1942_audio_frame(pcm, 375);

        int mn, mx, nz;
        long abs_sum;
        analyse_pcm(pcm, 375, &mn, &mx, &abs_sum, &nz);
        if ((f % 60) == 0 || c1942_ay_writes != before || nz) {
            const unsigned char *a0 = c1942_ay_regs(0);
            const unsigned char *a1 = c1942_ay_regs(1);
            printf("f=%04d mainpc=%04x audpc=%04x latch=%02x ayw=%d +%d pcm=%d..%d nz=%d abs=%ld ay0v=%02x/%02x/%02x ay1v=%02x/%02x/%02x\n",
                   f, c1942_pc(&z), c1942_audio_pc(), c1942_soundlatch() & 0xff,
                   c1942_ay_writes, c1942_ay_writes - before,
                   mn, mx, nz, abs_sum,
                   a0[8], a0[9], a0[10], a1[8], a1[9], a1[10]);
        }
    }

    return 0;
}
