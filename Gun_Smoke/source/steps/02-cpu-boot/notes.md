# Step 2 — Host CPU Boot (main Z80)

Goal: run the **real** Gun.Smoke main-Z80 ROM on a host harness and prove it boots
into attract, driving VRAM / scroll / sprites — before any Amiga work.

## What was built
- `src/gunsmoke.c` — the machine: memory map, I/O ports, bank switching, IRQ,
  VBLANK bit, protection table. Modelled on `pacland-amiga/src/hal/c1943.c`.
  Exposes `machine_rd`/`machine_wr` (the globals `cores/z80.c` calls) plus a small
  introspection API (`gunsmoke_pc/scrollx/scrolly/chon/bgon/objon/...`).
- `tools/gunsmoke_host.c` — boot-trace driver: loads the maincpu region, runs N
  frames pulsing VBLANK, dumps PC/bank/scroll/layer + memory fill counts.
- Z80 core reused verbatim from `pacland-amiga/src/cores/{z80.c,z80emu.h,tables.h}`
  (copied into `cores/`).

Build:
```
gcc -O2 -Isrc -Icores tools/gunsmoke_host.c src/gunsmoke.c cores/z80.c -o build/gunsmoke_host
./build/gunsmoke_host [frames]
```

## Key machine details (vs the 1943 template)
- **Bank window**: 4 banks of 0x4000 at region **0x8000** (1943 had 8 banks at
  0x10000). `bank = (control >> 2) & 3` from `control_w` @ 0xc804.
- **Layer enables are split**: `chon` is in `control_w` bit7 (0xc804); `bgon`/`objon`
  + `sprite3bank` are in `layer_w` (0xd806). 1943 had everything in one `layer` reg.
- **Protection** is a fixed 3-byte table at **0xc4c9-0xc4cb** = `{0xff,0x00,0x00}`
  (1943's was a single MCU/bootleg byte at 0xc007). Implemented verbatim.
- **One IRQ/frame** (RST38 at vblank), matching `irq0_line_hold`.
- **VBLANK** is SYSTEM (0xc000) bit3, active-low; the host pulses it each frame.

## Boot result — IT BOOTS (see `boot_trace.txt`)
```
@60   : ctrl=80 layer=10  chon=1 bgon=1 objon=0   videoram 896/1024   warning screen
@300  : ctrl=80 layer=10  chon=1 bgon=1 objon=0   colorram filling     warning screen
@600  : scrollx=6656 layer=30 objon=1            spriteram>0          title + demo
@900  : scrollx=6175 chon=1 bgon=1 objon=1 spr=37 workRAM 192          attract gameplay
@1500 : scrollx=6323 ... objon=1                                       attract gameplay
```
Proof of a healthy boot:
- `chon=1, bgon=1` immediately (char + bg layers enabled),
- `videoram` heavily written (≈890/1024 — the text/HUD + warning),
- by ~600 frames `objon=1` and **spriteram is non-zero** (sprite DMA running),
- `scrollx` advances into the thousands (the bg level is scrolling = attract demo),
- `workRAM` populated (game state), bank stays 0 during attract.

The sampled PC idles low (~0x00c2, the vblank-sync wait loop) because all per-frame
game work runs inside the RST38 IRQ handler — normal for this engine. The renderer
state advancing every frame confirms the IRQ-driven main loop is executing the real
attract logic.

## Notes for later steps
- Sound Z80 not run yet (not needed for boot/video). `machine.c` already latches
  `soundlatch` @ 0xc800 for the step-6 audio path. `gunsmoke_sync_io()` is provided
  for the future native-transcode path (lifts scroll/layer/bank/inputs written
  straight into z->memory, mirroring c1943_sync_io).
- DSW defaults: DSW1=0xf7 (upright, demo on), DSW2=0xff (1C/1C, continue, demo
  sounds). Inputs idle 0xff (active-low).
