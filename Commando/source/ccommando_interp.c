/* ccommando_interp.c -- Commando main Z80 through the shared z80.c interpreter.
 * This keeps the RTG presenter/renderer/audio shell unchanged while avoiding the
 * native transcode path, useful for correctness checks on sprite-object updates. */
#include "z80emu.h"
#include <string.h>

#define CYCLES_PER_FRAME 50000
#define SCANLINES 262

static unsigned char region[0xc000];
static unsigned char decrypted[0xc000];
static MY_LITTLE_Z80 *gz;
static unsigned char in_ports[5] = { 0xff, 0xff, 0xff, 0xff, 0xff };
static unsigned char sprite_buffer[0x180];
static int sprite_buffer_valid;

MY_LITTLE_Z80 *ccommando_audio_z = 0;
void          (*ccommando_audio_wr_hook)(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) = 0;
unsigned char (*ccommando_audio_rd_hook)(MY_LITTLE_Z80 *z, unsigned a) = 0;
void          (*ccommando_latch_hook)(unsigned char v) = 0;

static void build_decrypted(void) {
    decrypted[0] = region[0];
    for (int a = 1; a < 0xc000; a++) {
        unsigned char b = region[a];
        decrypted[a] = (b & 0x11) | ((b & 0xe0) >> 4) | ((b & 0x0e) << 4);
    }
}

void ccommando_load(const unsigned char *maincpu) {
    memcpy(region, maincpu, sizeof region);
    build_decrypted();
}

void ccommando_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2) {
    in_ports[0] = sys;
    in_ports[1] = p1;
    in_ports[2] = p2;
    if (gz) {
        gz->memory[0xc000] = sys;
        gz->memory[0xc001] = p1;
        gz->memory[0xc002] = p2;
        gz->memory[0xc003] = in_ports[3];
        gz->memory[0xc004] = in_ports[4];
    }
}

void ccommando_set_dsw(unsigned char dsw0, unsigned char dsw1) {
    in_ports[3] = dsw0;
    in_ports[4] = dsw1;
    if (gz) {
        gz->memory[0xc003] = dsw0;
        gz->memory[0xc004] = dsw1;
    }
}

void ccommando_init(MY_LITTLE_Z80 *z) {
    gz = z;
    memset(z->memory, 0, sizeof z->memory);
    memcpy(z->memory, region, sizeof region);
    z->opcodes = decrypted;
    z->opcodes_len = sizeof decrypted;
    for (int i = 0; i < 5; i++) z->memory[0xc000 + i] = in_ports[i];
    memset(sprite_buffer, 0, sizeof sprite_buffer);
    sprite_buffer_valid = 0;
    Z80Reset(&z->state);
}

static void run_cycles(MY_LITTLE_Z80 *z, int cycles) {
    while (cycles > 0) {
        int n = cycles > 2048 ? 2048 : cycles;
        int ran = Z80Emulate(&z->state, n, z);
        if (ran <= 0) ran = n;
        cycles -= ran;
        if (z->state.status == Z80_STATUS_HALT) break;
    }
}

void ccommando_run_frame(MY_LITTLE_Z80 *z) {
    int c109 = (CYCLES_PER_FRAME * 109) / SCANLINES;
    int c240 = (CYCLES_PER_FRAME * 240) / SCANLINES;
    run_cycles(z, c109);
    Z80Interrupt(&z->state, 0xcf, z);       /* IM0 RST 08h, scanline 109 */
    run_cycles(z, c240 - c109);
    Z80Interrupt(&z->state, 0xd7, z);       /* IM0 RST 10h, scanline 240 */
    run_cycles(z, CYCLES_PER_FRAME - c240);
}

unsigned char ccommando_peek(MY_LITTLE_Z80 *z, unsigned a) {
    return z->memory[a & 0xffff];
}

void ccommando_poke(MY_LITTLE_Z80 *z, unsigned a, unsigned char v) {
    z->memory[a & 0xffff] = v;
}

unsigned char ccommando_spritebuf_peek(unsigned o) {
    if (!gz) return 0;
    if (!sprite_buffer_valid) memcpy(sprite_buffer, gz->memory + 0xfe00, sizeof sprite_buffer);
    return sprite_buffer[o & 0x17f];
}

unsigned ccommando_pc(MY_LITTLE_Z80 *z) {
    return (unsigned)(z->state.pc & 0xffff);
}

int ccommando_scrollx(void) {
    return gz ? (gz->memory[0xc808] | (gz->memory[0xc809] << 8)) : 0;
}

int ccommando_scrolly(void) {
    return gz ? (gz->memory[0xc80a] | (gz->memory[0xc80b] << 8)) : 0;
}

int ccommando_control(void) {
    return gz ? gz->memory[0xc804] : 0;
}

int ccommando_soundlatch(void) {
    return gz ? gz->memory[0xc800] : 0;
}

unsigned char machine_rd(MY_LITTLE_Z80 *z, unsigned int a) {
    a &= 0xffff;
    if (z == ccommando_audio_z && ccommando_audio_rd_hook)
        return ccommando_audio_rd_hook(z, a);
    return z->memory[a];
}

void machine_wr(MY_LITTLE_Z80 *z, unsigned int a, unsigned char v) {
    a &= 0xffff;
    if (z == ccommando_audio_z && ccommando_audio_wr_hook) {
        ccommando_audio_wr_hook(z, a, v);
        return;
    }
    if (a < 0xc000) return;
    z->memory[a] = v;
    if (a == 0xc806) {
        memcpy(sprite_buffer, z->memory + 0xfe00, sizeof sprite_buffer);
        sprite_buffer_valid = 1;
    } else if (a == 0xc800 && ccommando_latch_hook) {
        ccommando_latch_hook(v);
    }
}

unsigned char in_impl(MY_LITTLE_Z80 *z, int port) {
    (void)z;
    (void)port;
    return 0xff;
}

void out_impl(MY_LITTLE_Z80 *z, int port, unsigned char x) {
    (void)z;
    (void)port;
    (void)x;
}
