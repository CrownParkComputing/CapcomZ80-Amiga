# 1943 Kai

Current Amiga build: RTG native-transcode presenter package.

## Run

Launch `1943 Kai` from the game folder or the AGS shared launcher.

## Controls

- Stick / cursor keys: move
- Fire / CD32 Blue or Red / Ctrl / Space: shot
- Second fire / CD32 Yellow or Green / Alt: special weapon / roll
- `5` / CD32 shoulder: coin
- `1` / Return / CD32 Play: start
- `P` / CD32 Yellow+Green: pause
- `Q` / hold all four CD32 face buttons: exit

## Status

1943 Kai runs on the same Capcom Z80 / Dual YM2203 hardware family as 1943: a
main Z80 with banked program ROMs, two scrolling backgrounds, sprites, text,
PROM colour tables, and a sound Z80 driving two YM2203 chips. This clean package
uses the Kai ROM map, a Kai-specific main-Z80 native m68k transcode, and a
Kai-specific sound-Z80 transcode generated from `1943kai.05`.

The RTG presenter renders the native 256x224 arcade frame into a static 864x486
bezel backdrop and refreshes only the scaled play rectangle with
WriteChunkyPixels. Parallax scrolling and the text/logo layer are restored, and
the YM2203 pair is mixed to Paula at 8040 Hz (134 samples/frame, period 441).
The Kai native main-Z80 transcode includes the same `0xc806` sprite-DMA hook as
the plain 1943 build so the renderer sees the buffered object state.

## Box art

No commercial box art is currently bundled for this title — add one to `assets/` when available.
