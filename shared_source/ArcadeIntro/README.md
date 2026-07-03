# ArcadeIntro

A standalone, game-agnostic **Amiga RTG/chunky "loader" intro** you can tag onto
any of the arcade ports. Old-school demo style (Red Sector / Kefrens vibe): a
copper-gradient backdrop, starfield, a **rotating multi-coloured twister bar**, a
big wavy title and a **sine-scrolling marquee**, with **ProTracker music** — while
it decodes the game's ROMs behind the scenes and only lets the player in on
FIRE/START once they're ready.

Extracted and generalised from the Tiger-Heli RTG loader (the playable mock-game
was dropped — this is a pure intro).

## Files
- `arcade_intro.h` / `arcade_intro.c` — the loader (portable C, draws into the
  host's chunky framebuffer, presents via Picasso96 `WriteChunkyPixels`).
- `arcade_intro_glue.s` — supervisor VBR fetch (`ai_get_vbr`) for the player.
- `tc_ptplayer.68k` / `tc_ptplayer_glue.s` — the shared ProTracker player.
- `intro_mod.s` + `intro_default.mod` — the embedded music (`ai_default_mod`).

## API
```c
#include "arcade_intro.h"

ai_init(scr, win, rtg_frame, rtg_w, rtg_h);   /* bind your open RTG surface */

static const char *const keys[] = { "SPACE FIRE", "5 COIN", "1 START", 0 };
static const char *const pad[]  = { "RED FIRE",   "L COIN", "PLAY START", 0 };
extern const unsigned char ai_default_mod[], ai_default_mod_end[];

static const ai_config cfg = {
    "MY GAME",                       /* title  */
    "SCROLLER TEXT ....    ",        /* marquee */
    keys, pad,
    ai_default_mod, ai_default_mod_end,
    0,                               /* palette: 0 = built-in 16-colour demo set */
    220,                             /* min loader frames before FIRE accepted   */
    my_ready, my_warmup, 0           /* ready()=ROMs decoded?  warmup()=run a frame */
};

if (!ai_run(&cfg)) { /* user quit at the loader */ }
/* else: ROMs are decoded, music stopped, screen handed back — start the game */
```

The host supplies two callbacks:
- `warmup(ctx)` — called once per loader frame; do your one-time ROM decode on
  the first call (showing "DECODING ROMS"), then advance the game model so it's
  warm by the time the player presses FIRE.
- `ready(ctx)` — return 1 once the decode is done. FIRE is ignored until both
  `ready()` is true **and** `min_ticks` frames have elapsed (then "READY").

## Build (m68k-amigaos)
```sh
AI=../../shared_source/ArcadeIntro
m68k-amigaos-gcc -m68020 -noixemul -O2 -I "$AI" -c "$AI/arcade_intro.c" -o arcade_intro.o
m68k-amigaos-as -m68020 "$AI/arcade_intro_glue.s" -o arcade_intro_glue.o
m68k-amigaos-as -m68020 "$AI/tc_ptplayer.68k"     -o tc_ptplayer.o
m68k-amigaos-as -m68020 "$AI/tc_ptplayer_glue.s"  -o tc_ptplayer_glue.o
vasmm68k_mot -I "$AI" -m68020 -phxass -Fhunk -o intro_mod.o "$AI/intro_mod.s"
# link all five objects with your port; compile your own sources with -I "$AI".
```
Swap the music by replacing `intro_default.mod` (any 4-channel ProTracker .mod),
or embed your own and pass it via `ai_config.mod`/`mod_end`.

## First user
Sky Kid (RTG) — see `AmigaArcadePorts/SkyKid/src/skykid_rtg_main.c`
(`sk_intro_cfg`, `sk_intro_warmup`, `sk_intro_ready`) and its `build.sh`.
