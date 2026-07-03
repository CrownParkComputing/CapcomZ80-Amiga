#!/bin/bash
# Build Gun.Smoke interpreter executable and direct-boot RTG HDF.
set -e
export PATH="${AMIGA_GCC_PATH:-$HOME/.local/bin}:$PATH"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/source"
CORE="${CORE:-$ROOT/../shared_source/CapcomZ80Core}"
YMCORE="$CORE/ym"
VIDEO="${VIDEO:-$ROOT/../shared_source/CapcomZ80Video}"
B="$ROOT/build/gunsmoke_interp_obj"
EXE="$ROOT/build/gunsmoke_interp"
HDF="$ROOT/dist/GunSmoke_RTG.hdf"
UAE="$ROOT/dist/GunSmoke-RTG.uae"
mkdir -p "$B" "$B/libgcc_extract" "$ROOT/build" "$ROOT/dist" "$ROOT/prebuilt"

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

echo "== stage ROM/gfx/PROM blobs =="
python3 "$SRC/tools/make_gunsmoke.py"
python3 "$SRC/tools/make_gunsmoke_rtg_bezel.py"

echo "== compile C =="
AI="${AI:-$ROOT/../shared_source/ArcadeIntro}"
NO_INTRO="${NO_EMBEDDED_INTRO:-0}"
EXTRA_DEFS=""
if [ "$NO_INTRO" = "1" ]; then
  EXTRA_DEFS="-DGUNSMOKE_NO_EMBEDDED_INTRO"
fi
YMDEF="-DHAS_YM2203=1 -DHAS_YM2608=0 -DHAS_YM2610=0 -DHAS_YM2610B=0 -DHAS_YM2612=0 -DHAS_YM3438=0"
GCC="m68k-amigaos-gcc -m68030 -noixemul -O3 -fomit-frame-pointer -funroll-loops -DNDEBUG $EXTRA_DEFS -DZ80_MAP_GUNSMOKE -I $CORE -I $YMCORE -I $VIDEO -I $SRC/hal -I $AI"
GCC_AUD="m68k-amigaos-gcc -m68030 -noixemul -O1 -fno-strict-aliasing -fomit-frame-pointer -DNDEBUG $EXTRA_DEFS -DZ80_MAP_GUNSMOKE -I $CORE -I $YMCORE -I $SRC/hal -I $AI"

LIBGCC="$ROOT/../Black_Tiger/source/obj_interp/libgcc_extract"
if [ ! -f "$LIBGCC/_udivdi3.o" ]; then LIBGCC="$ROOT/../Commando/source/obj_interp/libgcc_extract"; fi
if [ ! -f "$LIBGCC/_udivdi3.o" ]; then LIBGCC="$ROOT/../1943/source/obj/libgcc_extract"; fi
if [ ! -f "$LIBGCC/_udivdi3.o" ]; then LIBGCC="$HOME/amiga-gcc-build/build-Linux-m68k-amigaos/gcc/m68k-amigaos/libm020/libgcc"; fi
cp "$LIBGCC/_udivdi3.o" "$B/libgcc_extract/_udivdi3.o"
cp "$LIBGCC/_umoddi3.o" "$B/libgcc_extract/_umoddi3.o"

$GCC -c "$CORE/z80.c"                               -o "$B/z80.o"
$GCC -c "$VIDEO/capcom_z80_video.c"                 -o "$B/capcom_z80_video.o"
$GCC -c "$SRC/hal/cgunsmoke.c"                      -o "$B/cgunsmoke.o"
$GCC -c "$SRC/hal/cgunsmoke_rtg.c"                  -o "$B/cgunsmoke_rtg.o"
$GCC -c "$SRC/hal/cgunsmoke_rtg_presenter.c"        -o "$B/cgunsmoke_rtg_presenter.o"
INTRO_OBJS=()
if [ "$NO_INTRO" = "1" ]; then
  echo "== skip embedded ArcadeIntro loader =="
else
  $GCC -c "$AI/arcade_intro.c"                      -o "$B/arcade_intro.o"
fi
$GCC_AUD $YMDEF -c "$YMCORE/fm.c" -o "$B/fm.o"
$GCC_AUD $YMDEF -c "$SRC/hal/cgunsmoke_audio.c" -o "$B/cgunsmoke_audio.o"
$GCC_AUD -c "$SRC/hal/cgunsmoke_audio_amiga.c"      -o "$B/cgunsmoke_audio_amiga.o"

echo "== assemble glue + embedded data =="
VASM="vasmm68k_mot -I $SRC -I $SRC/amiga -I $SRC/hal -I $ROOT/build/rgunsmoke -I $ROOT/build/bezel -m68000 -phxass -nowarn=62 -Fhunk"
$VASM -o "$B/slave.o"       "$SRC/slave.s"
$VASM -o "$B/amiga.o"       "$SRC/amiga/amiga.s"
$VASM -o "$B/hal_sysvars.o" "$SRC/hal/hal_sysvars.s"
$VASM -o "$B/pl_support.o"  "$SRC/hal/pl_support.s"
$VASM -o "$B/romdata.o"     "$SRC/hal/cgunsmoke_romdata.s"
$VASM -o "$B/rtg_bezeldata.o" "$SRC/hal/cgunsmoke_rtg_bezeldata.s"

echo "== assemble ArcadeIntro ptplayer + glue =="
AS="m68k-amigaos-as -m68020"
if [ "$NO_INTRO" = "1" ]; then
  true
else
  $AS "$AI/arcade_intro_glue.s" -o "$B/arcade_intro_glue.o"
  $AS "$AI/tc_ptplayer.68k"     -o "$B/tc_ptplayer.o"
  $AS "$AI/tc_ptplayer_glue.s"  -o "$B/tc_ptplayer_glue.o"
  $VASM -I "$AI" -o "$B/intro_mod.o" "$AI/intro_mod.s"
  INTRO_OBJS=("$B/arcade_intro.o" "$B/arcade_intro_glue.o" "$B/tc_ptplayer.o" "$B/tc_ptplayer_glue.o" "$B/intro_mod.o")
fi

echo "== link =="
FAST_HUNK=4
vlink -b amigahunk -Bstatic -Cexestack -mrel -sc \
    -hunkattr code=$FAST_HUNK -hunkattr .text=$FAST_HUNK -hunkattr text=$FAST_HUNK \
    -hunkattr data=$FAST_HUNK -hunkattr .data=$FAST_HUNK \
    -hunkattr bss=$FAST_HUNK -hunkattr .bss=$FAST_HUNK \
    -o "$EXE" \
    "$B/slave.o" "$B/amiga.o" "$B/hal_sysvars.o" "$B/pl_support.o" \
    "$B/cgunsmoke_rtg_presenter.o" "$B/cgunsmoke_rtg.o" "$B/capcom_z80_video.o" "$B/cgunsmoke.o" "$B/z80.o" \
    "$B/cgunsmoke_audio.o" "$B/cgunsmoke_audio_amiga.o" "$B/fm.o" \
    "${INTRO_OBJS[@]}" \
    "$B/romdata.o" "$B/rtg_bezeldata.o" \
    "$B/libgcc_extract/_udivdi3.o" "$B/libgcc_extract/_umoddi3.o"
ls -la "$EXE"

echo "== package executable =="
cp -f "$EXE" "$ROOT/Gun.Smoke"
cp -f "$EXE" "$ROOT/prebuilt/gunsmoke"

echo "== build direct-boot RTG HDF =="
rm -f "$HDF"
RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/BlackTiger_RTG.hdf"
if [ ! -f "$RTG_BASE" ] && [ -f "$ROOT/../Black_Tiger/dist/BlackTiger_RTG.hdf" ]; then
  RTG_BASE="$ROOT/../Black_Tiger/dist/BlackTiger_RTG.hdf"
fi
if [ ! -f "$RTG_BASE" ] && [ -f "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/Commando_RTG.hdf" ]; then
  RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/Commando_RTG.hdf"
fi
if [ ! -f "$RTG_BASE" ] && [ -f "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/RTG1.hdf" ]; then
  RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/RTG1.hdf"
fi
if [ ! -f "$RTG_BASE" ] && [ -f "/home/jon/Downloads/RTG1 [RTG Boot Disk .hdf]/RTG1.hdf" ]; then
  RTG_BASE="/home/jon/Downloads/RTG1 [RTG Boot Disk .hdf]/RTG1.hdf"
fi
if [ -f "$RTG_BASE" ]; then
  cp -f "$RTG_BASE" "$HDF"
  if xdftool "$RTG_BASE" read S/startup-sequence "$B/base-startup-sequence" >/dev/null 2>&1; then
    sed -e 's/Black Tiger RTG/Gun.Smoke RTG/g' \
        -e 's/Commando RTG/Gun.Smoke RTG/g' \
        -e 's/SYS:BlackTiger/SYS:gunsmoke/g' \
        -e 's/SYS:Commando/SYS:gunsmoke/g' \
        "$B/base-startup-sequence" > "$B/startup-sequence"
  else
    printf 'Echo "Gun.Smoke RTG"\nStack 65536\nSYS:gunsmoke\nC:UAEquit\nEndCLI >NIL:\n' > "$B/startup-sequence"
  fi
  normalize_rtg_startup "$B/startup-sequence"
  xdftool "$HDF" delete S/startup-sequence >/dev/null 2>&1 || true
  xdftool "$HDF" delete BlackTiger >/dev/null 2>&1 || true
  xdftool "$HDF" delete BlackTiger.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete "Black Tiger" >/dev/null 2>&1 || true
  xdftool "$HDF" delete "Black Tiger.info" >/dev/null 2>&1 || true
  xdftool "$HDF" delete Commando >/dev/null 2>&1 || true
  xdftool "$HDF" delete Commando.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete gunsmoke >/dev/null 2>&1 || true
  xdftool "$HDF" delete gunsmoke.info >/dev/null 2>&1 || true
  xdftool "$HDF" write "$EXE" gunsmoke \
      + write "$B/startup-sequence" S/startup-sequence
else
  echo "WARNING: RTG base HDF not found; creating minimal fallback" >&2
  UAEQUIT="$B/UAEquit"
  rm -f "$UAEQUIT"
  for base in "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/1943_RTG.hdf" "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/Commando_RTG.hdf" "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/RTG1.hdf"; do
    if [ -f "$base" ] && xdftool "$base" read C/UAEquit "$UAEQUIT" >/dev/null 2>&1; then
      break
    fi
  done
  if [ ! -f "$UAEQUIT" ]; then
    echo "missing UAEquit; cannot build a clean standalone HDF" >&2
    exit 1
  fi
  printf 'Echo "Gun.Smoke RTG"\nStack 65536\nSYS:gunsmoke\nC:UAEquit\nEndCLI >NIL:\n' > "$B/startup-sequence"
  normalize_rtg_startup "$B/startup-sequence"
  xdftool "$HDF" create size=8M \
      + format GUNSMOKE ffs \
      + boot install \
      + write "$EXE" gunsmoke \
      + makedir S \
      + write "$B/startup-sequence" S/startup-sequence \
      + makedir C \
      + write "$UAEQUIT" C/UAEquit
fi

echo "== write Amiberry config =="
cat > "$UAE" <<EOF
; Gun.Smoke RTG/interpreter config.
; Boots the RTG1/Picasso96-derived HDF directly into the game.

[config]
config_description=Gun.Smoke RTG INTERPRETER
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

hardfile2=rw,DH0:${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/GunSmoke_RTG.hdf,32,1,2,512,0,,uae0,0
uaehf0=hdf,rw,DH0:${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/GunSmoke_RTG.hdf,32,1,2,512,0,,uae0,0

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
joyport1=joy0
joyport1mode=cd32joy
joyport1autofire=none
joyportfriendlyname1=Microsoft Xbox 360 Controller
joyportname1=JOY0
input.config=0
input.autoswitch=false
input.joymouse_speed_analog=100
input.joymouse_speed_digital=10
input.joymouse_deadzone=33
input.joystick_deadzone=33

log_file=/tmp/amiberry-gunsmoke-rtg.log
log_console=1
fpu_model=68882
EOF

echo "DONE -> $EXE + $HDF + $UAE"
