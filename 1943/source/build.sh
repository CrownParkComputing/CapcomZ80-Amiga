#!/bin/bash
# 1943 RTG native-transcode build (m68k-amigaos).
# Rebuilds the main-Z80 Rust transcode and the prefixed sound-Z80 transcode,
# then packages the direct-boot RTG HDF used by Amiberry/AGS.
set -e
export PATH="${AMIGA_GCC_PATH:-$HOME/.local/bin}:$PATH"

cd "$(dirname "$0")"

SRC="$(pwd)"
ROOT="$(cd "$SRC/.." && pwd)"
B="$SRC/obj_native"
MAIN_GC="$SRC/build/1943_native/gencrate"
AUD_GC="$SRC/build/1943_audio_native/gencrate"
# ArcadeIntro is an optional external intro-animation module. Override the
# AI env var to point at a local checkout, or leave it unset to skip the intro.
AI="${AI:-}"
if [ -n "$AI" ] && [ ! -d "$AI" ]; then
  echo "warning: AI=$AI not found; intro will be skipped" >&2
  AI=""
fi
EXE="$SRC/1943_native_96"
DIST="$ROOT/dist"
HDF="$DIST/1943_RTG.hdf"
UAE="$DIST/1943-RTG.uae"

GCC="m68k-amigaos-gcc -m68030 -noixemul -O3 -fomit-frame-pointer -funroll-loops -DNDEBUG -I . -I $AI"
GCC_AUD="m68k-amigaos-gcc -m68030 -noixemul -O1 -fno-strict-aliasing -fomit-frame-pointer -DNDEBUG -I . -I $AI"
AS="m68k-amigaos-as -m68020"
VASM="vasmm68k_mot -I . -m68020 -phxass -nowarn=62 -Fhunk"
YMDEF="-DHAS_YM2203=1 -DHAS_YM2608=0 -DHAS_YM2610=0 -DHAS_YM2610B=0 -DHAS_YM2612=0 -DHAS_YM3438=0"

normalize_rtg_startup() {
  local in="$1"
  local out="$1.tmp"
  awk '
    /  IF EXISTS DEVS:Monitors\/more\/uaegfx/ {
      print "  IF EXISTS DEVS:Monitors/uaegfx"
      print "    DEVS:Monitors/uaegfx"
      print "  ELSE"
      print "    IF EXISTS DEVS:Monitors/more/uaegfx"
      print "      DEVS:Monitors/more/uaegfx"
      print "    EndIF"
      print "  EndIF"
      skip=2
      next
    }
    skip > 0 { skip--; next }
    /C:List >NIL: DEVS:Monitors\/~\(#\?\.info\|VGAOnly\) TO T:M LFORMAT "DEVS:Monitors\/%s"/ {
      print "  C:List >NIL: DEVS:Monitors/~(#?.info|VGAOnly|uaegfx) TO T:M LFORMAT \"DEVS:Monitors/%s\""
      next
    }
    { print }
  ' "$in" > "$out"
  mv "$out" "$in"
}

mkdir -p "$B" "$B/rsobj_main" "$B/rsobj_audio" "$DIST" "$ROOT/prebuilt"

echo "== embed Bezel Project backdrop =="
python3 make_c1943_rtg_bezel.py >/dev/null

echo "== build Rust main-Z80 transcode =="
( cd "$MAIN_GC" && cargo +nightly build -Z build-std=core --target m68k-unknown-none-elf --release -q )
MAIN_A="$MAIN_GC/target/m68k-unknown-none-elf/release/libc1943_z80.a"
BIN=$(ls -d ~/.rustup/toolchains/nightly-*/lib/rustlib/*/bin | head -1)
rm -rf "$B/rsobj_main"
mkdir -p "$B/rsobj_main"
( cd "$B/rsobj_main" && "$BIN/llvm-ar" x "$MAIN_A" )
for o in "$B"/rsobj_main/*.o; do
  "$BIN/llvm-objcopy" --remove-section .comment --remove-section .note.GNU-stack \
      --remove-section .llvmbc --remove-section .llvmcmd "$o" "$o" 2>/dev/null || true
done

echo "== build Rust sound-Z80 transcode =="
( cd "$AUD_GC" && cargo +nightly build -Z build-std=core --target m68k-unknown-none-elf --release -q )
AUD_A="$AUD_GC/target/m68k-unknown-none-elf/release/libc1943_audio_z80.a"
rm -rf "$B/rsobj_audio"
mkdir -p "$B/rsobj_audio"
( cd "$B/rsobj_audio" && "$BIN/llvm-ar" x "$AUD_A" )
for o in "$B"/rsobj_audio/*.o; do
  "$BIN/llvm-objcopy" --remove-section .comment --remove-section .note.GNU-stack \
      --remove-section .llvmbc --remove-section .llvmcmd --prefix-symbols=_aud_ "$o" "$o" 2>/dev/null || true
done

echo "== compile native bridge, renderer, RTG presenter, and audio =="
$GCC     -c c1943_native_rust.c            -o "$B/c1943_native_rust.o"
$GCC     -c c1943_render.c                 -o "$B/c1943_render.o"
$GCC     -c c1943_glue.c                   -o "$B/c1943_glue.o"
$GCC     -c c1943_rtg_main.c               -o "$B/c1943_rtg_main.o"
$GCC     -c c1943_menu.c                   -o "$B/c1943_menu.o"
$GCC_AUD $YMDEF -I ym -c ym/fm.c           -o "$B/fm.o"
$GCC_AUD $YMDEF -I ym -DC1943_AUDIO_NATIVE -c c1943_audio.c -o "$B/c1943_audio.o"
$GCC_AUD -c c1943_audio_amiga.c            -o "$B/c1943_audio_amiga.o"

echo "== compile + assemble ArcadeIntro loader =="
$GCC -c "$AI/arcade_intro.c"    -o "$B/arcade_intro.o"
$AS   "$AI/arcade_intro_glue.s" -o "$B/arcade_intro_glue.o"
$AS   "$AI/tc_ptplayer.68k"     -o "$B/tc_ptplayer.o"
$AS   "$AI/tc_ptplayer_glue.s"  -o "$B/tc_ptplayer_glue.o"
$VASM -I "$AI" -o "$B/intro_mod.o" "$AI/intro_mod.s"

echo "== assemble glue + embedded ROMs + bezel =="
$VASM -o "$B/slave.o"       slave.s
$VASM -o "$B/amiga.o"       amiga.s
$VASM -o "$B/hal_sysvars.o" hal_sysvars.s
$VASM -o "$B/pl_support.o"  pl_support.s
$VASM -o "$B/romdata.o"     c1943_romdata.s
$VASM -o "$B/rtg_bezeldata.o" c1943_rtg_bezeldata.s
$VASM -o "$B/c1943_native_aliases.o" c1943_native_aliases.s

echo "== link native executable =="
FAST_HUNK=4
vlink -b amigahunk -Bstatic -Cexestack -mrel -sc \
    -hunkattr code=$FAST_HUNK -hunkattr .text=$FAST_HUNK -hunkattr text=$FAST_HUNK \
    -hunkattr data=$FAST_HUNK -hunkattr .data=$FAST_HUNK \
    -hunkattr bss=$FAST_HUNK -hunkattr .bss=$FAST_HUNK \
    -o "$EXE" \
    "$B/slave.o" "$B/amiga.o" "$B/hal_sysvars.o" "$B/pl_support.o" \
    "$B/c1943_rtg_main.o" "$B/c1943_glue.o" "$B/c1943_render.o" "$B/c1943_native_rust.o" \
    "$B/c1943_menu.o" "$B/c1943_audio.o" "$B/c1943_audio_amiga.o" "$B/fm.o" \
    "$B/arcade_intro.o" "$B/arcade_intro_glue.o" "$B/tc_ptplayer.o" "$B/tc_ptplayer_glue.o" "$B/intro_mod.o" \
    "$B/romdata.o" "$B/rtg_bezeldata.o" "$B/c1943_native_aliases.o" \
    "$B"/rsobj_main/*.o "$B"/rsobj_audio/*.o

cp -f "$EXE" "$ROOT/1943"
cp -f "$EXE" "$ROOT/prebuilt/1943"
ls -la "$EXE" "$ROOT/1943"

echo "== build direct-boot RTG HDF =="
rm -f "$HDF"
RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/BlackTiger_RTG.hdf"
if [ ! -f "$RTG_BASE" ] && [ -f "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/Commando_RTG.hdf" ]; then
  RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/Commando_RTG.hdf"
fi
if [ ! -f "$RTG_BASE" ] && [ -f "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/1943_RTG.hdf" ]; then
  RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/1943_RTG.hdf"
fi
if [ ! -f "$RTG_BASE" ] && [ -f "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/RTG1.hdf" ]; then
  RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/RTG1.hdf"
fi

if [ -f "$RTG_BASE" ]; then
  cp -f "$RTG_BASE" "$HDF"
  cat > "$B/startup-sequence" <<'STARTUP'
; 1943 RTG direct boot, based on the supplied RTG1/Picasso96 boot disk.

C:SetPatch QUIET
C:Version >NIL:
FailAt 21

C:MakeDir RAM:T RAM:Clipboards RAM:ENV RAM:ENV/Sys
C:Copy >NIL: ENVARC: RAM:ENV ALL NOREQ

Resident >NIL: C:Assign PURE
Resident >NIL: C:Execute PURE

Assign >NIL: ENV: RAM:ENV
Assign >NIL: T: RAM:T
Assign >NIL: CLIPS: RAM:Clipboards
Assign >NIL: REXX: S:
Assign >NIL: PRINTERS: DEVS:Printers
Assign >NIL: KEYMAPS: DEVS:Keymaps
Assign >NIL: LOCALE: SYS:Locale
Assign >NIL: LIBS: SYS:Classes ADD

BindDrivers
C:Mount >NIL: DEVS:DOSDrivers/~(#?.info)

IF EXISTS DEVS:Monitors
  IF EXISTS DEVS:Monitors/VGAOnly
    DEVS:Monitors/VGAOnly
  EndIF
  IF EXISTS DEVS:Monitors/uaegfx
    DEVS:Monitors/uaegfx
  ELSE
    IF EXISTS DEVS:Monitors/more/uaegfx
      DEVS:Monitors/more/uaegfx
    EndIF
  EndIF
  C:List >NIL: DEVS:Monitors/~(#?.info|VGAOnly|uaegfx) TO T:M LFORMAT "DEVS:Monitors/%s"
  Execute T:M
  C:Delete >NIL: T:M
EndIF

C:AddDataTypes REFRESH QUIET
C:IPrefs
C:ConClip
C:Wait 2 SECS

Path >NIL: RAM: C: SYS:Utilities SYS:System S: SYS:Prefs SYS:WBStartup SYS:Tools
Echo "1943 RTG"
Stack 65536
SYS:1943
C:UAEquit
EndCLI >NIL:
STARTUP
  xdftool "$HDF" delete S/startup-sequence >/dev/null 2>&1 || true
  xdftool "$HDF" delete BlackTiger >/dev/null 2>&1 || true
  xdftool "$HDF" delete BlackTiger.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete Commando >/dev/null 2>&1 || true
  xdftool "$HDF" delete Commando.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete gunsmoke >/dev/null 2>&1 || true
  xdftool "$HDF" delete gunsmoke.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete 1943 >/dev/null 2>&1 || true
  xdftool "$HDF" delete 1943.info >/dev/null 2>&1 || true
  xdftool "$HDF" write "$ROOT/1943" 1943 \
      + write "$ROOT/1943.info" 1943.info \
      + write "$B/startup-sequence" S/startup-sequence
else
  echo "missing RTG base HDF; cannot build package" >&2
  exit 1
fi

echo "== write Amiberry config =="
cat > "$UAE" <<EOF
; 1943 RTG native-transcode config.
; Boots the RTG1/Picasso96-derived HDF directly into the game.

[config]
config_description=1943 RTG TRANSCODE
config_version=8.2.0
config_hardware=1
config_host=1
use_gui=no

amiga_model=A1200
chipset=aga
chipset_compatible=A1200
cpu_type=68030
cpu_model=68030
cpu_compatible=false
cpu_cycle_exact=false
cpu_speed=max
address_space_24=false
cpu_24bit_addressing=false
cachesize=16384
comp_trustbyte=direct
comp_trustword=direct
comp_trustlong=direct
comp_trustnaddr=direct
comp_flushmode=hard
comp_constjump=true

chipmem_size=4
fastmem_size=8
z3mem_size=512
z3mem_start=0x40000000
bogomem_size=0

gfxcard_size=16
gfxcard_type=ZorroIII
gfxcard_hardware_vblank=false
gfxcard_hardware_sprite=false
gfxcard_multithread=false
gfxcard_zerocopy=true
rtg_nocustom=false
rtg_modes=0x3ffe
rtg_noautomodes=false

kickstart_rom_file=${KICKSTART_ROM:-${KICKSTART_DIR:-/home/jon/Amiberry/ROMs}/}kick40068.A1200.rom

nr_floppies=0
floppy0type=-1

hardfile2=rw,DH0:${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/1943_RTG.hdf,32,1,2,512,0,,uae0,0
uaehf0=hdf,rw,DH0:${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/1943_RTG.hdf,32,1,2,512,0,,uae0,0

gfx_display=0
gfx_display_rtg=0
gfx_width=864
gfx_height=486
gfx_x_windowed=64
gfx_y_windowed=48
gfx_width_windowed=864
gfx_height_windowed=486
gfx_width_fullscreen=864
gfx_height_fullscreen=486
gfx_fullscreen=0
gfx_fullscreen_amiga=false
gfx_fullscreen_picasso=false
gfx_linemode=none
gfx_center_horizontal=smart
gfx_center_vertical=smart
gfx_keep_aspect=true
gfx_colour_mode=32bit
gfx_api=sdl3
gfx_api_options=hardware
gfx_backbuffers=2
gfx_backbuffers_rtg=1
gfx_vsync=false
gfx_vsync_picasso=false
immediate_blits=true
screenshot_dir=${SCREENSHOT_DIR:-/home/jon/Amiberry/Screenshots/}

sound=1
sound_output=exact
sound_channels=stereo
sound_frequency=44100
sound_interpol=anti
sound_volume=80

joyport0=none
joyport0autofire=none
joyport1=joy0
joyport1mode=cd32joy
joyport1autofire=none
joyportfriendlyname1=Microsoft Xbox 360 Controller
joyportname1=JOY0
input.config=0
input.joymouse_speed_analog=100
input.joymouse_speed_digital=10
input.joymouse_deadzone=33
input.joystick_deadzone=33
input.analog_joystick_multiplier=18
input.analog_joystick_offset=-5
input.mouse_speed=100
input.autofire_speed=600
input.autoswitch=false

log_file=/tmp/amiberry-1943-rtg.log
log_console=1
fpu_model=68882
EOF

echo "DONE -> $EXE + $HDF + $UAE"
