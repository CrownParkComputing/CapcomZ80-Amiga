# Capcom Z80 / Dual YM2203 — Amiga RTG Ports

Seven Capcom arcade games built on the classic Z80 + dual-YM2203 hardware
platform, ported to Commodore Amiga with RTG (Retargetable Graphics) output
via Picasso96.

## Games

| Game | Year | Notes |
|------|------|-------|
| **1942** | 1984 | Vertical shooter, ROT270 upright orientation |
| **1943: The Battle of Midway** | 1987 | Follow-up to 1942 |
| **1943 Kai** | 1987 | Alternate / "midway" revision of 1943 |
| **Black Tiger** | 1987 | Platformer / RPG hybrid |
| **Commando** | 1985 | Top-down run-and-gun |
| **Gun.Smoke** | 1985 | Vertical scrolling shooter |
| **Side Arms** | 1986 | Side-scrolling shooter |

## What this is — and what it isn't

These are **not** an emulator in the traditional sense. The original arcade
boards use **Z80 CPUs** — a completely different instruction-set architecture
from the Amiga's Motorola 68000. The original Z80 game code is therefore
**interpreted at runtime by a software Z80 interpreter** (`z80.c`). However,
everything *around* the CPU — rendering, audio mixing, input, and all I/O —
is handled **natively by the Amiga's 68000**. The Z80 never touches the
hardware directly; the Amiga side does.

In other words: the game *logic* runs interpreted, but the *presentation*
(video, sound, controls) is 100% native Amiga code. There is no "guest
machine" being emulated — only the Z80 CPU core is interpreted, and its
memory accesses are routed straight to the native renderer and sound mixer.

## Technology

- **Z80 CPU interpreter** — a software C implementation of the Z80
  instruction set. The original arcade ROM code runs through this interpreter.
- **Dual YM2203 FM/SSG sound chips** — reimplemented in C (`ym/fm.c`),
  mixed to a Paula audio ring buffer. The YM2203 provides three FM channels
  and three SSG (square-wave) channels; each arcade board uses two of them.
- **Capcom tile/sprite video hardware** — reimplemented as a software
  renderer. Background tilemaps, foreground text, and hardware sprites are
  composited in software and blitted to the RTG screen.
- **RGB888 → Picasso96 RTG screen** — output is rendered to an RGB888
  framebuffer and displayed via a Picasso96 RTG screen mode (e.g. uaegfx /
  Zorro III), giving true-colour output on any RTG-capable Amiga or emulator.
- **Paula audio ring buffer** — the mixed YM2203 output is streamed to
  Paula through a ring buffer, providing smooth 44100 Hz stereo audio.
- **Shared menu / launcher** — the Gaplus menu (`shared_source/Gaplus/src`)
  provides a common front-end / launcher shared across the ports.

An optional **ArcadeIntro** module (not included in this repo) can provide an
intro animation sequence. Build scripts reference it via the `AI` environment
variable; if unset, the intro is simply skipped.

## Controls

All games use the same standard **CD32 gamepad** mapping (in port 2):

| CD32 Pad | Action |
|----------|--------|
| D-pad | Move |
| Red (fire) | Fire |
| Blue | Bomb / Special |
| Play | Start |
| Shoulder (L/R) | Coin |
| Esc | Quit |

## Project layout

```
CapcomZ80-Amiga/
├── 1942/
│   ├── source/          # Z80 interpreter, HAL, renderer, cores
│   │   ├── cores/       # z80.c, tables.h (vendored)
│   │   ├── hal/         # game-specific C/HAL + Amiga glue
│   │   ├── amiga/       # 68k startup / amiga.s
│   │   ├── tools/       # build scripts, asset generators
│   │   └── steps/       # development notes
│   └── assets/         # boxart, bezel, loader images, .info icons
├── 1943/                # same structure
├── 1943_Kai/
├── Black_Tiger/
├── Commando/
├── Gun_Smoke/
├── Side_Arms/
└── shared_source/
    ├── make_workbench_icon.py
    └── Gaplus/src/       # shared menu / launcher
```

Each game's `source/` directory is self-contained: it carries its own vendored
copy of the Z80 core (`z80.c`), the YM FM core (`ym/`), and all game-specific
C / 68k assembly source.

## ROM images

**ROM images are not included in this repository.** The original arcade ROMs
are required to build and run the games, but are not distributed here due to
copyright. Each game's `source/` contains the code and tooling to assemble a
ROM blob from original ROM files you provide.

## License

MIT — see [LICENSE](LICENSE). Copyright © 2026 Crown Park Computing Ltd.