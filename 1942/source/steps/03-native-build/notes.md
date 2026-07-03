# 03 - Native build (off the ADF constraint)

Move 1942 from the 880KB ADF floppy build to a native A1200/AGA directory-FS boot,
like Commando / 1943 / Terra Cresta. No floppy size limit -> a richer renderer is
affordable.

## What was added
- `tools/build_1942_native.sh` - same engine + assets as `build_1942_amiga.sh`, but:
  - also compiles `src/hal/render_router.c` and links `render_router.o`;
  - stages a **directory-FS boot** `build/c1942_native_boot/` (with
    `s/startup-sequence` = `SYS:c1942`) instead of writing an ADF.
  - still emits the exe `build/c1942_native`.
- `/home/jon/Amiberry/Configurations/1942.uae` - A1200 / AGA / 68020, `cpu_speed=max`,
  `chipmem=4 / fastmem=8`, `cachesize=8192` (JIT), Kickstart 3.1. Boots the dir-FS:
  ```
  filesystem2=rw,DH0:C1942:/home/jon/pacland-amiga/build/c1942_native_boot,0
  uaehf0=dir,rw,DH0:C1942:/home/jon/pacland-amiga/build/c1942_native_boot,0
  nr_floppies=0
  ```
  Inputs: `joyport0=none` (avoids the joyport0=mouse click-grab freeze),
  `joyport1=joy0` (gamepad on Amiga port 2 = 1942 P1). `sound=1`.

## Build result
`bash tools/build_1942_native.sh` links `build/c1942_native` (305,640 bytes; +~1.2KB
vs the ADF exe for render_router) and stages `build/c1942_native_boot/`. Clean build
with the m68k-amigaos-gcc / vasm / vlink toolchain.

The old ADF path (`build_1942_amiga.sh`, `1942-ADF.uae`) is left untouched as the
fallback baseline.
