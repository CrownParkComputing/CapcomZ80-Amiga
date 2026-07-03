#ifndef COMMANDO_RTG_RENDER_H
#define COMMANDO_RTG_RENDER_H

#include "z80emu.h"
#include <stdint.h>

#define CMD_NW 224
#define CMD_NH 256

void commando_rtg_render_init(void);
void commando_rtg_render(MY_LITTLE_Z80 *z, uint8_t *dst, int dst_stride, int dst_w, int dst_h);

#endif
