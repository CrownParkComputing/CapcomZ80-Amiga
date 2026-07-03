/* src/hal/c1942_amain.c -- 1942 Amiga driver. amiga_main calls hal_game_init()
 * once then hal_game_frame() in a loop. Runs the main Z80 (real ROMs embedded via
 * c1942_romdata.s), renders bg/sprite/fg into 7 AGA planes (upright), presents. */
#include "z80emu.h"
#include "c1942_render.h"

extern const unsigned char c1942_rom_main[];   /* 0x20000 maincpu region */
extern const unsigned char c1942_rom_snd[];    /* 0x4000 audio Z80 program  */
extern void c1942_load(const unsigned char *maincpu);
extern void c1942_init(MY_LITTLE_Z80 *z);
extern void c1942_run_frame(MY_LITTLE_Z80 *z);
extern void c1942_video_open(void);
extern void c1942_present(void);
extern void c1942_read_inputs(void);
extern void c1942_audio_init(const unsigned char *snd);
extern void c1942_audio_amiga_open(void);
extern void c1942_audio_amiga_frame(void);
extern void c1942_render_prealloc(void);
extern void c1942_loading(int phase);
extern unsigned char c1942_peek(MY_LITTLE_Z80 *z, unsigned a);
extern void c1942_poke(MY_LITTLE_Z80 *z, unsigned a, unsigned char v);
extern int c1942_input_pause_toggled(void);
extern int c1942_input_exit_requested(void);

static MY_LITTLE_Z80 z;
static int booted = 0, bootframes = 0;
static int paused = 0;

/* Replace the attract "(c)1984 CAPCOM" line with custom branding. The copyright
 * sits in fg tilemap column 2 (read top->bottom = display left->right); its (c)
 * glyph is code 0x38 at tile (row 9, col 2). Font: 0x00-09='0'-'9',
 * 0x0a='A'..0x23='Z', 0x30=space. "WHITTY ARCADE 2026" (18 glyphs) is centred
 * over rows 7..24. Re-applied every frame the copyright is on screen. */
static void c1942_brand(void)
{
    if (c1942_peek(&z, 0xd000 + 9*32 + 2) != 0x38) return;     /* (c) glyph present? */
    static const unsigned char txt[18] = {
        0x20,0x11,0x12,0x1d,0x1d,0x22, 0x30,        /* W H I T T Y _ */
        0x0a,0x1b,0x0c,0x0a,0x0d,0x0e, 0x30,        /* A R C A D E _ */
        0x02,0x00,0x02,0x06 };                       /* 2 0 2 6       */
    unsigned char col = c1942_peek(&z, 0xd000 + (11*32+2) + 0x400) & 0x3f;  /* copyright colour */
    for (int i = 0; i < 18; i++) {
        unsigned cell = 0xd000 + (7 + i)*32 + 2;
        c1942_poke(&z, cell,          txt[i]);
        c1942_poke(&z, cell + 0x400,  col);          /* colour, code-high bit clear */
    }
}

void hal_game_init(void)
{
    c1942_load(c1942_rom_main);
    c1942_init(&z);
    c1942_audio_init(c1942_rom_snd);
    c1942_render_prealloc();
    c1942_video_open();
    c1942_audio_amiga_open();        /* Paula playback (after display DMA is up) */
}

void hal_game_frame(void)
{
    c1942_read_inputs();        /* Amiga joystick + keyboard -> 1942 inputs */
    if (c1942_input_pause_toggled()) paused = !paused;
    if (paused) { c1942_render_planes(&z); c1942_present(); return; }
    c1942_run_frame(&z);
    c1942_brand();              /* WHITTY ARCADE 2026 over the (c)1984 CAPCOM line */
    c1942_audio_amiga_frame();  /* run audio CPU + AY, play one frame via Paula */
    if (!booted) {
        /* Slow interp warm-up: the boot RAM-test leaves stable-but-black patterns
         * in vram, so "vram non-zero" can't tell black from a drawn attract. The
         * only reliable signal is actual VISIBLE composited content. Compositing
         * is costly, so only test it every 8th frame; show the animated loading
         * bar the rest of the time so the screen is never just black. */
        bootframes++;
        if ((bootframes & 7) == 0) {
            if (c1942_render_planes(&z) >= 24) { booted = 1; c1942_present(); return; }
        }
        if (bootframes > 4000) booted = 1;              /* give up -> show whatever */
        else { c1942_loading(bootframes); return; }
    }
    c1942_render_planes(&z);
    c1942_present();
}

int hal_game_should_exit(void)
{
    return c1942_input_exit_requested();
}
