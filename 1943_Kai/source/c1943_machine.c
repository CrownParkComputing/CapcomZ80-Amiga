/* c1943_machine.c -- 1943 (Capcom, 1943b bootleg, NO MCU) main-Z80 machine.
 * Greenfield transcription of MAME 0.288 capcom/1943.cpp (_1943b config).
 *
 * Main Z80 @ 6 MHz. Memory map (from c1943_map + c1943b_map overrides):
 *   0x0000-0x7fff  fixed ROM
 *   0x8000-0xbfff  banked ROM (8 banks of 0x4000 at region 0x10000)
 *   0xc000 SYSTEM  0xc001 P1  0xc002 P2  0xc003 DSWA  0xc004 DSWB
 *   0xc007         protection read -> 0x00 (bootleg)
 *   0xc800 (w)     sound latch (captured, not played)
 *   0xc804 (w)     control: bits2-4 ROM bank, bit5 sndreset, bit6 flip, bit7 char enable
 *   0xc806 (w)     sprite-DMA strobe -> buffer 0xf000..0xffff
 *   0xc807 (w)     protection write -> nop (bootleg)
 *   0xd000-0xd3ff  videoram   0xd400-0xd7ff colorram
 *   0xd800-0xd801  scrollx(16) 0xd802 scrolly(8) 0xd803-0xd804 bgscrollx(16)
 *   0xd806 (w)     layer enable: bit4 bg1, bit5 bg2, bit6 sprites
 *   0xe000-0xefff  work RAM    0xf000-0xffff spriteram
 * IRQ: 2 per frame (scanlines 144 & 240), both assert the maskable line.
 *
 * z80.c is compiled with -DZ80_MAP_1943, which sets:
 *   Z80_FAST_READ(a)  = ((a & 0xf000) != 0xc000)   -> only the 0xc000 page traps
 *   Z80_FAST_WRITE(a) = ((a>=0xd000 && a<=0xd7ff) || a>=0xe004)
 * So videoram/colorram (0xd000-0xd7ff) and RAM/spriteram (0xe004+) write straight
 * to memory[]; the control ports, ROM, scroll (0xd800+), layer (0xd806) and
 * 0xe000-0xe003 trap here to machine_wr.
 */
#include "z80emu.h"
#include <string.h>

#define MAIN_CYCLES_PER_FRAME 100000   /* 6 MHz / 60 */
#define SCANLINES 262

static unsigned char region[0x30000];      /* maincpu: fixed 0..7fff + 8 banks @0x10000 */
static unsigned char spr_buffer[0x1000];   /* buffered spriteram (filled on 0xc806) */
static int cur_bank;
static int control;                        /* 0xc804 latch */
static int layer;                          /* 0xd806 latch */
static int soundlatch;
static int vblank;                         /* SYSTEM bit3, set during vblank region */

/* inputs: [0]=SYSTEM [1]=P1 [2]=P2 [3]=DSWA [4]=DSWB. Active-low; MAME defaults
 * for the DSWs (difficulty Normal, 1C/1C, continue Yes, demo sounds On). */
static unsigned char in_ports[5] = { 0xff, 0xff, 0xff, 0xf8, 0xff };

/* audio-CPU delegation: c1943_audio.c (when linked) registers the audio Z80
 * context + its rd/wr handlers here; the shared z80.c calls machine_rd/wr for
 * BOTH CPUs, so we route by context pointer. The latch hook posts main-CPU
 * 0xc800 writes into the audio soundlatch FIFO. All null when audio isn't linked
 * (e.g. the host harness), leaving the main CPU unaffected. */
MY_LITTLE_Z80 *c1943_audio_z = 0;
void          (*c1943_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) = 0;
unsigned char (*c1943_audio_rd_hook)(MY_LITTLE_Z80 *z, unsigned a) = 0;
void          (*c1943_latch_hook)(unsigned char v) = 0;

void c1943_flush_pending_latches(void)
{
    /* Interpreter main CPU starts after audio registration, so no pre-audio
     * latch queue is required. Keep the hook for the shared audio module. */
}

static void map_bank(MY_LITTLE_Z80 *z)
{
    memcpy(z->memory + 0x8000, region + 0x10000 + cur_bank * 0x4000, 0x4000);
}

static void control_w(MY_LITTLE_Z80 *z, unsigned char v)
{
    control = v;
    int bank = (v & 0x1c) >> 2;            /* bits 2-4 */
    if (bank != cur_bank) { cur_bank = bank; map_bank(z); }
    /* bit5 sound-CPU reset, bits0-1 coin counters: no-op (audio not emulated) */
}

/* ---- z80.c global hooks (shared by the core; we run only the main CPU) ---- */
unsigned char machine_rd(MY_LITTLE_Z80 *z, unsigned int a)
{
    a &= 0xffff;
    if (z == c1943_audio_z) return c1943_audio_rd_hook ? c1943_audio_rd_hook(z, a) : z->memory[a];
    switch (a) {
        case 0xc000: return (in_ports[0] & ~0x08) | (vblank ? 0x08 : 0x00);
        case 0xc001: return in_ports[1];
        case 0xc002: return in_ports[2];
        case 0xc003: return in_ports[3];
        case 0xc004: return in_ports[4];
#ifdef C1943_KAI
        /* 1943 Kai: original i8751 MCU set (no no-MCU bootleg). The MCU protection
         * reduces to: a read of 0xc007 returns the Z80 B register. The protected code
         * sets B = expected value, reads 0xc007, then CP B -- so returning B makes
         * every check pass (software MCU sim; verified byte-identical to MAME 1943kai). */
        case 0xc007: return z->state.registers.byte[Z80_B];
#else
        case 0xc007: return 0x00;          /* 1943b bootleg: protection patched out */
#endif
    }
    return z->memory[a];                    /* banked ROM hi half / anything else */
}

void machine_wr(MY_LITTLE_Z80 *z, unsigned int a, unsigned char v)
{
    a &= 0xffff;
    if (z == c1943_audio_z) { if (c1943_audio_wr_hook) c1943_audio_wr_hook(z, a, v); return; }
    if (a < 0xc000) return;                 /* ROM (fixed + banked window) */
    if (a < 0xd000) {                       /* I/O page */
        switch (a) {
            case 0xc800: soundlatch = v; if (c1943_latch_hook) c1943_latch_hook(v); return;
            case 0xc804: control_w(z, v); return;
            case 0xc806: memcpy(spr_buffer, z->memory + 0xf000, 0x1000); return;
            case 0xc807: return;            /* bootleg nop */
        }
        return;                             /* other 0xc000-page writes: ignore */
    }
    /* 0xd800-0xd804 scroll, 0xd806 layer, 0xe000-0xe003 (main RAM) trap here */
    if (a == 0xd806) layer = v;
    z->memory[a] = v;                       /* keep scroll/layer/RAM readable in memory[] */
}

/* main Z80 uses no Z80 IN/OUT ports */
unsigned char in_impl(MY_LITTLE_Z80 *z, int port) { (void)z; (void)port; return 0xff; }
void out_impl(MY_LITTLE_Z80 *z, int port, unsigned char x) { (void)z; (void)port; (void)x; }

/* ---- public API ---- */
void c1943_load_maincpu(const unsigned char *p) { memcpy(region, p, sizeof region); }

void c1943_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2,
                      unsigned char dswa, unsigned char dswb)
{
    in_ports[0] = sys; in_ports[1] = p1; in_ports[2] = p2;
    in_ports[3] = dswa; in_ports[4] = dswb;
}

void c1943_init(MY_LITTLE_Z80 *z)
{
    memset(z->memory, 0, sizeof z->memory);
    memcpy(z->memory, region, 0x8000);      /* fixed ROM 0x0000-0x7fff */
    z->opcodes = 0; z->opcodes_len = 0;     /* unencrypted */
    cur_bank = 0; map_bank(z);
    control = 0; layer = 0; soundlatch = 0; vblank = 0;
    memset(spr_buffer, 0, sizeof spr_buffer);
    Z80Reset(&z->state);
}

/* Run one frame in fine slices, asserting the two maskable IRQs (mid-screen
 * scanline 144 + vblank scanline 240) and HOLDING each until the CPU actually
 * accepts it -- Z80Interrupt() returns 0 while interrupts are masked (DI), so we
 * keep retrying on later slices instead of dropping it. (A dropped vblank/enemy
 * IRQ is the classic "enemies fire less" symptom on this Capcom board.) The
 * vblank SYSTEM bit tracks the beam across the slices. */
#define FRAME_SLICES 16
int c1943_dbg_irqs = 0;     /* IRQs actually accepted in the last frame (host debug) */
void c1943_run_frame(MY_LITTLE_Z80 *z)
{
    const int per = MAIN_CYCLES_PER_FRAME / FRAME_SLICES;
    const int s1  = 144 * FRAME_SLICES / SCANLINES;   /* mid-screen IRQ slice */
    const int s2  = 240 * FRAME_SLICES / SCANLINES;   /* vblank IRQ slice */
    int irq = 0;
    c1943_dbg_irqs = 0;
    for (int s = 0; s < FRAME_SLICES; s++) {
        if (s == s1 || s == s2) irq = 1;              /* assert (HOLD_LINE) */
        vblank = (s >= s2) ? 1 : 0;
        if (irq && Z80Interrupt(&z->state, 0xff, z) != 0) { irq = 0; c1943_dbg_irqs++; }
        Z80Emulate(&z->state, per, z);
    }
}

/* ---- accessors ---- */
const unsigned char *c1943_videoram(MY_LITTLE_Z80 *z) { return z->memory + 0xd000; }
const unsigned char *c1943_colorram(MY_LITTLE_Z80 *z) { return z->memory + 0xd400; }
const unsigned char *c1943_spritebuf(void)            { return spr_buffer; }
int c1943_bg1_scrollx(MY_LITTLE_Z80 *z) { return z->memory[0xd800] | (z->memory[0xd801] << 8); }
int c1943_bg1_scrolly(MY_LITTLE_Z80 *z) { return z->memory[0xd802]; }
int c1943_bg2_scrollx(MY_LITTLE_Z80 *z) { return z->memory[0xd803] | (z->memory[0xd804] << 8); }
int c1943_bg1_on(void)  { return (layer & 0x10) != 0; }
int c1943_bg2_on(void)  { return (layer & 0x20) != 0; }
int c1943_obj_on(void)  { return (layer & 0x40) != 0; }
int c1943_char_on(void) { return (control & 0x80) != 0; }
int c1943_flip(void)    { return (control & 0x40) != 0; }

unsigned c1943_pc(MY_LITTLE_Z80 *z) { return z->state.pc & 0xffff; }
int c1943_cur_bank(void)            { return cur_bank; }
int c1943_soundlatch(void)          { return soundlatch & 0xff; }

unsigned char c1943_peek(MY_LITTLE_Z80 *z, unsigned a)            { return z->memory[a & 0xffff]; }
void          c1943_poke(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) { z->memory[a & 0xffff] = v; }
