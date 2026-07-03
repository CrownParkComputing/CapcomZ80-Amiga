# Side Arms

Current Amiga build: RTG presenter and interpreter package.

## Run

Launch `Side Arms` from the game folder or the AGS shared launcher.

## Controls

- Stick / cursor keys: move
- Fire / CD32 Blue or Red / Ctrl / Space: shoot forward
- Second fire / CD32 Yellow or Green / Alt: shoot backward
- `5` / CD32 shoulder: coin
- `1` / Return / CD32 Play: start
- `P` / CD32 Yellow+Green: pause
- `Q` / hold all four CD32 face buttons: exit

## Status

Side Arms is a Capcom horizontal Z80 board: interpreted main/audio Z80s with
banked program ROMs, RAM-driven xBRG palette RAM, background tilemaps, sprites,
text, and a sound board of a sound Z80 plus two YM2203 chips. The Amiga build is
the RTG/bezel version: the game renders to a software 384x224 arcade frame,
scales into the Bezel Project playfield, and displays through a fixed RGB332
8-bit palette (which replaced an earlier dynamic palette reducer that could
flash sprite blocks). The YM2203 pair is mixed to Paula as signed 8-bit mono at
8040 Hz through the sample-clocked lead-buffer ring. Rebuild from
`source/build.sh`; this stages ROM blobs when needed and emits
`dist/SideArms_RTG.hdf` and `dist/SideArms-RTG.uae`.
