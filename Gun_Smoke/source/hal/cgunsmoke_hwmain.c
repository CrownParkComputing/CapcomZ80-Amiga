/* src/hal/cgunsmoke_hwmain.c -- native Gun.Smoke driver entry points (hal_game_init /
 * hal_game_frame, called by src/amiga/amiga.s). First-light build: main Z80 + 2x
 * YM2203 chip sound (Paula) + single-playfield hwscroll renderer. Mirrors
 * commando_hwmain.c / c1943_hwmain.c.
 *
 * LOADER: before the game is shown, a classic C64-style loader (src/hal/
 * cgunsmoke_loader.c) brings up the GUN.SMOKE title art revealing cube-by-cube on
 * black with a "sanxion" ProTracker .mod on Paula. The Z80 advances behind it (no
 * render) so the game boots; once the picture has drawn in, a flashing "PRESS FIRE
 * TO CONTINUE" prompt arms, and on fire the chipset is handed to the real renderer
 * + YM audio. Build -DGUNSMOKE_SKIP_LOADER to boot straight into the game. */
#include "z80emu.h"

extern const unsigned char gunsmoke_rom_main[];
extern const unsigned char gunsmoke_rom_snd[];
extern void gunsmoke_load(const unsigned char *maincpu);
extern void gunsmoke_init(MY_LITTLE_Z80 *z);
extern void gunsmoke_run_frame(MY_LITTLE_Z80 *z);
extern void gunsmoke_read_inputs(MY_LITTLE_Z80 *z);
extern void gunsmoke_audio_init(const unsigned char *snd);
extern void gunsmoke_audio_amiga_open(void);
extern void gunsmoke_audio_amiga_frame(void);
extern void gunsmoke_audio_amiga_close(void);
extern void gunsmoke_audio_shutdown(void);
#ifdef GUNSMOKE_DOUBLESTEP
extern void gunsmoke_audio_amiga_begin(void);
extern void gunsmoke_audio_amiga_step(void);
extern void gunsmoke_audio_amiga_commit(void);
#endif
extern void gunsmoke_hw_splash(void);
extern void gunsmoke_hw_open(void);
extern void gunsmoke_hw_frame(MY_LITTLE_Z80 *z);
extern void gunsmoke_hw_wait(void);
extern void gunsmoke_input_suppress_until_release(void);
extern int  gunsmoke_input_pause_toggled(void);
extern int  gunsmoke_input_exit_requested(void);

#ifndef GUNSMOKE_SKIP_LOADER
extern void gunsmoke_loader_open(void);
extern void gunsmoke_loader_frame(void);
extern void gunsmoke_loader_arm(void);
extern int  gunsmoke_loader_is_armed(void);
extern int  gunsmoke_loader_fire(void);
extern void gunsmoke_loader_close(void);
#define LOADER_MIN_FRAMES 30      /* don't accept fire for at least ~0.5s of boot */
/* GUEST THROTTLE (Terra Cresta lesson): the loader steps the Z80 once per loader
 * frame while the reveal runs vblank-paced (gunsmoke_loader_frame() waits one VBL).
 * If the guest frame cost the loader its 50Hz pace, the cube reveal would stretch
 * (TC saw ~200s). Gun.Smoke's single 3MHz Z80 is cheap, so it stays true 50Hz
 * (~6.4s reveal verified), hence RV_GUEST_DIV=1 (run guest every loader frame). Raise
 * to throttle the guest (run it only every Nth loader frame) and keep the reveal
 * vblank-paced should the per-frame guest cost ever dominate. */
#define RV_GUEST_DIV       48
static int  loader_active = 0;
static long loadframes = 0;
#endif

static MY_LITTLE_Z80 z;
static int paused = 0;
static int arcade60_accum = 0;
static int cleaned_up = 0;

static void gunsmoke_run_guest_frame(void)
{
#ifdef GUNSMOKE_DOUBLESTEP
    gunsmoke_run_frame(&z);
    gunsmoke_read_inputs(&z);
    gunsmoke_run_frame(&z);
#elif defined(GUNSMOKE_ARCADE60)
    arcade60_accum += 6;
    while (arcade60_accum >= 5) {
        gunsmoke_run_frame(&z);
        arcade60_accum -= 5;
    }
#else
    gunsmoke_run_frame(&z);
#endif
}

void hal_game_init(void)
{
    cleaned_up = 0;
    arcade60_accum = 0;
    gunsmoke_load(gunsmoke_rom_main);
    gunsmoke_init(&z);
#ifndef GUNSMOKE_NOAUDIO
    gunsmoke_audio_init(gunsmoke_rom_snd);   /* YM2203Init allocs BEFORE any takeover */
#endif
#ifndef GUNSMOKE_SKIP_LOADER
    gunsmoke_loader_open();                   /* title art + mod; game boots behind it */
    loader_active = 1;
#else
    gunsmoke_hw_splash();                      /* take over the display first */
    gunsmoke_hw_open();                        /* build palette from proms */
#ifndef GUNSMOKE_NOAUDIO
    gunsmoke_audio_amiga_open();               /* start Paula playback */
#endif
#endif
}

void hal_game_frame(void)
{
#ifndef GUNSMOKE_SKIP_LOADER
    if (loader_active) {
        /* LOADER PHASE: advance the Z80 (no game render) so the game boots behind the
         * loader, keep the title art + mod alive, draw the picture in cube by cube.
         * The guest step is throttled by RV_GUEST_DIV so the reveal stays vblank-paced. */
        if ((loadframes % RV_GUEST_DIV) == 0) gunsmoke_run_frame(&z);
        gunsmoke_loader_frame();
        if (++loadframes >= LOADER_MIN_FRAMES) gunsmoke_loader_arm();
        /* loader_arm() only flashes the prompt once the picture has fully revealed; we
         * accept fire only after it is actually armed, so the unveil always plays out. */
        if (gunsmoke_loader_is_armed() && gunsmoke_loader_fire()) {
            gunsmoke_loader_close();           /* stop mod + free Paula/CIA + release display */
            gunsmoke_input_suppress_until_release();
            gunsmoke_hw_splash();              /* hand the chipset to the game renderer */
            gunsmoke_hw_open();                /* build palette from proms */
#ifndef GUNSMOKE_NOAUDIO
            gunsmoke_audio_amiga_open();       /* game audio AFTER the mod has stopped */
#endif
            loader_active = 0;
        }
        return;
    }
#endif
    gunsmoke_read_inputs(&z);
    if (gunsmoke_input_pause_toggled()) paused = !paused;
    if (paused) { gunsmoke_hw_wait(); return; }
#ifdef GUNSMOKE_FASTLOCK
#ifndef GUNSMOKE_NOAUDIO
    gunsmoke_audio_amiga_begin();
#endif
    gunsmoke_run_frame(&z);
#ifndef GUNSMOKE_NOAUDIO
    gunsmoke_audio_amiga_step();
#endif
    gunsmoke_read_inputs(&z);
    gunsmoke_run_frame(&z);
#ifndef GUNSMOKE_NOAUDIO
    gunsmoke_audio_amiga_step();
    gunsmoke_audio_amiga_commit();
#endif
#else
    gunsmoke_run_guest_frame();
#ifndef GUNSMOKE_NOAUDIO
    gunsmoke_audio_amiga_frame();              /* render 1 frame of YM PCM -> Paula */
#endif
#endif
    gunsmoke_hw_frame(&z);
}

int hal_game_should_exit(void)
{
    return gunsmoke_input_exit_requested();
}

void hal_cleanup(void)
{
    if (cleaned_up) return;
    cleaned_up = 1;
#ifndef GUNSMOKE_NOAUDIO
    gunsmoke_audio_amiga_close();
    gunsmoke_audio_shutdown();
#endif
}
