# 01 - Existing boot + decode (reference - NOT redone)

1942 was already a fully working ADF-era port before this modernization. This step
documents the existing, proven pipeline so the later steps can build on it without
re-deriving it. Source of truth: memory `1942-port-facts.md`.

## Hardware
Capcom dual-Z80 (NO MCU/protection) + 2x AY-3-8910. Predecessor of 1943; same
4-layer Capcom video: fg chars + bg tiles + sprites (1942 has a single bg layer
hardware-scrolled; 1943 adds the second bg). ROT270 vertical game.

## Boot (proven, host + on-target)
- Main Z80 on the shared `src/cores/z80.c` interpreter; `src/hal/c1942.c` is the
  machine (memory map, bank switch @0xc806, scroll @0xc802/3, palette bank @0xc805,
  soundlatch @0xc800, spriteram 0xcc00, fg vram 0xd000, bg vram 0xd800, IRQs
  RST08 mid-frame + RST10 vblank).
- Main map: 0x0000-7fff fixed ROM (srb-03+srb-04), 0x8000-bfff banked ROM
  (banks at region 0x10000: srb-05/06/07), inputs 0xc000-c004 (DSWA/DSWB default
  0xff = attract; 0x00 forces the RAM-test screen).
- Audio Z80 (sr-01.c11) + 2x AY8910 -> Paula (`c1942_audio.c` + `c1942_audio_amiga.c`);
  AY register writes captured via z80.c `-DWR_LOG_HOOK`.

## Video decode recipe (matches MAME capcom/1942_v.cpp, validated)
- gfx1 chars sr-02.f2: 8x8 2bpp, transp pixel 0. fg layer is a STATIC 32x32 char
  map drawn WITHOUT scrollx -> it is the HUD/score/text field.
- gfx2 tiles sr-08..13: 16x16 3bpp, bg layer, hardware-scrolled by scroll[0..1].
- gfx3 sprites sr-14..17: 16x16 4bpp, transp pen 15, drawn over bg, reverse order
  for priority.
- PROMs sb-5/6/7 = R/G/B 8-bit guns (weights 0x0e/0x1f/0x43/0x8f); charLUT sb-0,
  tileLUT sb-4, sprLUT sb-8. pen->indirect: char 0x80|lchr[col*4+pix],
  bg ((palbank&3)<<4)|ltile[(col&0x1f)*8+pix], spr 0x40|lspr[col*16+pix].
- BIT-ORDER: plane[0] is the MSB (MAME drawgfx) -> emit MSB-first.

## Working build (the baseline this modernization must not regress)
- `tools/build_1942_amiga.sh` -> `build/c1942_native` (281K exe) + `build/c1942.adf`
  + `build/c1942_boot/`. Config `Configurations/1942-ADF.uae` (A1200 AGA, floppy boot).
- On-target proven: upright ROT90 render, correct 8-bit palette (2-pass LOCT),
  static HUD, white text, sprites, branding ("WHITTY ARCADE 2026"), audio by ear.
- Host A/B oracle: `tools/c1942_shot.c` (decode reference, == MAME).

Nothing in this step was changed. The renderer is a SOFTWARE ROT90 compositor
(`c1942_render.c`: composite indirect pens into a chunky buffer, then C2P to 8 AGA
bitplanes) - NOT the hwscroll hardware-scroll / hardware-sprite path used by
Commando/1943. That distinction drives step 02.
