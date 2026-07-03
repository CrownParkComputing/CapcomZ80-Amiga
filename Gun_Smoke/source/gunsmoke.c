/* gunsmoke.c -- Gun.Smoke (Capcom, 1985) machine, MAIN Z80.
 * Uses the 'gunsmoke' World parent set (no MCU, no encryption; the only
 * "protection" is a 3-byte fixed table at 0xc4c9). Modelled on c1943.c.
 *
 * Main Z80 map (from mame/capcom/gunsmoke.cpp main_map):
 *   0x0000-7fff  fixed ROM        (gs03)
 *   0x8000-bfff  banked ROM       (4 x 0x4000 at region 0x8000; bank=(ctrl>>2)&3)
 *   0xc000       SYSTEM (r)        bit3 = VBLANK (active-low)
 *   0xc001       P1 (r)
 *   0xc002       P2 (r)
 *   0xc003       DSW1 (r)
 *   0xc004       DSW2 (r)
 *   0xc4c9-c4cb  protection_r -> { 0xff, 0x00, 0x00 }
 *   0xc800 (w)   soundlatch
 *   0xc804 (w)   control_w: bit0-1 coin, bit2-3 ROM bank, bit5 sndreset,
 *                           bit6 flip, bit7 chon (char layer enable)
 *   0xc806 (w)   spriteram DMA trigger (buffered)
 *   0xd000-d3ff  videoram  (fg char tilemap)            (w-through)
 *   0xd400-d7ff  colorram  (fg char attr)               (w-through)
 *   0xd800-d801  scrollx (16-bit, the scroll axis)
 *   0xd802       scrolly (8-bit)
 *   0xd806 (w)   layer_w: bit0-2 sprite3bank, bit4 bgon, bit5 objon
 *   0xe000-efff  work RAM
 *   0xf000-ffff  spriteram
 * IRQ: single irq0_line_hold at vblank => RST38 (0xff) once per frame.
 */
#include "z80emu.h"
#include <string.h>

#define CYCLES_PER_FRAME 50000        /* 3 MHz / ~60 Hz */

static unsigned char region[0x18000]; /* maincpu: fixed 0..7fff, 4 banks at 0x8000 */
static int cur_bank;
static int control, layer, soundlatch;
static int scrollx, scrolly;

/* inputs: 0 SYSTEM, 1 P1, 2 P2, 3 DSW1, 4 DSW2 (active-low; idle 0xff).
 * DSW1=0xf7 selects Upright; DSW2=0xff = 1C/1C, continue, demo sounds on. */
static unsigned char in_ports[5] = { 0xff, 0xff, 0xff, 0xf7, 0xff };

/* VBLANK bit (SYSTEM bit3, active-low): the host toggles this via the API. */
static int vblank_bit = 0;            /* 0 => bit set (not in vblank) */

/* Optional debug hook (host only): fires on protection reads. */
void (*gunsmoke_dbg_prot)(unsigned a, unsigned char v, unsigned pc) = 0;

static void map_bank(MY_LITTLE_Z80 *z)
{
    memcpy(z->memory + 0x8000, region + 0x8000 + cur_bank * 0x4000, 0x4000);
}

static unsigned char system_port(void)
{
    unsigned char v = in_ports[0];
    if (vblank_bit) v &= ~0x08; else v |= 0x08;   /* bit3 active-low VBLANK */
    return v;
}

unsigned char machine_rd(MY_LITTLE_Z80 *z, unsigned int a)
{
    a &= 0xffff;
    if (a == 0xc000) return system_port();
    if (a >= 0xc001 && a <= 0xc004) return in_ports[a - 0xc000];
    if (a >= 0xc4c9 && a <= 0xc4cb) {              /* protection table */
        static const unsigned char fixed[3] = { 0xff, 0x00, 0x00 };
        unsigned char v = fixed[a - 0xc4c9];
        if (gunsmoke_dbg_prot) gunsmoke_dbg_prot(a, v, z->state.pc & 0xffff);
        return v;
    }
    return z->memory[a];                            /* bank hi half, vram, RAM, spriteram */
}

void machine_wr(MY_LITTLE_Z80 *z, unsigned int a, unsigned char v)
{
    a &= 0xffff;
    if (a < 0xc000) return;                          /* ROM / bank window */
    switch (a) {
        case 0xc800: soundlatch = v; return;
        case 0xc804:
            control = v;
            { int bank = (v >> 2) & 3; if (bank != cur_bank) { cur_bank = bank; map_bank(z); } }
            return;
        case 0xc806: return;                         /* spriteram DMA trigger (buffered) */
        case 0xd800: scrollx = (scrollx & 0xff00) | v; return;
        case 0xd801: scrollx = (scrollx & 0x00ff) | (v << 8); return;
        case 0xd802: scrolly = v; return;
        case 0xd806: layer = v; return;
    }
    if (a >= 0xd000 && a <= 0xd7ff) { z->memory[a] = v; return; }  /* videoram/colorram */
    if (a >= 0xe000)                { z->memory[a] = v; return; }  /* RAM + spriteram */
    /* other 0xc0xx/0xd8xx I/O: ignore */
}

/* Native-transcode path (used later): lift latches written straight into
 * z->memory back into the renderer globals, and mirror live inputs. */
void gunsmoke_sync_io(MY_LITTLE_Z80 *z)
{
    scrollx = z->memory[0xd800] | (z->memory[0xd801] << 8);
    scrolly = z->memory[0xd802];
    layer   = z->memory[0xd806];
    control = z->memory[0xc804];
    int bank = (control >> 2) & 3;
    if (bank != cur_bank) { cur_bank = bank; map_bank(z); }
    z->memory[0xc000] = system_port();
    z->memory[0xc001] = in_ports[1]; z->memory[0xc002] = in_ports[2];
    z->memory[0xc003] = in_ports[3]; z->memory[0xc004] = in_ports[4];
}

unsigned char in_impl(MY_LITTLE_Z80 *z, int port)        { (void)z; (void)port; return 0xff; }
void          out_impl(MY_LITTLE_Z80 *z, int port, unsigned char x) { (void)z; (void)port; (void)x; }

/* ---- public API ---- */
void gunsmoke_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2)
{ in_ports[0] = sys; in_ports[1] = p1; in_ports[2] = p2; }
void gunsmoke_set_dsw(unsigned char d1, unsigned char d2) { in_ports[3] = d1; in_ports[4] = d2; }
void gunsmoke_set_vblank(int on) { vblank_bit = on ? 1 : 0; }

void gunsmoke_load(const unsigned char *maincpu) { memcpy(region, maincpu, sizeof region); }

void gunsmoke_init(MY_LITTLE_Z80 *z)
{
    memset(z->memory, 0, sizeof z->memory);
    memcpy(z->memory, region, 0x8000);   /* fixed ROM 0x0000-0x7fff */
    cur_bank = control = layer = soundlatch = 0;
    scrollx = scrolly = 0;
    map_bank(z);
    Z80Reset(&z->state);
}

void gunsmoke_run_frame(MY_LITTLE_Z80 *z)
{
    Z80Emulate(&z->state, CYCLES_PER_FRAME, z);
    Z80Interrupt(&z->state, 0xFF, z);    /* RST 38h (vblank) */
}

/* introspection / for the renderer */
unsigned      gunsmoke_pc(MY_LITTLE_Z80 *z)     { return z->state.pc & 0xffff; }
int           gunsmoke_bank(void)               { return cur_bank; }
int           gunsmoke_scrollx(void)            { return scrollx; }
int           gunsmoke_scrolly(void)            { return scrolly; }
int           gunsmoke_control(void)            { return control; }
int           gunsmoke_layer(void)              { return layer; }
int           gunsmoke_chon(void)               { return (control & 0x80) ? 1 : 0; }
int           gunsmoke_bgon(void)               { return (layer & 0x10) ? 1 : 0; }
int           gunsmoke_objon(void)              { return (layer & 0x20) ? 1 : 0; }
int           gunsmoke_sprite3bank(void)        { return layer & 0x07; }
int           gunsmoke_soundlatch(void)         { return soundlatch & 0xff; }
unsigned char gunsmoke_peek(MY_LITTLE_Z80 *z, unsigned a) { return z->memory[a & 0xffff]; }
void          gunsmoke_poke(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) { z->memory[a & 0xffff] = v; }
