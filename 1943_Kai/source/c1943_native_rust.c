/* c1943_native_rust.c -- native main-Z80 bridge for the Rust whole-ROM 1943
 * transcode. The main CPU is native, with the native sound-Z80/YM2203 board
 * supplied by c1943_audio.c. */
#include "z80emu.h"
#include <string.h>

extern void run(void *state);

void abort(void) { for (;;) {} }

#define BUDGET 100000
#define FRAME_SLICES 16
#define SCANLINES 262

static unsigned char ST[48];
#define U8(o)   (ST[o])
#define U16(o)  ((unsigned short)((ST[o] << 8) | ST[(o)+1]))
#define U32(o)  ((unsigned long)(((unsigned long)ST[o]<<24)|((unsigned long)ST[(o)+1]<<16)|((unsigned long)ST[(o)+2]<<8)|ST[(o)+3]))
#define SETU16(o,v) do{ ST[o]=(unsigned char)((v)>>8); ST[(o)+1]=(unsigned char)(v); }while(0)
#define SETU32(o,v) do{ ST[o]=(unsigned char)((v)>>24); ST[(o)+1]=(unsigned char)((v)>>16); ST[(o)+2]=(unsigned char)((v)>>8); ST[(o)+3]=(unsigned char)(v); }while(0)
#define O_SP     12
#define O_PC     14
#define O_IFF1   26
#define O_HALTED 29
#define O_STOP   30
#define O_CYCLES 32
#define O_BUDGET 36

static unsigned char region[0x30000];
static unsigned char spr_buffer[0x1000];
static MY_LITTLE_Z80 *gz;
static unsigned char in_ports[5] = { 0xff, 0xff, 0xff, 0xf0, 0xff };
static int cur_bank;
static int s_irq_pending;
static int vblank;

static void update_system_port(void)
{
    if (gz) gz->memory[0xc000] = (unsigned char)((in_ports[0] & ~0x08) | (vblank ? 0x08 : 0x00));
}

MY_LITTLE_Z80 *c1943_audio_z = 0;
void          (*c1943_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) = 0;
unsigned char (*c1943_audio_rd_hook)(MY_LITTLE_Z80 *z, unsigned a) = 0;
void          (*c1943_latch_hook)(unsigned char v) = 0;
static unsigned char pending_latch[64];
static int pending_latch_r, pending_latch_w;

static void pending_latch_push(unsigned char v)
{
    if (v == 0xff) return;
    int n = (pending_latch_w + 1) & 63;
    if (n != pending_latch_r) {
        pending_latch[pending_latch_w] = v;
        pending_latch_w = n;
    }
}

void c1943_soundlatch_write(unsigned char v)
{
    if (c1943_latch_hook) c1943_latch_hook(v);
    else pending_latch_push(v);
}

void c1943_flush_pending_latches(void)
{
    if (!c1943_latch_hook) return;
    while (pending_latch_r != pending_latch_w) {
        c1943_latch_hook(pending_latch[pending_latch_r]);
        pending_latch_r = (pending_latch_r + 1) & 63;
    }
}

void c1943_sprite_dma(unsigned char *mem)
{
    memcpy(spr_buffer, mem + 0xf000, 0x1000);
}

void c1943_bank_select(unsigned char *mem, unsigned char ctrl)
{
    cur_bank = (ctrl >> 2) & 7;
    mem[0xc804] = ctrl;
    memcpy(mem + 0x8000, region + 0x10000 + cur_bank * 0x4000, 0x4000);
}

unsigned char machine_rd(MY_LITTLE_Z80 *z, unsigned int a)
{
    a &= 0xffff;
    return c1943_audio_rd_hook ? c1943_audio_rd_hook(z, a) : z->memory[a];
}

void machine_wr(MY_LITTLE_Z80 *z, unsigned int a, unsigned char v)
{
    a &= 0xffff;
    if (c1943_audio_wr_hook) c1943_audio_wr_hook(z, a, v);
}

unsigned char in_impl(MY_LITTLE_Z80 *z, int port) { (void)z; (void)port; return 0xff; }
void out_impl(MY_LITTLE_Z80 *z, int port, unsigned char x) { (void)z; (void)port; (void)x; }

void c1943_load_maincpu(const unsigned char *p)
{
    memcpy(region, p, sizeof region);
}

void c1943_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2,
                      unsigned char dswa, unsigned char dswb)
{
    in_ports[0] = sys;
    in_ports[1] = p1;
    in_ports[2] = p2;
    in_ports[3] = dswa;
    in_ports[4] = dswb;
    if (gz) {
        update_system_port();
        gz->memory[0xc001] = p1;
        gz->memory[0xc002] = p2;
        gz->memory[0xc003] = dswa;
        gz->memory[0xc004] = dswb;
    }
}

void c1943_init(MY_LITTLE_Z80 *z)
{
    gz = z;
    memset(z->memory, 0, sizeof z->memory);
    memcpy(z->memory, region, 0x8000);
    cur_bank = 0;
    memcpy(z->memory + 0x8000, region + 0x10000, 0x4000);
    for (int i = 0; i < 5; i++) z->memory[0xc000 + i] = in_ports[i];
    z->memory[0xc007] = 0x00;
    memset(spr_buffer, 0, sizeof spr_buffer);

    memset(ST, 0, sizeof ST);
    U8(6) = 0xff;
    U8(7) = 0xff;
    SETU16(O_SP, 0xffff);
    SETU16(O_PC, 0x0000);
    SETU32(O_BUDGET, BUDGET);
    SETU32(44, (unsigned long)z->memory);
    s_irq_pending = 0;
    vblank = 0;
    pending_latch_r = pending_latch_w = 0;
    update_system_port();
}

static void inject38(void)
{
    if (!U8(O_IFF1)) return;
    unsigned short pc = U16(O_PC);
    unsigned short sp = (unsigned short)(U16(O_SP) - 2);
    if (sp >= 0xc000) gz->memory[sp] = (unsigned char)pc;
    if ((unsigned short)(sp + 1) >= 0xc000) gz->memory[(unsigned short)(sp + 1)] = (unsigned char)(pc >> 8);
    SETU16(O_SP, sp);
    U8(O_IFF1) = 0;
    U8(27) = 0;
    U8(O_HALTED) = 0;
    SETU16(O_PC, 0x38);
}

void c1943_run_frame(MY_LITTLE_Z80 *z)
{
    (void)z;
    const unsigned long per = BUDGET / FRAME_SLICES;
    const int s1 = 144 * FRAME_SLICES / SCANLINES;
    const int s2 = 240 * FRAME_SLICES / SCANLINES;

    SETU32(O_CYCLES, 0);
    for (int s = 0; s < FRAME_SLICES; s++) {
        unsigned long target = U32(O_CYCLES) + per;
        if (s == s1 || s == s2) s_irq_pending = 1;
        vblank = (s >= s2) ? 1 : 0;
        update_system_port();
        if (s_irq_pending && U8(O_IFF1)) {
            inject38();
            s_irq_pending = 0;
        }
        while (U32(O_CYCLES) < target) {
            unsigned long step = U32(O_CYCLES) + 2048;
            if (step > target) step = target;
            if (s_irq_pending && U8(O_IFF1)) {
                inject38();
                s_irq_pending = 0;
            }
            SETU32(O_BUDGET, step);
            run(ST);
            if (U8(O_STOP) == 1 || U8(O_STOP) == 2) {
                SETU32(O_CYCLES, target);
                break;
            }
        }
    }
    vblank = 0;
    update_system_port();
    SETU32(O_BUDGET, BUDGET);
}

const unsigned char *c1943_videoram(MY_LITTLE_Z80 *z) { return z->memory + 0xd000; }
const unsigned char *c1943_colorram(MY_LITTLE_Z80 *z) { return z->memory + 0xd400; }
const unsigned char *c1943_spritebuf(void)
{
    return spr_buffer;
}
int c1943_bg1_scrollx(MY_LITTLE_Z80 *z) { return z->memory[0xd800] | (z->memory[0xd801] << 8); }
int c1943_bg1_scrolly(MY_LITTLE_Z80 *z) { return z->memory[0xd802]; }
int c1943_bg2_scrollx(MY_LITTLE_Z80 *z) { return z->memory[0xd803] | (z->memory[0xd804] << 8); }
int c1943_bg1_on(void)  { return (gz->memory[0xd806] & 0x10) != 0; }
int c1943_bg2_on(void)  { return (gz->memory[0xd806] & 0x20) != 0; }
int c1943_obj_on(void)  { return (gz->memory[0xd806] & 0x40) != 0; }
int c1943_char_on(void) { return (gz->memory[0xc804] & 0x80) != 0; }
int c1943_flip(void)    { return (gz->memory[0xc804] & 0x40) != 0; }

unsigned c1943_pc(MY_LITTLE_Z80 *z) { (void)z; return U16(O_PC); }
int c1943_cur_bank(void)            { return cur_bank; }
int c1943_soundlatch(void)          { return gz ? gz->memory[0xc800] : 0; }
unsigned char c1943_peek(MY_LITTLE_Z80 *z, unsigned a) { return z->memory[a & 0xffff]; }
void c1943_poke(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) { z->memory[a & 0xffff] = v; }
