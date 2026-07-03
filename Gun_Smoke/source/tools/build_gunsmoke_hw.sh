#!/bin/bash
# Build NATIVE Gun.Smoke (Capcom 1985) for Amiga AGA -> build/gunsmoke_hw + a bootable
# directory filesystem build/gunsmokehw_boot/. Reuses the generic hwscroll engine, the
# shared z80.c (-DZ80_MAP_GUNSMOKE fast-path map), fm.c YM2203 OPN core, and the
# slave.s/amiga.s/hal glue AS-IS. Single-playfield software-sprite first-light renderer
# (src/hal/cgunsmoke_hwrender.c): main Z80 + 2x YM2203 Paula sound + attract tilemap/
# sprites/fg. Mirrors tools/build_commando_hw.sh.
set -e
export PATH="${AMIGA_GCC_PATH:-$HOME/.local/bin}:$PATH"
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$HERE"
B=build/gunsmokehw_obj
mkdir -p "$B"

echo "== stage ROM/gfx/PROM blobs =="
python3 src/tools/make_gunsmoke.py

echo "== stage LOADER assets: GUN.SMOKE title image (-> bitplanes) + sanxion .mod =="
# Drop assets/gunsmoke_loader.png (320x256) to override the title art.
LOADER_COLORS=${LOADER_COLORS:-64} python3 src/tools/make_gs_loader_img.py
mkdir -p build/gunsmoke_loader
cp assets/gunsmoke_loader.mod build/gunsmoke_loader/gunsmoke_loader.mod

echo "== compile C =="
GCC="m68k-amigaos-gcc -m68030 -noixemul -O2 -fomit-frame-pointer -DNDEBUG -DHWS_SPR_W=64 -I src/cores -I src/hal"
YMDEF="-DHAS_YM2203=1 -DHAS_YM2608=0 -DHAS_YM2610=0 -DHAS_YM2610B=0 -DHAS_YM2612=0 -DHAS_YM3438=0"
# z80.c built with -DZ80_MAP_GUNSMOKE so the audio CPU's YM regs at 0xe000-0xe003 trap
# to machine_wr (writing them to RAM would silently kill all sound).
$GCC -DZ80_MAP_GUNSMOKE -c src/cores/z80.c      -o "$B/z80.o"
$GCC -c src/hal/cgunsmoke.c                      -o "$B/cgunsmoke.o"
$GCC -c src/hal/cgunsmoke_hwrender.c             -o "$B/cgunsmoke_hwrender.o"
$GCC -c src/hal/hwscroll.c                       -o "$B/hwscroll.o"
$GCC -c src/hal/cgunsmoke_input.c                -o "$B/cgunsmoke_input.o"
$GCC -c src/hal/cgunsmoke_firedir.c              -o "$B/cgunsmoke_firedir.o"
$GCC -c src/hal/cgunsmoke_hwmain.c               -o "$B/cgunsmoke_hwmain.o"
# LOADER: C64-style title reveal + sanxion .mod (LOADER_MUSIC=1 plays the mod via ptplayer).
$GCC -DLOADER_MUSIC -c src/hal/cgunsmoke_loader.c -o "$B/cgunsmoke_loader.o"
$GCC $YMDEF -I src/cores/ym -c src/cores/ym/fm.c -o "$B/fm.o"
$GCC $YMDEF -I src/cores/ym -c src/hal/cgunsmoke_audio.c -o "$B/cgunsmoke_audio.o"
$GCC -c src/hal/cgunsmoke_audio_amiga.c          -o "$B/cgunsmoke_audio_amiga.o"

echo "== assemble glue + embedded data =="
VASM="vasmm68k_mot -I src -I src/amiga -I src/hal -I build/rgunsmoke -m68000 -phxass -nowarn=62 -Fhunk"
$VASM -o "$B/slave.o"       src/slave.s
$VASM -o "$B/amiga.o"       src/amiga/amiga.s
$VASM -o "$B/hal_sysvars.o" src/hal/hal_sysvars.s
$VASM -o "$B/pl_support.o"  src/hal/pl_support.s
$VASM -o "$B/romdata.o"     src/hal/cgunsmoke_romdata.s
# embedded loader image + .mod (incbin from build/gunsmoke_loader)
$VASM -I build/gunsmoke_loader -o "$B/gunsmoke_loaderimg.o" src/hal/gunsmoke_loaderimg.s
$VASM -I build/gunsmoke_loader -o "$B/gunsmoke_loadermod.o" src/hal/gunsmoke_loadermod.s

echo "== assemble ptplayer (GNU-as syntax) + VBR helper (reused PD replayer) =="
# Frank Wille's PD ptplayer + the C-callable glue use GNU-as directives, so they go
# through m68k-amigaos-as (NOT vasm). -m68020 is fine (ptplayer is 68000-clean).
AS="m68k-amigaos-as -m68020"
$AS src/hal/tc_ptplayer.68k       -o "$B/tc_ptplayer.o"         # the PD replayer (read-only)
$AS src/hal/tc_ptplayer_glue.s    -o "$B/tc_ptplayer_glue.o"    # C-callable tc_pt_* wrappers (read-only)
$AS src/hal/gunsmoke_loader_glue.s -o "$B/gunsmoke_loader_glue.o" # gunsmoke_get_vbr (movec, supervisor)

echo "== link =="
FAST_HUNK=4
vlink -b amigahunk -Bstatic -Cexestack -mrel -sc \
    -hunkattr code=$FAST_HUNK -hunkattr .text=$FAST_HUNK -hunkattr text=$FAST_HUNK \
    -hunkattr data=$FAST_HUNK -hunkattr .data=$FAST_HUNK \
    -hunkattr bss=$FAST_HUNK -hunkattr .bss=$FAST_HUNK \
    -o build/gunsmoke_hw \
    "$B/slave.o" "$B/amiga.o" "$B/hal_sysvars.o" "$B/pl_support.o" \
    "$B/cgunsmoke_hwmain.o" "$B/cgunsmoke_input.o" "$B/cgunsmoke_firedir.o" "$B/cgunsmoke_hwrender.o" \
    "$B/hwscroll.o" "$B/cgunsmoke.o" \
    "$B/cgunsmoke_audio.o" "$B/cgunsmoke_audio_amiga.o" "$B/fm.o" \
    "$B/cgunsmoke_loader.o" "$B/gunsmoke_loaderimg.o" "$B/gunsmoke_loadermod.o" \
    "$B/tc_ptplayer.o" "$B/tc_ptplayer_glue.o" "$B/gunsmoke_loader_glue.o" \
    "$B/z80.o" "$B/romdata.o"
ls -la build/gunsmoke_hw

echo "== stage bootable directory filesystem =="
rm -rf build/gunsmokehw_boot; mkdir -p build/gunsmokehw_boot/s
cp build/gunsmoke_hw build/gunsmokehw_boot/gunsmokehw
printf 'Echo ""\nEcho "Welcome to another conversion of a classic arcade game"\nEcho "for the Commodore Amiga."\nEcho ""\nEcho "This would not be possible without the amazing work of JOTD666,"\nEcho "who inspired me to bring these classics to the Amiga."\nEcho ""\nSYS:gunsmokehw\n' > build/gunsmokehw_boot/s/startup-sequence

echo "== build bootable HDF + package prebuilt/dist =="
rm -f build/Gunsmoke.hdf
xdftool build/Gunsmoke.hdf create size=8M \
    + format GUNSMOKE ffs \
    + boot install \
    + write build/gunsmokehw_boot/gunsmokehw gunsmokehw \
    + makedir s \
    + write build/gunsmokehw_boot/s/startup-sequence s/startup-sequence
mkdir -p prebuilt/s dist
cp -f build/gunsmoke_hw prebuilt/gunsmokehw
cp -f build/gunsmokehw_boot/s/startup-sequence prebuilt/s/startup-sequence
cp -f build/Gunsmoke.hdf dist/Gunsmoke.hdf
echo "DONE -> build/gunsmoke_hw + build/gunsmokehw_boot/ + build/Gunsmoke.hdf"
echo "PACKAGED -> prebuilt/gunsmokehw + dist/Gunsmoke.hdf"
