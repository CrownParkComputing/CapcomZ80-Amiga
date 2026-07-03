#ifndef SIDEARMS_RENDER_H
#define SIDEARMS_RENDER_H

#include "z80emu.h"
#include "sidearms_machine.h"

void sa_set_gfx(const unsigned char *chars, const unsigned char *tiles,
                const unsigned char *sprites, const unsigned char *tilemap);
void sa_render_init(void);
void sa_render(MY_LITTLE_Z80 *z, unsigned short frame[SA_NH][SA_NW]);

#endif
