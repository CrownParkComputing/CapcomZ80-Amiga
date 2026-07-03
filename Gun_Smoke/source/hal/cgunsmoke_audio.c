/* src/hal/cgunsmoke_audio.c -- Gun.Smoke sound: audio Z80 (gs02.14h) + 2x YM2203.
 * Same Capcom sound board as 1943 / Commando -- reuses the Tatsuyuki-Satoh OPN core
 * (src/cores/ym/fm.c, BUILD_YM2203) + the local AY-3-8910 SSG core verbatim; only
 * the audio-CPU memory map differs.
 *
 * Audio Z80 map (MAME capcom/gunsmoke.cpp sound_map): 0x0000-7fff ROM (gs02.14h 32K),
 * 0xc000-c7ff RAM, 0xc800 soundlatch (read), YM0 0xe000(addr)/0xe001(data),
 * YM1 0xe002/0xe003. Runs on the shared z80.c via the global machine_rd/wr, routed
 * here by context == gunsmoke_audio_z. Output -> signed 8-bit PCM for Paula.
 * Sound IRQ: irq0_line_hold 4x/frame (RST38) -- gunsmoke set_periodic_int(4*60).
 */
#include "z80emu.h"
#include "driver.h"     /* OPN core types + logerror stub */
#include "fm.h"
#include <string.h>

#define AUD_CLOCK         3000000    /* 3 MHz audio Z80 */
#define AUD_CYC_PER_FRAME 50000      /* 3 MHz audio Z80 / 60 Hz arcade frame -- MAME gunsmoke is
                                      * 59.64 Hz, 4 IRQs/frame @ ~12500 cyc/chunk; SAME as 1943 */
#define AUD_IRQ_CHUNKS    4          /* sound CPU IRQ 4x/frame */
#define AUD_IRQ_PERIOD    (AUD_CYC_PER_FRAME / AUD_IRQ_CHUNKS)
#define AY_TICKS_PER_SMP  23         /* SSG tone clock/8 = 187500; 187500/8040 ~= 23 (== 1943) */
#define SR                8040       /* 134 samples at 60 Hz; Paula period 441 (== 1943) */

/* delegation hooks exported by cgunsmoke.c */
extern MY_LITTLE_Z80 *gunsmoke_audio_z;
extern void          (*gunsmoke_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v);
extern unsigned char (*gunsmoke_audio_rd_hook)(MY_LITTLE_Z80 *z, unsigned a);
extern void          (*gunsmoke_latch_hook)(unsigned char v);

/* ---- soundlatch FIFO (main posts a cmd then 0xff idle within a frame) ---- */
int gunsmoke_audio_dbg_latch = 0, gunsmoke_audio_dbg_ymw = 0;   /* host-verify counters */
static unsigned char latq[64]; static int latr=0, latw=0;
static void latch_push(unsigned char v){ if(v==0xff) return; gunsmoke_audio_dbg_latch++; int n=(latw+1)&63; if(n!=latr){ latq[latw]=v; latw=n; } }
static unsigned char latch_pop(void){ if(latr==latw) return 0xff; unsigned char v=latq[latr]; latr=(latr+1)&63; return v; }

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
static unsigned char cur_latch = 0xff;
static int ym_inited = 0;
static int aud_irq_left = AUD_IRQ_PERIOD;
static unsigned aud_cycle_acc = 0;
#ifdef GUNSMOKE_AUDIO_NATIVE
extern void aud_run(void *state);
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
unsigned gunsmoke_audio_pc(void){ return AU16(AO_PC); }
#else
unsigned gunsmoke_audio_pc(void){ return aud.state.pc & 0xffff; }
#endif

static unsigned char aud_rd(MY_LITTLE_Z80 *z, unsigned a){
    a &= 0xffff;
    if (a == 0xc800) return cur_latch;        /* soundlatch */
    return z->memory[a];                       /* ROM 0-7fff / RAM c000-c7ff */
}
static void aud_wr(MY_LITTLE_Z80 *z, unsigned a, unsigned char v){
    a &= 0xffff;
    if (a >= 0xc000 && a < 0xc800){ z->memory[a] = v; return; }   /* work RAM */
    if (!ym_inited) return;
    if (a>=0xe000 && a<=0xe003) gunsmoke_audio_dbg_ymw++;
    switch (a){
        case 0xe000: YM2203Write(0,0,v); break;   /* YM0 address */
        case 0xe001: YM2203Write(0,1,v); break;   /* YM0 data    */
        case 0xe002: YM2203Write(1,0,v); break;   /* YM1 address */
        case 0xe003: YM2203Write(1,1,v); break;   /* YM1 data    */
    }
}

#ifdef GUNSMOKE_AUDIO_NATIVE
/* C hooks the Rust prelude's rd()/wr() traps call. The prelude reads/writes work RAM
 * (0xc000-0xc7ff) and ROM straight through s.mem (== aud.memory); only the soundlatch
 * READ (0xc800) and the four YM2203 writes (0xe000-0xe003) forward here. Unlike 1943,
 * Gun.Smoke pops the latch FIFO ONCE per IRQ chunk (in gunsmoke_audio_frame) and serves
 * 0xc800 from cur_latch, so this hook returns cur_latch WITHOUT popping. */
unsigned char aud_gunsmoke_aud_latch(void)
{
    return cur_latch;
}

void aud_gunsmoke_aud_ym(unsigned long a, unsigned char v)
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
static int  ym_irq = 0;
static void fm_irq(int n,int irq){ (void)n; if(irq) ym_irq = 1; }

void gunsmoke_audio_init(const unsigned char *snd){
#ifdef GUNSMOKE_AUDIO_NATIVE
    aud_native_reset(snd);
#else
    memset(aud.memory, 0, sizeof aud.memory);
    memcpy(aud.memory, snd, 0x8000);        /* sound ROM 0x0000-0x7fff (gs02.14h) */
#endif
    ay_reset(&ssg[0]); ay_reset(&ssg[1]);
    if (!ym_inited){
        if (YM2203Init(2, 0, 1500000, SR, fm_timer, fm_irq) == 0) ym_inited = 1;
    }
    if (ym_inited){ YM2203ResetChip(0); YM2203ResetChip(1); }
#ifndef GUNSMOKE_AUDIO_NATIVE
    Z80Reset(&aud.state);
#endif
    latr = latw = 0; cur_latch = 0xff; ym_irq = 0;
    aud_irq_left = AUD_IRQ_PERIOD;
    aud_cycle_acc = 0;
    gunsmoke_audio_z       = &aud;
    gunsmoke_audio_wr_hook = aud_wr;
    gunsmoke_audio_rd_hook = aud_rd;
    gunsmoke_latch_hook    = latch_push;
}

#define CHUNK 512
static void render(signed char *out, int n){
    short fm0[CHUNK], fm1[CHUNK];
    int done=0;
    while (done < n){
        int m = n-done; if (m>CHUNK) m=CHUNK;
        if (ym_inited){ YM2203UpdateOne(0, fm0, m); YM2203UpdateOne(1, fm1, m); }
        else { memset(fm0,0,sizeof fm0); memset(fm1,0,sizeof fm1); }
        for (int i=0;i<m;i++){
            for (int t=0;t<AY_TICKS_PER_SMP;t++){ ay_tick(&ssg[0]); ay_tick(&ssg[1]); }
            int ssgv = ay_amp(&ssg[0]) + ay_amp(&ssg[1]);   /* 0..252 (unipolar) */
            int fmv  = (int)fm0[i] + (int)fm1[i];
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
    while (cycles > 0) {
        if (aud_irq_left == AUD_IRQ_PERIOD)
            cur_latch = latch_pop();

        int n = cycles < aud_irq_left ? cycles : aud_irq_left;
        if (n > 0) {
#ifdef GUNSMOKE_AUDIO_NATIVE
            ASETU32(AO_CYCLES, 0);
            ASETU32(AO_BUDGET, n);
            aud_run(aud_st);
#else
            Z80Emulate(&aud.state, n, &aud);
#endif
            cycles -= n;
            aud_irq_left -= n;
        }

        if (aud_irq_left <= 0) {
#ifdef GUNSMOKE_AUDIO_NATIVE
            aud_native_irq38();
#else
            Z80Interrupt(&aud.state, 0xFF, &aud);    /* IM1 -> RST 38h */
#endif
            aud_irq_left = AUD_IRQ_PERIOD;
        }
    }
}

static void run_audio_samples(int nsamples)
{
    if (nsamples <= 0) return;
    unsigned long total = (unsigned long)aud_cycle_acc + (unsigned long)nsamples * AUD_CLOCK;
    int cycles = (int)(total / SR);
    aud_cycle_acc = (unsigned)(total % SR);
    run_audio_cycles(cycles);
}

/* Render PCM and advance the sound board by the exact audio time emitted. */
void gunsmoke_audio_render_samples(signed char *out, int nsamples)
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

/* Legacy fixed-frame entry point for callers that still step at 60 Hz. */
void gunsmoke_audio_frame(signed char *out, int nsamples)
{
    gunsmoke_audio_render_samples(out, nsamples);
}

void gunsmoke_audio_shutdown(void)
{
    if (ym_inited) {
        YM2203Shutdown();
        ym_inited = 0;
    }
    gunsmoke_audio_z = 0;
    gunsmoke_audio_wr_hook = 0;
    gunsmoke_audio_rd_hook = 0;
    gunsmoke_latch_hook = 0;
}
