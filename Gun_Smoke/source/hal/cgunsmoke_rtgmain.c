/* cgunsmoke_rtgmain.c -- interpreter Gun.Smoke driver for RTG reference build. */
#include "z80emu.h"

extern const unsigned char gunsmoke_rom_main[];
extern void gunsmoke_load(const unsigned char *maincpu);
extern void gunsmoke_init(MY_LITTLE_Z80 *z);
extern void gunsmoke_run_frame(MY_LITTLE_Z80 *z);
extern void gunsmoke_rtg_open(void);
extern void gunsmoke_rtg_read_inputs(void);
extern void gunsmoke_rtg_frame(MY_LITTLE_Z80 *z);
extern int  gunsmoke_rtg_paused(void);
extern int  gunsmoke_rtg_exit_requested(void);

static MY_LITTLE_Z80 z;

void hal_game_init(void)
{
    gunsmoke_load(gunsmoke_rom_main);
    gunsmoke_init(&z);
    gunsmoke_rtg_open();
}

void hal_game_frame(void)
{
    gunsmoke_rtg_read_inputs();
    if (!gunsmoke_rtg_paused())
        gunsmoke_run_frame(&z);
    gunsmoke_rtg_frame(&z);
}

int hal_game_should_exit(void)
{
    return gunsmoke_rtg_exit_requested();
}
