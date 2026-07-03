/* blktiger_render.h -- Black Tiger software renderer. Composites the BG tilemap,
 * sprites and text into a 256x224 frame of 10-bit palette indices (0..1023). The
 * host/AGA layer maps indices -> RGB through the xBRG_444 palette RAM. */
#ifndef BLKTIGER_RENDER_H
#define BLKTIGER_RENDER_H
#include "z80emu.h"
#include "blktiger_machine.h"

/* One-time: bit-expand every char/tile/sprite from the gfx ROMs into flat
 * pixel-byte caches. Call ONCE after bt_set_gfx, before the first bt_render. */
void bt_render_init(void);

void bt_render(MY_LITTLE_Z80 *z, unsigned short frame[BT_NH][BT_NW]);

#endif
