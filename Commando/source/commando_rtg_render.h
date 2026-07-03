#ifndef COMMANDO_RTG_RENDER_H
#define COMMANDO_RTG_RENDER_H

#include "capcom_z80_video.h"
#include "z80emu.h"
#include <stdint.h>

#define CMD_NW CAPCOM_Z80_GAME_W
#define CMD_NH CAPCOM_Z80_GAME_H

void commando_rtg_render_init(void);
void commando_rtg_render(MY_LITTLE_Z80 *z, uint8_t *dst, int dst_stride, int dst_w, int dst_h);

#endif
