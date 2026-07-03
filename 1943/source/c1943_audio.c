/* c1943_audio.c -- 1943 sound: audio Z80 (bm05.4k) + 2x YM2203.
 * Identical Capcom sound board to Gun.Smoke / Commando -- adapted from
 * cgunsmoke_audio.c. Uses the Tatsuyuki-Satoh OPN core (ym/fm.c, HAS_YM2203) for
 * the FM half + a local AY-3-8910 SSG core for the SSG half. The sound Z80 runs
 * as a Rust-generated native m68k transcode in the RTG package.
 *
 * Audio Z80 map (MAME capcom/1943.cpp sound_map): 0x0000-7fff ROM (bm05.4k 32K),
 * 0xc000-c7ff RAM, 0xc800 soundlatch (read), 0xd800 MCU comm (returns 0 -- the
 * 1943b bootleg has no MCU), YM0 0xe000/0xe001, YM1 0xe002/0xe003. Output ->
 * signed 8-bit PCM for Paula. Sound IRQ: 4x/frame, RST38. The current RTG
 * package builds this with C1943_AUDIO_NATIVE, so the sound Z80 is also a
 * Rust-generated native m68k transcode.
 */
#ifdef C1943_AUDIO_SPLIT
#define Z80Reset c1943_audio_Z80Reset
#define Z80Interrupt c1943_audio_Z80Interrupt
#define Z80NonMaskableInterrupt c1943_audio_Z80NonMaskableInterrupt
#define Z80Emulate c1943_audio_Z80Emulate
#define Z80BuildQuick c1943_audio_Z80BuildQuick
#endif
#include "z80emu.h"
#include "driver.h"
#include "fm.h"
#include <string.h>

#define AUD_CLOCK         3000000    /* 3 MHz audio Z80 */
#define AUD_CYC_PER_FRAME 50000      /* 3 MHz audio Z80 / 60 Hz arcade frame */
#define AUD_IRQ_CHUNKS    4          /* sound CPU IRQ 4x/frame */
#define AUD_IRQ_PERIOD    (AUD_CYC_PER_FRAME / AUD_IRQ_CHUNKS)
#define AY_TICKS_PER_SMP  23         /* SSG tone clock/8 = 187500; /8040 ~= 23 */
#define SR                8040       /* 134 samples at 60Hz; close to Paula period 441
                                      * than 12500 (the big audio CPU lever); fine for arcade FM */
/* Keep the YM2203 FM path full-rate. Half-rate FM was fast, but audibly wrong. */
#define FM_DIV            1
#define FM_SR             (SR / FM_DIV)

/* machine audio-delegation hooks (the native main-CPU bridge routes latches here) */
extern MY_LITTLE_Z80 *c1943_audio_z;
extern void          (*c1943_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v);
extern unsigned char (*c1943_audio_rd_hook)(MY_LITTLE_Z80 *z, unsigned a);
extern void          (*c1943_latch_hook)(unsigned char v);

/* ---- soundlatch FIFO ----
 * The main CPU writes real commands interleaved with 0xff clears. Keep the clear
 * strobe, because the sound ROM uses it to accept repeated commands, but compress
 * consecutive 0xff writes so idle frames cannot bury music/SFX commands. */
static unsigned char cur_latch = 0xff, last_queued_latch = 0xff;
static unsigned char latq[64]; static int latr=0, latw=0;
static void latch_push(unsigned char v){
    if (v == 0xff && last_queued_latch == 0xff) return;
    int n=(latw+1)&63;
    if(n!=latr){ latq[latw]=v; latw=n; last_queued_latch=v; }
}
static int latch_pop(unsigned char *out){ if(latr==latw) return 0; *out=latq[latr]; latr=(latr+1)&63; return 1; }
static unsigned char latch_read(void){ unsigned char v; if(latch_pop(&v)) cur_latch=v; return cur_latch; }

void c1943_audio_command(unsigned char v)
{
    latch_push(v);
    if (v != 0xff) latch_push(0xff);
}

/* ---------------- AY-3-8910 SSG core (one per YM2203) ---------------- */
typedef struct {
    unsigned char reg[16];
    unsigned tcnt[3], tper[3];   unsigned char tout[3];
    unsigned ncnt, nper, nrng;   unsigned char nout;
    unsigned ecnt, eper;         unsigned char estep, eatt, evol;
} AY;
static const unsigned char ay_vol[16] = { 0,1,2,3,4,5,7,10,13,18,24,30,36,40,42,42 };
static void ay_reset(AY *a){ memset(a,0,sizeof *a); a->nrng=1; a->tper[0]=a->tper[1]=a->tper[2]=1; a->nper=1; a->eper=1; }
static void ay_write(AY *a, unsigned r, unsigned char v){
    r &= 15; a->reg[r] = v;
    switch (r) {
        case 0: case 1: a->tper[0]=(a->reg[0]|((a->reg[1]&0x0f)<<8)); if(!a->tper[0])a->tper[0]=1; break;
        case 2: case 3: a->tper[1]=(a->reg[2]|((a->reg[3]&0x0f)<<8)); if(!a->tper[1])a->tper[1]=1; break;
        case 4: case 5: a->tper[2]=(a->reg[4]|((a->reg[5]&0x0f)<<8)); if(!a->tper[2])a->tper[2]=1; break;
        case 6: a->nper=v&0x1f; if(!a->nper)a->nper=1; break;
        case 11: case 12: a->eper=(a->reg[11]|(a->reg[12]<<8)); if(!a->eper)a->eper=1; break;
        case 13: a->estep=0; a->ecnt=0; a->eatt=(v>>2)&1; a->evol=a->eatt?0:15; break;
    }
}
static void ay_tick(AY *a){
    for (int c=0;c<3;c++){ if(++a->tcnt[c] >= a->tper[c]){ a->tcnt[c]=0; a->tout[c]^=1; } }
    if (++a->ncnt >= a->nper*2u){ a->ncnt=0;
        a->nrng=(a->nrng>>1)|(((a->nrng^(a->nrng>>3))&1)<<16); a->nout=a->nrng&1; }
    if (++a->ecnt >= a->eper*32u){ a->ecnt=0;
        unsigned char cont=a->reg[13]&8, alt=a->reg[13]&2, hold=a->reg[13]&1;
        if (a->estep<16){ a->evol=a->eatt?a->estep:15-a->estep; a->estep++; }
        else if (!cont){ a->evol=0; }
        else if (hold){ a->evol=(a->eatt^(alt?1:0))?15:0; }
        else { if(alt) a->eatt^=1; a->estep=0; a->evol=a->eatt?0:15; }
    }
}
static int ay_amp(AY *a){
    int s=0; unsigned char mix=a->reg[7];
    for (int c=0;c<3;c++){
        int tone=(mix>>c)&1, noise=(mix>>(c+3))&1;
        int on=(tone||a->tout[c])&&(noise||a->nout);
        if (!on) continue;
        int v=(a->reg[8+c]&0x10)?a->evol:(a->reg[8+c]&0x0f);
        s += ay_vol[v&15];
    }
    return s;   /* 0..126 */
}

/* ---- the AY8910* shim fm.c calls for the SSG half of each YM2203 ---- */
static AY ssg[2]; static int ssg_lat[2];
int  ay8910_index_ym = 0;
void AY8910Write(int chip,int addr,int v){ if(addr==0) ssg_lat[chip]=v&15; else ay_write(&ssg[chip&1], ssg_lat[chip&1], (unsigned char)v); }
int  AY8910Read(int chip){ return ssg[chip&1].reg[ssg_lat[chip&1]]; }
void AY8910Reset(int chip){ ay_reset(&ssg[chip&1]); ssg_lat[chip&1]=0; }
void AY8910_set_clock(int chip,int clock){ (void)chip;(void)clock; }
double timer_get_time(void){ return 0.0; }
void   BurnYM2203UpdateRequest(void){ }

/* ---------------- audio Z80 ---------------- */
static MY_LITTLE_Z80 aud;
static int ym_inited = 0;
static unsigned aud_cycle_acc = 0;
static int aud_irq_left = AUD_IRQ_PERIOD;

#ifdef C1943_AUDIO_NATIVE
extern void aud_run(void *state);
extern void c1943_flush_pending_latches(void);
static unsigned char aud_st[48];
#define AU8(o)   (aud_st[o])
#define AU16(o)  ((unsigned short)((aud_st[o] << 8) | aud_st[(o)+1]))
#define AU32(o)  ((unsigned long)(((unsigned long)aud_st[o]<<24)|((unsigned long)aud_st[(o)+1]<<16)|((unsigned long)aud_st[(o)+2]<<8)|aud_st[(o)+3]))
#define ASETU16(o,v) do{ aud_st[o]=(unsigned char)((v)>>8); aud_st[(o)+1]=(unsigned char)(v); }while(0)
#define ASETU32(o,v) do{ aud_st[o]=(unsigned char)((v)>>24); aud_st[(o)+1]=(unsigned char)((v)>>16); aud_st[(o)+2]=(unsigned char)((v)>>8); aud_st[(o)+3]=(unsigned char)(v); }while(0)
#define AO_SP     12
#define AO_PC     14
#define AO_IFF1   26
#define AO_HALTED 29
#define AO_STOP   30
#define AO_CYCLES 32
#define AO_BUDGET 36
void aud_abort(void) { for (;;) {} }
unsigned c1943_audio_pc(void){ return AU16(AO_PC); }
#else
unsigned c1943_audio_pc(void){ return aud.state.pc & 0xffff; }
#endif

static unsigned char aud_rd(MY_LITTLE_Z80 *z, unsigned a){
    a &= 0xffff;
    if (a == 0xc800) return latch_read();     /* soundlatch */
    if (a == 0xd800) return 0x00;             /* MCU comm (no MCU in bootleg) */
    return z->memory[a];                       /* ROM 0-7fff / RAM c000-c7ff */
}
static void aud_wr(MY_LITTLE_Z80 *z, unsigned a, unsigned char v){
    a &= 0xffff;
    if (a >= 0xc000 && a < 0xc800){ z->memory[a] = v; return; }   /* work RAM */
    if (a == 0xd800) return;                                       /* MCU comm: ignore */
    if (!ym_inited) return;
    switch (a){
        case 0xe000: YM2203Write(0,0,v); break;   /* YM0 address */
        case 0xe001: YM2203Write(0,1,v); break;   /* YM0 data    */
        case 0xe002: YM2203Write(1,0,v); break;   /* YM1 address */
        case 0xe003: YM2203Write(1,1,v); break;   /* YM1 data    */
    }
}

#ifdef C1943_AUDIO_NATIVE
unsigned char aud_c1943_aud_latch(void)
{
    return latch_read();
}

void aud_c1943_aud_ym(unsigned long a, unsigned char v)
{
    aud_wr(&aud, (unsigned)a, v);
}

static void aud_native_reset(const unsigned char *snd)
{
    memset(aud.memory, 0, sizeof aud.memory);
    memcpy(aud.memory, snd, 0x8000);
    memset(aud_st, 0, sizeof aud_st);
    AU8(6) = 0xff; AU8(7) = 0xff;          /* A,F reset */
    ASETU16(AO_SP, 0xffff);
    ASETU16(AO_PC, 0x0000);
    ASETU32(AO_BUDGET, 0);
    ASETU32(44, (unsigned long)aud.memory);
}

static void aud_native_irq38(void)
{
    if (!AU8(AO_IFF1)) return;
    unsigned short pc = AU16(AO_PC);
    unsigned short sp = (unsigned short)(AU16(AO_SP) - 2);
    aud.memory[sp] = (unsigned char)pc;
    aud.memory[(unsigned short)(sp + 1)] = (unsigned char)(pc >> 8);
    ASETU16(AO_SP, sp);
    AU8(AO_IFF1) = 0;
    AU8(27) = 0;
    AU8(AO_HALTED) = 0;
    AU8(25) = (AU8(25) & 0x80) | ((AU8(25) + 1) & 0x7f);
    ASETU16(AO_PC, 0x0038);
}
#endif

static void fm_timer(int n,int c,int cnt,double stepTime){ (void)n;(void)c;(void)cnt;(void)stepTime; }
static void fm_irq(int n,int irq){ (void)n;(void)irq; }

void c1943_audio_init(const unsigned char *snd)
{
#ifdef C1943_AUDIO_NATIVE
    aud_native_reset(snd);
#else
    memset(aud.memory, 0, sizeof aud.memory);
    memcpy(aud.memory, snd, 0x8000);        /* sound ROM 0x0000-0x7fff (bm05.4k) */
    aud.opcodes = 0; aud.opcodes_len = 0;
#ifdef C1943_Z80_QUICKENED
    aud.quick_enabled = 0;
    memset(aud.quick_valid, 0, sizeof aud.quick_valid);
    Z80BuildQuick(&aud, 0x0000, 0x8000);
#endif
#endif
    ay_reset(&ssg[0]); ay_reset(&ssg[1]);
    if (!ym_inited){
        if (YM2203Init(2, 0, 1500000, FM_SR, fm_timer, fm_irq) == 0) ym_inited = 1;
    }
    if (ym_inited){ YM2203ResetChip(0); YM2203ResetChip(1); }
#ifndef C1943_AUDIO_NATIVE
    Z80Reset(&aud.state);
#endif
    latr = latw = 0; cur_latch = 0xff; last_queued_latch = 0xff;
    aud_cycle_acc = 0;
    aud_irq_left = AUD_IRQ_PERIOD;
    /* register with the machine bridge so soundlatch writes route here */
    c1943_audio_z       = &aud;
    c1943_audio_wr_hook = aud_wr;
    c1943_audio_rd_hook = aud_rd;
    c1943_latch_hook    = latch_push;
    c1943_flush_pending_latches();
}

/* free the FM core's AllocMem'd state on exit (else every launch leaks it -- under
 * AGS, which relaunches games, that accumulates toward an out-of-memory guru). */
void c1943_audio_shutdown(void){ if(ym_inited){ YM2203Shutdown(); ym_inited=0; } }

#define CHUNK 512
static void render(signed char *out, int n){
    short fm0[CHUNK], fm1[CHUNK];
    int done=0;
    while (done < n){
        int m = n-done; if (m>CHUNK) m=CHUNK;
        int fm_m = (m + FM_DIV-1)/FM_DIV;                  /* half-rate FM sample count */
        if (ym_inited){ YM2203UpdateOne(0, fm0, fm_m); YM2203UpdateOne(1, fm1, fm_m); }
        else { memset(fm0,0,(unsigned)fm_m*sizeof(short)); memset(fm1,0,(unsigned)fm_m*sizeof(short)); }
        for (int i=0;i<m;i++){
            for (int t=0;t<AY_TICKS_PER_SMP;t++){ ay_tick(&ssg[0]); ay_tick(&ssg[1]); }
            int ssgv = ay_amp(&ssg[0]) + ay_amp(&ssg[1]);   /* 0..252 (unipolar) */
            int fi = i / FM_DIV; if (fi >= fm_m) fi = fm_m-1;   /* nearest-upsample the FM */
            int fmv  = (int)fm0[fi] + (int)fm1[fi];
            int s = (fmv / 320) + (ssgv / 3);
            if (s >  100) s = 100 + (s-100)*27/256;
            if (s < -100) s = -100 + (s+100)*27/256;
            if (s >  127) s =  127; if (s < -127) s = -127;
            out[done+i] = (signed char)s;
        }
        done += m;
    }
}

static void run_audio_cycles(int cycles)
{
#ifdef C1943_AUDIO_NATIVE
    while (cycles > 0) {
        int n = cycles < aud_irq_left ? cycles : aud_irq_left;
        if (n > 0) {
            unsigned long target = AU32(AO_CYCLES) + (unsigned long)n;
            ASETU32(AO_BUDGET, target);
            while (AU32(AO_CYCLES) < target) {
                unsigned long before = AU32(AO_CYCLES);
                aud_run(aud_st);
                if (AU32(AO_CYCLES) == before || AU8(AO_STOP) == 1 || AU8(AO_STOP) == 2) {
                    ASETU32(AO_CYCLES, target);     /* HALT/idling: time still passes until IRQ */
                    break;
                }
            }
            cycles -= n;
            aud_irq_left -= n;
        }
        if (aud_irq_left <= 0) {
            aud_native_irq38();
            aud_irq_left = AUD_IRQ_PERIOD;
        }
    }
#else
    while (cycles > 0) {
        int n = cycles < aud_irq_left ? cycles : aud_irq_left;
        if (n > 0) {
            Z80Emulate(&aud.state, n, &aud);
            cycles -= n;
            aud_irq_left -= n;
        }
        if (aud_irq_left <= 0) {
            Z80Interrupt(&aud.state, 0xFF, &aud);    /* IM1 -> RST 38h */
            aud_irq_left = AUD_IRQ_PERIOD;
        }
    }
#endif
}

static void run_audio_samples(int nsamples)
{
    if (nsamples <= 0) return;
    unsigned long total = aud_cycle_acc + (unsigned long)nsamples * AUD_CLOCK;
    int cycles = (int)(total / SR);
    aud_cycle_acc = (unsigned)(total % SR);
    run_audio_cycles(cycles);
}

/* Legacy fixed-frame entry point for non-ring callers. */
void c1943_audio_tick_frame(void)
{
    run_audio_cycles(AUD_CYC_PER_FRAME);
}

/* Render PCM and advance the sound board by the exact amount of audio time emitted.
 * This keeps music pitch stable when the RTG/input frame time jitters slightly. */
void c1943_audio_render_samples(signed char *out, int nsamples)
{
    if (nsamples <= 0) return;
    int done = 0;
    while (done < nsamples) {
        int n = nsamples - done;
        if (n > 32) n = 32;
        run_audio_samples(n);
        render(out + done, n);
        done += n;
    }
}

/* Run one video frame of the audio CPU in the board's native 4x IRQ cadence,
 * then render that frame's PCM. The music driver advances on these quarterly
 * RST38 interrupts, so keep this path frame-synchronous. */
void c1943_audio_frame(signed char *out, int nsamples){
    int done = 0;
    for (int k = 0; k < AUD_IRQ_CHUNKS; k++) {
#ifdef C1943_AUDIO_NATIVE
        ASETU32(AO_CYCLES, 0);
        ASETU32(AO_BUDGET, AUD_IRQ_PERIOD);
        aud_run(aud_st);
        aud_native_irq38();
#else
        Z80Emulate(&aud.state, AUD_IRQ_PERIOD, &aud);
        Z80Interrupt(&aud.state, 0xFF, &aud);
#endif
        int n = (k == AUD_IRQ_CHUNKS - 1) ? (nsamples - done) : (nsamples / AUD_IRQ_CHUNKS);
        if (n > 0) {
            render(out + done, n);
            done += n;
        }
    }
}
