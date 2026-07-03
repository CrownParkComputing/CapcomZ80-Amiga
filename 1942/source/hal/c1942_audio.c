/* src/hal/c1942_audio.c -- 1942 sound: audio Z80 (sr-01.c11) + 2x AY-3-8910,
 * mixed to signed-8-bit PCM for Amiga Paula. Portable C (no Amiga headers); the
 * Paula DMA layer is src/hal/c1942_audio_amiga.c.
 *
 * Audio Z80 map: 0x0000-3fff ROM, 0x4000-47ff RAM, 0x6000 soundlatch (read),
 * AY1 0x8000(addr)/0x8001(data), AY2 0xc000/0xc001. It runs on the shared z80.c
 * via the SAME global machine_rd/wr (routed to us by context == c1942_audio_z).
 * z80.c's fast paths: reads <0xa000 hit memory[] (so ROM/RAM/latch are served
 * from memory[] -- we keep memory[0x6000]=latch); writes 0x8000-0x9fff hit
 * memory[] too, so AY1 can't be trapped via machine_wr -- instead we capture all
 * AY register writes (both chips) through z80.c's WR_LOG_HOOK (wr_log_hook),
 * which fires on every write before the split. Only the audio CPU writes those
 * addresses, and we gate on g_audio_active for safety. */
#include "z80emu.h"
#include <string.h>

#define AUD_CYC_PER_FRAME 50000      /* 3 MHz / 60 */
#define AUD_IRQ_CHUNKS    4          /* approx audio IRQs per frame */
#define AY_TICKS_PER_SMP  10         /* AY tone counter clocked at clock/8 = 187500Hz
                                      * (tone = clock/(16*period)); 187500/18750 = 10.
                                      * Was clock/16 -> an octave too low ("low tone"). */

extern int c1942_soundlatch(void);
/* delegation hooks exported by c1942.c */
extern MY_LITTLE_Z80 *c1942_audio_z;
extern void (*c1942_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v);
extern void (*c1942_latch_hook)(unsigned char v);

/* Soundlatch command FIFO: the main CPU posts a command then writes 0xff (idle)
 * within the same frame, so an end-of-frame snapshot misses it. We queue every
 * non-0xff write and hand one to the audio CPU per IRQ chunk. */
static unsigned char latq[64]; static int latr=0, latw=0;
static void latch_push(unsigned char v)
{
    if (v == 0xff) return;
    /* The first live-game start cue (06) is weak/missing on the Amiga AY path,
     * while the later/restart music command (0c) plays correctly. Route the
     * initial start cue through that same music path so first start and restart
     * behave consistently. */
    if (v == 0x06) v = 0x0c;
    int n = (latw + 1) & 63;
    if (n != latr) { latq[latw] = v; latw = n; }
}
static unsigned char latch_pop(void){ if(latr==latw) return 0xff; unsigned char v=latq[latr]; latr=(latr+1)&63; return v; }

/* ---------------- AY-3-8910 ---------------- */
typedef struct {
    unsigned char reg[16];
    unsigned tcnt[3], tper[3];   unsigned char tout[3];
    unsigned ncnt, nper, nrng;   unsigned char nout;
    unsigned ecnt, eper;         unsigned char estep, eatt, evol;
} AY;

/* 16-level logarithmic DAC, scaled so one channel max = 42 (6 ch sum < 256). */
static const unsigned char ay_vol[16] = {
    0,1,2,3,4,5,7,10,13,18,24,30,36,40,42,42 };

static void ay_reset(AY *a){ memset(a,0,sizeof *a); a->nrng=1; a->tper[0]=a->tper[1]=a->tper[2]=1; a->nper=1; a->eper=1; }

static void ay_write(AY *a, unsigned r, unsigned char v){
    r &= 15; a->reg[r] = v;
    switch (r) {
        case 0: case 1: a->tper[0] = (a->reg[0] | ((a->reg[1]&0x0f)<<8)); if(!a->tper[0])a->tper[0]=1; break;
        case 2: case 3: a->tper[1] = (a->reg[2] | ((a->reg[3]&0x0f)<<8)); if(!a->tper[1])a->tper[1]=1; break;
        case 4: case 5: a->tper[2] = (a->reg[4] | ((a->reg[5]&0x0f)<<8)); if(!a->tper[2])a->tper[2]=1; break;
        case 6: a->nper = v & 0x1f; if(!a->nper)a->nper=1; break;
        case 11: case 12: a->eper = (a->reg[11] | (a->reg[12]<<8)); if(!a->eper)a->eper=1; break;
        case 13: a->estep=0; a->ecnt=0; a->eatt=(v>>2)&1; a->evol=a->eatt?0:15; break;
    }
}

/* one clock/8 tick (187500Hz). tone toggles every period -> clock/(16*period). */
static void ay_tick(AY *a){
    for (int c=0;c<3;c++){ if(++a->tcnt[c] >= a->tper[c]){ a->tcnt[c]=0; a->tout[c]^=1; } }
    if (++a->ncnt >= a->nper*2u){ a->ncnt=0;   /* noise clocked at clock/16 = base/2 */
        a->nrng = (a->nrng>>1) | (((a->nrng ^ (a->nrng>>3)) & 1) << 16);
        a->nout = a->nrng & 1; }
    /* envelope clocked at clock/256 = every 32 clock/8 ticks, /eper */
    if (++a->ecnt >= a->eper*32u){ a->ecnt=0;
        unsigned char cont=a->reg[13]&8, alt=a->reg[13]&2, hold=a->reg[13]&1;
        if (a->estep < 16){ a->evol = a->eatt ? a->estep : 15-a->estep; a->estep++; }
        else if (!cont){ a->evol=0; }
        else if (hold){ a->evol = (a->eatt ^ (alt?1:0)) ? 15 : 0; }
        else { if(alt) a->eatt^=1; a->estep=0; a->evol = a->eatt?0:15; }
    }
}

/* mixed amplitude of one chip (0..126) */
static int ay_amp(AY *a){
    int s=0; unsigned char mix=a->reg[7];
    for (int c=0;c<3;c++){
        int tone = (mix>>c)&1;          /* 1 = tone disabled */
        int noise= (mix>>(c+3))&1;      /* 1 = noise disabled */
        int on = (tone || a->tout[c]) && (noise || a->nout);
        if (!on) continue;
        int v = (a->reg[8+c]&0x10) ? a->evol : (a->reg[8+c]&0x0f);
        s += ay_vol[v & 15];
    }
    return s;
}

/* ---------------- audio Z80 ---------------- */
static MY_LITTLE_Z80 aud;
static AY ay1, ay2;
static unsigned char ay1_lat, ay2_lat;
static volatile int g_audio_active = 0;
int c1942_ay_writes = 0;                 /* host-test introspection */
unsigned c1942_audio_pc(void){ return aud.state.pc & 0xffff; }
const unsigned char *c1942_ay_regs(int chip){ return chip ? ay2.reg : ay1.reg; }

/* called by z80.c on EVERY write (WR_LOG_HOOK) -- capture AY register writes */
void wr_log_hook(unsigned int a, unsigned char v, unsigned int pc){
    (void)pc;
    if (!g_audio_active) return;
    switch (a & 0xffff){
        case 0x8000: ay1_lat = v; break;
        case 0x8001: ay_write(&ay1, ay1_lat, v); c1942_ay_writes++; break;
        case 0xc000: ay2_lat = v; break;
        case 0xc001: ay_write(&ay2, ay2_lat, v); c1942_ay_writes++; break;
    }
}

/* audio CPU RAM writes (0x4000-0x47ff) route here via machine_wr delegation */
static void aud_wr(MY_LITTLE_Z80 *z, unsigned a, unsigned char v){
    a &= 0xffff;
    if (a >= 0x4000 && a < 0x4800) z->memory[a] = v;   /* work RAM */
    /* AY ports land in memory[] (0x8000-9fff) or here (0xc000); captured by hook */
    else if (a >= 0x8000) z->memory[a & 0xffff] = v;
}

void c1942_audio_init(const unsigned char *snd){
    memset(aud.memory, 0, sizeof aud.memory);
    memcpy(aud.memory, snd, 0x4000);        /* sound ROM 0x0000-0x3fff */
    ay_reset(&ay1); ay_reset(&ay2);
    Z80Reset(&aud.state);
    latr = latw = 0;
    c1942_audio_z = &aud;
    c1942_audio_wr_hook = aud_wr;
    c1942_latch_hook = latch_push;       /* queue each main soundlatch command */
}

static void render(signed char *out, int n){
    static int hp_x = 0, hp_y = 0;
    for (int i=0;i<n;i++){
        for (int t=0;t<AY_TICKS_PER_SMP;t++){ ay_tick(&ay1); ay_tick(&ay2); }
        int s = ay_amp(&ay1) + ay_amp(&ay2);   /* unsigned 0..252 */
        /* soft-knee compressor: music (low level) stays linear and loud; only the
         * loud SFX above the knee are compressed, so shots/explosions don't dwarf
         * the music. */
        if (s > 100) s = 100 + (s - 100) * 26 / 152;
        if (s > 126) s = 126;
        /* Paula samples are signed. The AY model is naturally unipolar (0..126),
         * so high-pass it into a real signed waveform; otherwise much of the
         * energy sits as DC and can sound silent/very weak on some setups. */
        int x = s << 8;
        int y = x - hp_x + ((hp_y * 251) >> 8);
        hp_x = x;
        hp_y = y;
        y >>= 7;                       /* gain after DC removal */
        if (y > 120) y = 120;
        if (y < -120) y = -120;
        out[i] = (signed char)y;
    }
}

/* Run one video frame of the audio CPU (with periodic IRQs) and render nsamples
 * of PCM into out. Register changes are captured at IRQ-chunk granularity. */
void c1942_audio_frame(signed char *out, int nsamples){
    int done = 0;
    for (int k=0;k<AUD_IRQ_CHUNKS;k++){
        aud.memory[0x6000] = latch_pop();       /* deliver one queued command/chunk */
        g_audio_active = 1;
        Z80Emulate(&aud.state, AUD_CYC_PER_FRAME / AUD_IRQ_CHUNKS, &aud);
        Z80Interrupt(&aud.state, 0xFF, &aud);   /* IM1 -> RST 38h */
        g_audio_active = 0;
        int n = (k == AUD_IRQ_CHUNKS-1) ? (nsamples - done) : (nsamples / AUD_IRQ_CHUNKS);
        if (n > 0) { render(out + done, n); done += n; }
    }
}
