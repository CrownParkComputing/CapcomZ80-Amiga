# Black Tiger

Current Amiga build: RTG presenter and interpreter package.

## Run

Launch `Black Tiger` from the game folder or the AGS shared launcher.

## Controls

- Stick / cursor keys: move
- Fire / CD32 Blue or Red / Ctrl / Space: attack
- Second fire / CD32 Yellow or Green / Alt: jump
- `5` / CD32 shoulder: coin
- `1` / Return / CD32 Play: start
- `P` / CD32 Yellow+Green: pause
- `Q` / hold all four CD32 face buttons: exit

## Status

Black Tiger runs the Capcom Z80 hardware with interpreted main/audio Z80s:
banked program ROMs, banked background RAM, sprite RAM, text RAM, xBRG palette
RAM, and the family sound board of a sound Z80 plus two YM2203 chips. The Amiga
build is an RTG presenter that opens a fixed 864x486 custom RTG screen, paints
the Bezel Project backdrop once, then refreshes only the aspect-correct 537x470
play rectangle with WriteChunkyPixels. The display uses fixed RGB332 so palette
changes stay cheap, and the renderer applies the Capcom background split-priority
masks (BG back, sprites, BG front, then text). The two YM2203 chips are mixed to
Paula at 7920 Hz through the sample-clocked lead-buffer ring. Rebuild from
`source/build.sh`; this also emits `dist/BlackTiger_RTG.hdf` and
`dist/BlackTiger-RTG.uae`.
