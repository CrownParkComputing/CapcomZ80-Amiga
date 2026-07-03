#!/bin/bash
# Build the 1942 Amiga executable -> build/c1942_native + bootable dir-FS + HDF.
# Main Z80 interpreted (z80.c), bg/sprite/fg -> 7 AGA planes (upright/ROT270).
set -e
export PATH="${AMIGA_GCC_PATH:-$HOME/.local/bin}:$PATH"
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$HERE"
B=build/c1942_amiga
mkdir -p "$B"

echo "== embed ROM/gfx/PROM blobs =="
python3 src/tools/make_1942rom.py

echo "== compile C =="
GCC="m68k-amigaos-gcc -m68030 -noixemul -O2 -fomit-frame-pointer -DNDEBUG -I src/cores -I src/hal"
$GCC -DWR_LOG_HOOK -DZ80_MAP_1942 -c src/cores/z80.c -o "$B/z80.o"
$GCC -c src/hal/c1942.c           -o "$B/c1942.o"
$GCC -c src/hal/render_router.c   -o "$B/render_router.o"
$GCC -c src/hal/c1942_render.c    -o "$B/c1942_render.o"
$GCC -c src/hal/c1942_video.c     -o "$B/c1942_video.o"
$GCC -c src/hal/c1942_input.c     -o "$B/c1942_input.o"
$GCC -c src/hal/c1942_audio.c -o "$B/c1942_audio.o"
$GCC -c src/hal/c1942_audio_amiga.c -o "$B/c1942_audio_amiga.o"
$GCC -c src/hal/c1942_amain.c     -o "$B/c1942_amain.o"

echo "== assemble glue + embedded data =="
VASM="vasmm68k_mot -I src -I src/amiga -I src/hal -I build/r1942 -m68000 -phxass -nowarn=62 -Fhunk"
$VASM -o "$B/slave.o"       src/slave.s
$VASM -o "$B/amiga.o"       src/amiga/amiga.s
$VASM -o "$B/hal_sysvars.o" src/hal/hal_sysvars.s
$VASM -o "$B/pl_support.o"  src/hal/pl_support.s
$VASM -o "$B/romdata.o"     src/hal/c1942_romdata.s

echo "== link =="
FAST_HUNK=4
vlink -b amigahunk -Bstatic -Cexestack -mrel -sc \
    -hunkattr code=$FAST_HUNK -hunkattr .text=$FAST_HUNK -hunkattr text=$FAST_HUNK \
    -hunkattr data=$FAST_HUNK -hunkattr .data=$FAST_HUNK \
    -hunkattr bss=$FAST_HUNK -hunkattr .bss=$FAST_HUNK \
    -o build/c1942_native \
    "$B/slave.o" "$B/amiga.o" "$B/hal_sysvars.o" "$B/pl_support.o" \
    "$B/c1942_amain.o" "$B/c1942_input.o" "$B/c1942_video.o" "$B/c1942_render.o" \
    "$B/render_router.o" "$B/c1942.o" \
    "$B/c1942_audio.o" "$B/c1942_audio_amiga.o" "$B/z80.o" "$B/romdata.o"
ls -la build/c1942_native

echo "== stage boot dir + HDF =="
rm -rf build/c1942_boot; mkdir -p build/c1942_boot/s
cp build/c1942_native build/c1942_boot/c1942
printf 'SYS:c1942\n' > build/c1942_boot/s/startup-sequence
printf 'SYS:c1942\n' > "$B/startup-sequence"
rm -f build/1942.hdf
xdftool build/1942.hdf create size=4M \
    + format "1942" ffs \
    + boot install \
    + write build/c1942_native c1942 \
    + makedir s + write "$B/startup-sequence" s/startup-sequence
mkdir -p prebuilt/s dist
cp -f build/c1942_native prebuilt/c1942
cp -f "$B/startup-sequence" prebuilt/s/startup-sequence
cp -f build/1942.hdf dist/1942.hdf
echo "DONE -> build/c1942_native + build/c1942_boot/ + build/1942.hdf"
echo "PACKAGED -> prebuilt/c1942 + dist/1942.hdf"
