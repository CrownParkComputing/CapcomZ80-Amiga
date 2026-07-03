# 1942 — A1200 AGA native port

Native Amiga **A1200 (AGA)** port of Capcom's **1942** (1984).

The arcade main CPU (Z80) runs through the shared `z80.c` core; the background
tilemap, sprites and foreground text are composited into 8 AGA bitplanes in the
upright (ROT270) portrait orientation by the native renderer
(`src/hal/c1942_render.c` + `render_router.c` + `c1942_video.c`). AY-3-8910
sound writes are captured from the CPU and replayed through Paula.

## Run

```sh
amiberry -f "/home/jon/pacland-amiga/AmigaArcadePorts/1942/1942.uae" -s use_gui=no
```

Boots the self-contained bootable hardfile `dist/1942.hdf`. Requires a
**Kickstart 3.1 (40.068)** A1200 ROM (edit `kickstart_rom_file=`). If you move
the folder, fix the absolute `hardfile2=`/`uaehf0=` paths in `1942.uae`.

## Controls

CD32-style gamepad in **port 2** (`joyport1=cd32joy`), or keyboard:

| Input | Action |
|-------|--------|
| Stick / cursor keys | Move |
| Fire / CD32 face buttons / L-Ctrl / Space | Shoot |
| CD32 face buttons / L-Alt | Loop-de-loop (roll) |
| CD32 Start/Play, Xbox Start, or **1** / Return | Start |
| CD32 shoulder/Select, Xbox Back, or **5** / Esc | Insert coin |

## Folder layout

- `dist/1942.hdf` — bootable FFS hardfile (the deliverable).
- `1942.uae` — amiberry config (boots the HDF in `dist/`).
- `prebuilt/` — verified Amiga executable (`c1942`) + `s/startup-sequence`.
- `src/` — game source (HAL + shared `z80.c` core, build/make scripts under `src/tools/`).
- `roms/1942.zip` — the 1942 ROM set (your own dump).
- `screenshots/` — in-game captures.

## Rebuild (reference)

`src/tools/build_1942_amiga.sh` (m68k-amigaos toolchain; ROM set staged under
`games/1942/roms/`). Script paths are relative to the original monorepo root.
