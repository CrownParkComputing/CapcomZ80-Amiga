#ifndef SIDEARMS_MACHINE_H
#define SIDEARMS_MACHINE_H

#include "z80emu.h"

#define SA_NW 384
#define SA_NH 224

void csidearms_load(const unsigned char *maincpu);
void csidearms_init(MY_LITTLE_Z80 *z);
void csidearms_run_frame(MY_LITTLE_Z80 *z);
void csidearms_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2);
void csidearms_set_dsw(unsigned char d0, unsigned char d1, unsigned char d2);
int csidearms_scrollx(void);
int csidearms_scrolly(void);
int csidearms_control(void);
int csidearms_gfxctrl(void);
unsigned csidearms_pc(MY_LITTLE_Z80 *z);
unsigned char csidearms_peek(MY_LITTLE_Z80 *z, unsigned a);

static inline void sa_load_maincpu(const unsigned char *p) { csidearms_load(p); }
static inline void sa_init(MY_LITTLE_Z80 *z) { csidearms_init(z); }
static inline void sa_run_frame(MY_LITTLE_Z80 *z) { csidearms_run_frame(z); }
static inline const unsigned char *sa_palette(MY_LITTLE_Z80 *z) { return z->memory + 0xc000; }
static inline void sa_set_inputs(unsigned char sys, unsigned char p1, unsigned char p2,
                                 unsigned char dsw0, unsigned char dsw1)
{
    csidearms_set_inputs(sys, p1, p2);
    csidearms_set_dsw(dsw0, dsw1, 0xff);
}

#endif
