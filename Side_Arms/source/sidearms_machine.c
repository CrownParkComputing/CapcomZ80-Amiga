/* src/hal/csidearms.c -- Side Arms (Capcom, 1986) machine, MAIN Z80.
 *
 * Cleanest Capcom-Z80 family member: NO encryption (init only sets gameid),
 * NO MCU (8751 socket unpopulated -> no protection reads), HORIZONTAL game
 * (not rotated), RAM-DRIVEN palette (xBRG_444 written straight to 0xc000-c7ff,
 * NOT a PROM-indirect LUT like 1943/Commando). Mirrors c1943.c structure.
 *
 * Main Z80 map (sidearms_map):
 *   0x0000-7fff  fixed ROM (sa03.bin)
 *   0x8000-bfff  banked ROM  (16 entries of 0x4000 at region+0x8000; bank=c801&7)
 *   0xc000-c3ff  palette  (write8,     low byte  RRRRGGGG)
 *   0xc400-c7ff  palette  (write8_ext, high byte xxxxBBBB)
 *   0xc800  R SYSTEM  W soundlatch
 *   0xc801  R P1      W bankswitch (data&7)
 *   0xc802  R P2      W spriteram DMA (buffered)
 *   0xc803  R DSW0
 *   0xc804  R DSW1    W control  (b0/1 coin, b4 sndreset, b5 star, b6 char, b7 flip)
 *   0xc805  R DSW2    W star_scrollx
 *   0xc806           W star_scrolly
 *   0xc808-c809      bg_scrollx (12-bit)
 *   0xc80a-c80b      bg_scrolly (12-bit)
 *   0xc80c           gfxctrl  (b0 bg enable, b1 sprite enable)
 *   0xd000-d7ff  videoram (fg chars)
 *   0xd800-dfff  colorram (fg attr)
 *   0xe000-efff  RAM
 *   0xf000-ffff  spriteram
 * IRQ: 2x/frame HOLD_LINE on line 0 (scanline 112 + 240) -> RST38 (0xFF vector).
 * Sound: Z80 + 2x YM2203 (latch main c800 -> audio d000).
 *
 * Build z80.c with -DZ80_MAP_SIDEARMS so c000-page reads trap (inputs) and
 * palette/vram/ram writes go straight to memory[].
 */
#include "z80emu.h"
#include <string.h>

#define CYCLES_PER_FRAME 133333       /* 8 MHz / 60 */

/* maincpu region: fixed 0..7fff + banked area 0x8000.., up to bank 7 = 0x28000 */
static unsigned char region[0x28000];
static int cur_bank;
static int control, gfxctrl, soundlatch;
static int bg_scrollx, bg_scrolly;     /* raw 16-bit (c808/9, c80a/b) */
static int in_vblank;

/* inputs (active-low; idle 0xff). 0 SYSTEM, 1 P1, 2 P2, 3 DSW0, 4 DSW1, 5 DSW2.
 * SYSTEM bit3 is the vblank flag (active HIGH). The RST38 handler reads it to
 * tell the two per-frame IRQs apart: only the vblank IRQ (scanline 240) does
 * sprite DMA + scroll latch, so it MUST be modelled, not toggled. */
static unsigned char in_ports[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* audio CPU delegation (same global machine_rd/wr serves both Z80s; dispatch
 * by context == csidearms_audio_z). Installed by the audio subsystem later. */
MY_LITTLE_Z80 *csidearms_audio_z = 0;
void          (*csidearms_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) = 0;
unsigned char (*csidearms_audio_rd_hook)(MY_LITTLE_Z80 *z, unsigned a) = 0;
void          (*csidearms_latch_hook)(unsigned char v) = 0;

int csidearms_dbg_maxbank = 0, csidearms_dbg_banksw = 0, csidearms_dbg_seen8 = 0;
static void map_bank(MY_LITTLE_Z80 *z)
{
    if (cur_bank > csidearms_dbg_maxbank) csidearms_dbg_maxbank = cur_bank;
    csidearms_dbg_banksw++;
    memcpy(z->memory + 0x8000, region + 0x8000 + cur_bank * 0x4000, 0x4000);
}

unsigned char machine_rd(MY_LITTLE_Z80 *z, unsigned int a)
{
    a &= 0xffff;
    if (z == csidearms_audio_z) return csidearms_audio_rd_hook ? csidearms_audio_rd_hook(z, a) : z->memory[a];
    switch (a) {
        case 0xc800:                  /* SYSTEM: bit3 = live vblank flag */
            return (in_ports[0] & ~0x08) | (in_vblank ? 0x08 : 0x00);
        case 0xc801: return in_ports[1];   /* P1   */
        case 0xc802: return in_ports[2];   /* P2   */
        case 0xc803: return in_ports[3];   /* DSW0 */
        case 0xc804: return in_ports[4];   /* DSW1 */
        case 0xc805: return in_ports[5];   /* DSW2 */
    }
    return z->memory[a];              /* palette readback / bank / vram / ram */
}

void machine_wr(MY_LITTLE_Z80 *z, unsigned int a, unsigned char v)
{
    a &= 0xffff;
    if (z == csidearms_audio_z) { if (csidearms_audio_wr_hook) csidearms_audio_wr_hook(z, a, v); return; }
    if (a < 0xc000) return;           /* ROM / bank window */
    switch (a) {
        case 0xc800: soundlatch = v; if (csidearms_latch_hook) csidearms_latch_hook(v); return;
        case 0xc801: cur_bank = v & 0x07; map_bank(z); return;   /* bankswitch */
        case 0xc802: return;          /* 86S105 sprite DMA trigger (buffered) */
        case 0xc804: control = v; return;   /* coin/sndreset/star/char/flip */
        case 0xc805: return;          /* star_scrollx (hw starfield clock) */
        case 0xc806: return;          /* star_scrolly */
        case 0xc808: bg_scrollx = (bg_scrollx & 0xff00) | v; return;
        case 0xc809: bg_scrollx = (bg_scrollx & 0x00ff) | (v << 8); return;
        case 0xc80a: bg_scrolly = (bg_scrolly & 0xff00) | v; return;
        case 0xc80b: bg_scrolly = (bg_scrolly & 0x00ff) | (v << 8); return;
        case 0xc80c: gfxctrl = v; return;   /* b0 bg enable, b1 sprite enable */
    }
    if (a >= 0xc000 && a <= 0xc7ff) { z->memory[a] = v; return; }  /* palette RAM */
    if (a >= 0xd000)                { z->memory[a] = v; return; }  /* vram/cram/ram/spr */
    /* other c000-cfff I/O: ignore */
}

unsigned char in_impl(MY_LITTLE_Z80 *z, int port)        { (void)z; (void)port; return 0xff; }
void          out_impl(MY_LITTLE_Z80 *z, int port, unsigned char x) { (void)z; (void)port; (void)x; }

/* ---- public API ---- */
void csidearms_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2)
{ in_ports[0] = sys; in_ports[1] = p1; in_ports[2] = p2; }
void csidearms_set_dsw(unsigned char d0, unsigned char d1, unsigned char d2)
{ in_ports[3] = d0; in_ports[4] = d1; in_ports[5] = d2; }

void csidearms_load(const unsigned char *maincpu) { memcpy(region, maincpu, sizeof region); }

void csidearms_init(MY_LITTLE_Z80 *z)
{
    memset(z->memory, 0, sizeof z->memory);
    memcpy(z->memory, region, 0x8000);   /* fixed ROM 0x0000-0x7fff */
    cur_bank = control = gfxctrl = soundlatch = 0;
    bg_scrollx = bg_scrolly = in_vblank = 0;
    map_bank(z);
    Z80Reset(&z->state);
}

/* HOLD_LINE model: the IRQ line stays asserted until the CPU accepts it. The
 * core's Z80Interrupt drops the IRQ (returns 0) whenever iff1 is clear, but the
 * scheduler spends most of its time inside di/ei windows, so a single edge would
 * be lost and the per-frame task timers would never advance. Retry, advancing
 * the CPU a few cycles between tries, until the pending EI lets it through. */
static void hold_irq(MY_LITTLE_Z80 *z)
{
    for (int t = 0; t < 8192; t++) {
        if (Z80Interrupt(&z->state, 0xFF, z)) return;  /* accepted (iff1 set) */
        Z80Emulate(&z->state, 24, z);                  /* run toward next EI   */
    }
}

void csidearms_run_frame(MY_LITTLE_Z80 *z)
{
    /* 2 IRQs/frame (MAME scanlines 112 + 240 of 256), both RST38 (0xFF). The
     * vblank flag (SYSTEM bit3) is 0 at the scanline-112 IRQ and 1 at the
     * scanline-240 IRQ (start of vblank); the handler keys off it. */
    in_vblank = 0;
    Z80Emulate(&z->state, CYCLES_PER_FRAME * 112 / 256, z);  /* lines 0..112  */
    hold_irq(z);                                             /* mid-screen IRQ */
    Z80Emulate(&z->state, CYCLES_PER_FRAME * 128 / 256, z);  /* lines 112..240 */
    in_vblank = 1;
    hold_irq(z);                                             /* vblank IRQ     */
    Z80Emulate(&z->state, CYCLES_PER_FRAME * 16 / 256, z);   /* lines 240..256 */
}

/* introspection / for the renderer */
unsigned      csidearms_pc(MY_LITTLE_Z80 *z)        { return z->state.pc & 0xffff; }
int           csidearms_bank(void)                  { return cur_bank; }
int           csidearms_scrollx(void)               { return bg_scrollx; }
int           csidearms_scrolly(void)               { return bg_scrolly; }
int           csidearms_control(void)               { return control & 0xff; }
int           csidearms_gfxctrl(void)               { return gfxctrl & 0xff; }
int           csidearms_soundlatch(void)            { return soundlatch & 0xff; }
unsigned char csidearms_peek(MY_LITTLE_Z80 *z, unsigned a) { return z->memory[a & 0xffff]; }
void          csidearms_poke(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) { z->memory[a & 0xffff] = v; }
