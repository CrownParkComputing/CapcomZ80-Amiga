/* blktiger_machine.h -- Capcom Black Tiger (1987) machine model: main Z80 +
 * banked ROM + banked bg-video RAM + port I/O + video state. Reimplemented from
 * the MAME blktiger.cpp spec. Single-game native port (SkyKid/Gaplus method),
 * shares the public-domain z80.c core (built with -DZ80_MAP_BLKTIGER). */
#ifndef BLKTIGER_MACHINE_H
#define BLKTIGER_MACHINE_H
#include "z80emu.h"

#define BT_NW 256          /* native visible width  */
#define BT_NH 224          /* native visible height (horizontal game, ROT0) */

/* ---- ROM region loaders (call before bt_init) ---- */
void bt_load_maincpu(const unsigned char *p /* 0x50000: fixed 0x8000 + 16x0x4000 banks */);
void bt_set_gfx(const unsigned char *chars   /* 0x8000  */,
                const unsigned char *tiles   /* 0x40000 */,
                const unsigned char *sprites /* 0x40000 */);

void bt_init(MY_LITTLE_Z80 *z);
void bt_run_frame(MY_LITTLE_Z80 *z);

/* DSW0/DSW1 + inputs are ACTIVE LOW (1 = released/off) */
void bt_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2,
                   unsigned char dsw0, unsigned char dsw1);

/* ---- video state accessors (read by blktiger_render.c) ---- */
const unsigned char *bt_scrollram(void);             /* 0x4000 banked BG attr RAM (2 bytes/tile) */
const unsigned char *bt_txram(MY_LITTLE_Z80 *z);     /* 0xd000, 0x800 (text code+attr) */
const unsigned char *bt_palette(MY_LITTLE_Z80 *z);   /* 0xd800, 0x800: lo[0x400]@d800, hi[0x400]@dc00 */
const unsigned char *bt_spritebuf(void);             /* buffered spriteram, 0x200 (128 sprites x4) */
const unsigned char *bt_chars(void);
const unsigned char *bt_tiles(void);
const unsigned char *bt_sprites(void);

int bt_scrollx(void), bt_scrolly(void);
int bt_screen_layout(void);     /* 0 => 4x8 (64x128 tall) map, !=0 => 8x4 (128x64 wide) map */
int bt_chon(void), bt_bgon(void), bt_objon(void), bt_flip(void);

/* host debug */
unsigned bt_pc(MY_LITTLE_Z80 *z);
int bt_cur_bank(void);

/* sound: last byte the main CPU wrote to the soundlatch (OUT port 0x00) */
unsigned char bt_soundlatch(void);

#endif
