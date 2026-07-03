/* c1943_machine.h -- public machine interface for the 1943 RTG native-transcode
 * runtime. Implemented by c1943_native_rust.c: main Z80 and sound Z80 are both
 * Rust-generated native m68k transcodes, with this C API exposing video state to
 * the renderer/presenter.
 */
#ifndef C1943_MACHINE_H
#define C1943_MACHINE_H

#include "z80emu.h"

/* ---- ROM region loader (call before c1943_init) ----
 * maincpu region is 0x30000 bytes: 0x00000-0x07fff fixed ROM, then 8 banks of
 * 0x4000 starting at 0x10000 (bm02+bm03). */
void c1943_load_maincpu(const unsigned char *p /* 0x30000 */);

/* ---- lifecycle ---- */
void c1943_init(MY_LITTLE_Z80 *z);
void c1943_run_frame(MY_LITTLE_Z80 *z);

/* ---- inputs (active-low bytes as the hardware reads them) ----
 * sys: SYSTEM (0xc000) -- bit0 START1, bit1 START2, bit6 COIN1, bit7 COIN2
 *      (bit3 VBLANK is generated internally and ignored here).
 * p1/p2: 0xc001/0xc002 -- bit0 R,1 L,2 D,3 U,4 BTN1,5 BTN2.
 * dswa/dswb: 0xc003/0xc004 dip switches. */
void c1943_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2,
                      unsigned char dswa, unsigned char dswb);

/* ---- video state accessors (read by c1943_render.c) ---- */
const unsigned char *c1943_videoram(MY_LITTLE_Z80 *z);   /* 0xd000, 0x400 (char codes) */
const unsigned char *c1943_colorram(MY_LITTLE_Z80 *z);   /* 0xd400, 0x400 (char attrs) */
const unsigned char *c1943_spritebuf(void);              /* buffered spriteram, 0x1000 */
int c1943_bg1_scrollx(MY_LITTLE_Z80 *z);                 /* 0xd800|d801 (16-bit) */
int c1943_bg1_scrolly(MY_LITTLE_Z80 *z);                 /* 0xd802 (8-bit)  */
int c1943_bg2_scrollx(MY_LITTLE_Z80 *z);                 /* 0xd803|d804 (16-bit) */
int c1943_bg1_on(void);   /* d806 bit4 */
int c1943_bg2_on(void);   /* d806 bit5 */
int c1943_obj_on(void);   /* d806 bit6 */
int c1943_char_on(void);  /* control (0xc804) bit7 */
int c1943_flip(void);     /* control (0xc804) bit6 */

/* ---- introspection ---- */
unsigned c1943_pc(MY_LITTLE_Z80 *z);
int c1943_cur_bank(void);
int c1943_soundlatch(void);

/* ---- raw work-RAM access ---- */
unsigned char c1943_peek(MY_LITTLE_Z80 *z, unsigned a);
void          c1943_poke(MY_LITTLE_Z80 *z, unsigned a, unsigned char v);

#endif /* C1943_MACHINE_H */
