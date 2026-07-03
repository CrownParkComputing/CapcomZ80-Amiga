/* src/hal/c1942.c -- 1942 (Capcom) machine: MAIN Z80 first-light.
 * Boots the real srb-03..07 ROMs on the z80.c core. Audio Z80 + AY8910 come
 * later (sound). Mirrors the Pac-Land pl_machine.c approach.
 *
 * Main Z80 map: 0x0000-7fff fixed ROM, 0x8000-bfff banked ROM (bank = data&3,
 *   4x0x4000 at region 0x10000), 0xc000-c004 inputs, 0xc800 soundlatch,
 *   0xc802-3 scroll, 0xc804 control, 0xc805 palette_bank, 0xc806 bankswitch,
 *   0xcc00-cc7f spriteram, 0xd000-d7ff fg vram, 0xd800-dbff bg vram,
 *   0xe000-efff RAM.
 * z80.c serves <0xa000 from memory[] directly (so fixed ROM + bank low half +
 *   opcode fetch work); >=0xa000 reads and non-RAM writes trap to machine_rd/wr.
 * IRQs: RST 08h (0xCF) mid-frame, RST 10h (0xD7) at vblank.
 */
#include "z80emu.h"
#include <string.h>

#define CYCLES_PER_FRAME 66667        /* 4 MHz / 60 */

static unsigned char region[0x20000]; /* maincpu: fixed 0..7fff, banks 0x10000+ */
static int cur_bank;
static int scroll, palette_bank, control, soundlatch;

/* inputs, indexes: 0 SYSTEM, 1 P1, 2 P2, 3 DSWA, 4 DSWB. Active-high reads on
 * this hardware; idle = 0xff for IN ports, DSW per default. 0x00 = nothing. */
static unsigned char in_ports[5] = { 0xff, 0xff, 0xff, 0xff, 0xff };

/* Audio CPU delegation: c1942_audio.c (when linked) registers its context + write
 * handler here; z80.c calls the same global machine_rd/wr for BOTH CPUs, so we
 * route by context pointer. Hooks default null -> host tools that don't link the
 * audio module are unaffected. */
MY_LITTLE_Z80 *c1942_audio_z = 0;
void (*c1942_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) = 0;
void (*c1942_latch_hook)(unsigned char v) = 0;     /* host debug: every soundlatch write */

static MY_LITTLE_Z80 *map_bank(MY_LITTLE_Z80 *z)
{
    memcpy(z->memory + 0x8000, region + 0x10000 + cur_bank * 0x4000, 0x4000);
    return z;
}

unsigned char machine_rd(MY_LITTLE_Z80 *z, unsigned int a)
{
    a &= 0xffff;
    if (z == c1942_audio_z) return z->memory[a];   /* audio: ROM/RAM/latch in mem[] */
    if (a >= 0xc000 && a <= 0xc004) return in_ports[a - 0xc000];
    return z->memory[a];              /* bank hi half, spriteram, vram, RAM */
}

void machine_wr(MY_LITTLE_Z80 *z, unsigned int a, unsigned char v)
{
    a &= 0xffff;
    if (z == c1942_audio_z) { if (c1942_audio_wr_hook) c1942_audio_wr_hook(z, a, v); return; }
    if (a < 0x8000)  return;          /* fixed ROM */
    if (a < 0xc000)  return;          /* banked ROM window */
    switch (a) {
        case 0xc800: soundlatch = v; if (c1942_latch_hook) c1942_latch_hook(v); return;
        case 0xc802: scroll = (scroll & 0xff00) | v; return;
        case 0xc803: scroll = (scroll & 0x00ff) | (v << 8); return;
        case 0xc804: control = v; return;          /* bit7 flip, bit4 ? */
        case 0xc805: palette_bank = v; return;
        case 0xc806: cur_bank = v & 0x03; map_bank(z); return;
    }
    if (a >= 0xcc00) { z->memory[a] = v; return; } /* spriteram/vram/RAM */
    /* 0xc000-0xcbff misc I/O: ignore */
}

/* main Z80 uses no I/O ports */
unsigned char in_impl(MY_LITTLE_Z80 *z, int port)        { (void)z; (void)port; return 0xff; }
void          out_impl(MY_LITTLE_Z80 *z, int port, unsigned char x) { (void)z; (void)port; (void)x; }

/* ---- public API ---- */
void c1942_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2)
{ in_ports[0] = sys; in_ports[1] = p1; in_ports[2] = p2; }

void c1942_load(const unsigned char *maincpu) { memcpy(region, maincpu, sizeof region); }

void c1942_init(MY_LITTLE_Z80 *z)
{
    memset(z->memory, 0, sizeof z->memory);
    memcpy(z->memory, region, 0x8000);   /* fixed ROM 0x0000-0x7fff */
    cur_bank = 0; map_bank(z);
    scroll = palette_bank = control = soundlatch = 0;
    Z80Reset(&z->state);
}

void c1942_run_frame(MY_LITTLE_Z80 *z)
{
    Z80Emulate(&z->state, CYCLES_PER_FRAME / 2, z);
    Z80Interrupt(&z->state, 0xCF, z);    /* RST 08h (mid-frame) */
    Z80Emulate(&z->state, CYCLES_PER_FRAME / 2, z);
    Z80Interrupt(&z->state, 0xD7, z);    /* RST 10h (vblank)    */
}

/* introspection */
unsigned      c1942_pc(MY_LITTLE_Z80 *z)        { return z->state.pc & 0xffff; }
int           c1942_bank(void)                  { return cur_bank; }
int           c1942_scroll(void)                { return scroll; }
int           c1942_palette_bank(void)          { return palette_bank; }
int           c1942_soundlatch(void)            { return soundlatch & 0xff; }
unsigned char c1942_peek(MY_LITTLE_Z80 *z, unsigned a) { return z->memory[a & 0xffff]; }
void          c1942_poke(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) { z->memory[a & 0xffff] = v; }
