/* sidearms_audio.c -- Side Arms sound: audio Z80 (bd-06.1l) + 2x YM2203.
 * Capcom's standard 2xYM2203 sound board -- the SAME hardware as 1943/Gun.Smoke,
 * so this is c1943_audio.c adapted to Side Arms's clocks + sound map. Uses the
 * Tatsuyuki-Satoh OPN core (ym/fm.c, HAS_YM2203) for the FM half + a local
 * AY-3-8910 SSG core for the SSG half. Runs on the shared z80.c via the machine's
 * audio-delegation hooks (context == csidearms_audio_z).
 *
 * Sound Z80 map (MAME capcom/sidearms.cpp sound_map):
 *   0x0000-0x7fff ROM (bd-06.1l, 32K), 0xc000-0xc7ff RAM, 0xd000 soundlatch (read),
 *   YM2203 #1 at 0xf000/0xf001 (addr/data), YM2203 #2 at 0xf002/0xf003.
 * Clocks (verified on PCB): sound Z80 @ 3.579545MHz, both YM2203 @ 3.579545MHz.
 * (1943 differs: its sound Z80 = 3.0MHz, its YM2203 = 1.5MHz.) IRQ source is the
 * YM timer (ym1.irq_handler -> audiocpu INPUT_LINE 0); modelled, like 1943, as 4
 * fixed RST38 (IM1) interrupts per video frame. Output -> signed 8-bit PCM for Paula. */
#include "z80emu.h"
#include "driver.h"
#include "fm.h"
#include "sidearms_audio.h"
#include <string.h>

#define AUD_CLOCK         4000000
#define AUD_CYC_PER_FRAME 66667      /* fast 4MHz-equivalent sound Z80 / 60 Hz frame */
#define AUD_IRQ_CHUNKS    4          /* deliver one latch per quarter-frame */
#define AUD_IRQ_PERIOD    (AUD_CYC_PER_FRAME / AUD_IRQ_CHUNKS)
#define AUD_SLICES        16         /* YM timer granularity within one video frame */
#define YM_CLOCK          4000000    /* YM2203 master clock used by the fast Amiga mix */
#define SR                8040       /* 134 samples at 60Hz; close to Paula period 441 */
#define AY_TICKS_PER_SMP  ((YM_CLOCK/8)/SR)

/* machine audio-delegation hooks (sidearms_machine.c routes the audio CPU here) */
extern MY_LITTLE_Z80 *csidearms_audio_z;
extern void          (*csidearms_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v);
extern unsigned char (*csidearms_audio_rd_hook)(MY_LITTLE_Z80 *z, unsigned a);
extern void          (*csidearms_latch_hook)(unsigned char v);

/* optional host-debug trace of YM register writes (NULL in the Amiga build) */
void (*sidearms_audio_ym_trace)(int chip, int ad, unsigned char v) = 0;
/* optional host-debug trace of soundlatch commands posted by the main CPU */
void (*sidearms_latch_trace)(unsigned char v) = 0;

/* ---- soundlatch FIFO (main posts a cmd then 0xff idle within a frame) ---- */
static unsigned char latq[64]; static int latr=0, latw=0;
static void latch_push(unsigned char v){
    if(sidearms_latch_trace) sidearms_latch_trace(v);
    if(v==0xff) return;
    int n=(latw+1)&63;
    if(n!=latr){ latq[latw]=v; latw=n; }
}
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
unsigned sidearms_audio_pc(void){ return aud.state.pc & 0xffff; }

static long tdead[2][2], tperiod[2][2];       /* YM timer deadlines in audio cycles */
static int in_overflow = 0, ym_irq_line = 0;  /* YM1 IRQ line level */
static void fm_timer(int n,int c,int cnt,double stepTime){
    (void)stepTime; if(n<0||n>1||c<0||c>1) return;
    if(cnt <= 0){ tperiod[n][c]=0; tdead[n][c]=0; return; }
    long p=(long)cnt * 72;
    tperiod[n][c]=p;
    if(!in_overflow) tdead[n][c]=p;
}
static void fm_irq(int n,int irq){ if(n==0) ym_irq_line = irq ? 1 : 0; }

static unsigned char aud_rd(MY_LITTLE_Z80 *z, unsigned a){
    a &= 0xffff;
    if (a == 0xd000) return cur_latch;
    if (a >= 0xf000 && a <= 0xf003)
        return ym_inited ? YM2203Read((a & 2) >> 1, a & 1) : 0xff;
    return z->memory[a];                       /* ROM 0-7fff / RAM c000-c7ff */
}
static void aud_wr(MY_LITTLE_Z80 *z, unsigned a, unsigned char v){
    a &= 0xffff;
    if (a >= 0xc000 && a < 0xd000){ z->memory[a] = v; return; }   /* work RAM */
    if (!ym_inited) return;
    switch (a){
        case 0xf000: if(sidearms_audio_ym_trace)sidearms_audio_ym_trace(0,0,v); YM2203Write(0,0,v); break;
        case 0xf001: if(sidearms_audio_ym_trace)sidearms_audio_ym_trace(0,1,v); YM2203Write(0,1,v); break;
        case 0xf002: if(sidearms_audio_ym_trace)sidearms_audio_ym_trace(1,0,v); YM2203Write(1,0,v); break;  /* YM1 address */
        case 0xf003: if(sidearms_audio_ym_trace)sidearms_audio_ym_trace(1,1,v); YM2203Write(1,1,v); break;  /* YM1 data    */
    }
}

/* YM2203 timer IRQ: only YM1 (chip 0) drives the sound-CPU INT line (MAME:
 * ym1.irq_handler -> audiocpu line 0). YM2203TimerOver (called by our external
 * timer) raises the IRQ via FM_STATUS_SET, which calls this on the 0->1 edge; we
 * queue one RST38 per overflow so the driver ticks the music at the timer rate. */
void sidearms_audio_init(const unsigned char *snd)
{
    memset(aud.memory, 0, sizeof aud.memory);
    memcpy(aud.memory, snd, 0x8000);        /* sound ROM 0x0000-0x7fff (bd-06.1l) */
    aud.opcodes = 0; aud.opcodes_len = 0;
    ay_reset(&ssg[0]); ay_reset(&ssg[1]);
    ym_irq_line = 0; in_overflow = 0;
    memset(tdead, 0, sizeof tdead); memset(tperiod, 0, sizeof tperiod);
    if (!ym_inited){
        if (YM2203Init(2, 0, YM_CLOCK, SR, fm_timer, fm_irq) == 0) ym_inited = 1;
    }
    if (ym_inited){ YM2203ResetChip(0); YM2203ResetChip(1); }
    Z80Reset(&aud.state);
    latr = latw = 0; cur_latch = 0xff;
    aud_irq_left = AUD_IRQ_PERIOD;
    aud_cycle_acc = 0;
    /* register with the machine so the shared z80.c routes the audio CPU here */
    csidearms_audio_z       = &aud;
    csidearms_audio_wr_hook = aud_wr;
    csidearms_audio_rd_hook = aud_rd;
    csidearms_latch_hook    = latch_push;
}

/* free the FM core's alloc'd state on exit (else every launch leaks it -- under
 * AGS, which relaunches games, that accumulates toward an out-of-memory guru). */
void sidearms_audio_shutdown(void){ if(ym_inited){ YM2203Shutdown(); ym_inited=0; } }

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

static void run_timers(long cyc){
    for (int chip=0; chip<2; chip++)
        for (int t=0; t<2; t++){
            if (tdead[chip][t] <= 0) continue;
            tdead[chip][t] -= cyc;
            while (tdead[chip][t] <= 0 && tperiod[chip][t] > 0){
                in_overflow = 1; YM2203TimerOver(chip, t); in_overflow = 0;
                tdead[chip][t] += tperiod[chip][t];
            }
        }
}

static void run_audio_cycles(int cycles){
    while(cycles > 0){
        if(aud_irq_left == AUD_IRQ_PERIOD) cur_latch = latch_pop();
        int n = cycles < aud_irq_left ? cycles : aud_irq_left;
        if(n > 0){
            Z80Emulate(&aud.state, n, &aud);
            run_timers(n);
            cycles -= n;
            aud_irq_left -= n;
        }
        if(ym_irq_line) Z80Interrupt(&aud.state, 0xFF, &aud);
        if(aud_irq_left <= 0) aud_irq_left = AUD_IRQ_PERIOD;
    }
}

static void run_audio_samples(int nsamples){
    if(nsamples <= 0) return;
    unsigned long total = (unsigned long)aud_cycle_acc + (unsigned long)nsamples * AUD_CLOCK;
    int cycles = (int)(total / SR);
    aud_cycle_acc = (unsigned)(total % SR);
    run_audio_cycles(cycles);
}

void sidearms_audio_render_samples(signed char *out, int nsamples){
    if(nsamples <= 0) return;
    int done = 0;
    while(done < nsamples){
        int n = nsamples - done;
        if(n > 32) n = 32;
        run_audio_samples(n);
        render(out + done, n);
        done += n;
    }
}

void sidearms_audio_frame(signed char *out, int nsamples){
    sidearms_audio_render_samples(out, nsamples);
}
