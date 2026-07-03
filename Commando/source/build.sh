#!/bin/bash
set -e
export PATH="${AMIGA_GCC_PATH:-$HOME/.local/bin}:$PATH"
cd "$(dirname "$0")"
BASE=$(pwd)

B=obj_interp
SRCROOT=..
AI="${AI:-../../shared_source/ArcadeIntro}"
CORE="${CORE:-../../shared_source/CapcomZ80Core}"
YMCORE="$CORE/ym"
NO_INTRO="${NO_EMBEDDED_INTRO:-0}"
EXTRA_DEFS=""
if [ "$NO_INTRO" = "1" ]; then
  EXTRA_DEFS="-DCOMMANDO_NO_EMBEDDED_INTRO"
fi
DIST="../dist"
mkdir -p "$B" "$DIST"

AS="m68k-amigaos-as -m68020"
GCC="m68k-amigaos-gcc -m68030 -noixemul -O3 -fomit-frame-pointer -funroll-loops -DNDEBUG $EXTRA_DEFS -I . -I $CORE -I $YMCORE -I $AI"
GCC_AUD="m68k-amigaos-gcc -m68030 -noixemul -O1 -fno-strict-aliasing -fomit-frame-pointer -DNDEBUG $EXTRA_DEFS -I . -I $CORE -I $YMCORE"
VASM="vasmm68k_mot -I . -I build/rcommando -m68020 -phxass -nowarn=62 -Fhunk"
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

echo "== stage Commando ROMs + bezel =="
python3 make_commando_roms.py
python3 make_commando_rtg_bezel.py >/dev/null

echo "== use interpreted main-Z80 backend =="

echo "== compile presenter + renderer =="
$GCC -c ccommando_interp.c -o "$B/ccommando_interp.o"
$GCC -c commando_rtg_render.c -o "$B/commando_rtg_render.o"
$GCC -c commando_rtg_main.c -o "$B/commando_rtg_main.o"
$GCC -c commando_libstubs.c -o "$B/commando_libstubs.o"
INTRO_OBJS=()
if [ "$NO_INTRO" = "1" ]; then
  echo "== skip embedded ArcadeIntro loader =="
else
  $GCC -c "$AI/arcade_intro.c" -o "$B/arcade_intro.o"
fi

echo "== compile sound =="
$GCC_AUD -DZ80_MAP_COMMANDO -c "$CORE/z80.c" -o "$B/z80.o"
$GCC_AUD $YMDEF -c "$YMCORE/fm.c" -o "$B/fm.o"
$GCC_AUD $YMDEF -c commando_audio.c -o "$B/commando_audio.o"
$GCC_AUD -c commando_audio_amiga.c -o "$B/commando_audio_amiga.o"

echo "== assemble entry + data + intro =="
$VASM -o "$B/slave.o" slave.s
$VASM -o "$B/amiga.o" amiga.s
$VASM -o "$B/hal_sysvars.o" hal_sysvars.s
$VASM -o "$B/pl_support.o" pl_support.s
$VASM -o "$B/romdata.o" commando_romdata.s
$VASM -I build/bezel -o "$B/rtg_bezeldata.o" commando_rtg_bezeldata.s
if [ "$NO_INTRO" = "1" ]; then
  true
else
  $AS "$AI/arcade_intro_glue.s" -o "$B/arcade_intro_glue.o"
  $AS "$AI/tc_ptplayer.68k" -o "$B/tc_ptplayer.o"
  $AS "$AI/tc_ptplayer_glue.s" -o "$B/tc_ptplayer_glue.o"
  $VASM -I "$AI" -o "$B/intro_mod.o" "$AI/intro_mod.s"
  INTRO_OBJS=("$B/arcade_intro.o" "$B/arcade_intro_glue.o" "$B/tc_ptplayer.o" "$B/tc_ptplayer_glue.o" "$B/intro_mod.o")
fi

echo "== link =="
FAST_HUNK=4
vlink -b amigahunk -Bstatic -Cexestack -mrel -sc \
    -hunkattr code=$FAST_HUNK -hunkattr .text=$FAST_HUNK -hunkattr text=$FAST_HUNK \
    -hunkattr data=$FAST_HUNK -hunkattr .data=$FAST_HUNK \
    -hunkattr bss=$FAST_HUNK -hunkattr .bss=$FAST_HUNK \
    -o "Commando" \
    "$B/slave.o" "$B/amiga.o" "$B/hal_sysvars.o" "$B/pl_support.o" \
    "$B/commando_rtg_main.o" "$B/commando_rtg_render.o" "$B/ccommando_interp.o" \
    "$B/commando_audio.o" "$B/commando_audio_amiga.o" "$B/fm.o" "$B/z80.o" \
    "$B/commando_libstubs.o" \
    "${INTRO_OBJS[@]}" \
    "$B/romdata.o" "$B/rtg_bezeldata.o"
cp -f "Commando" ..
ls -la "Commando" ../Commando

echo "== build direct-boot RTG HDF =="
HDF="$DIST/Commando_RTG.hdf"
UAE="$DIST/Commando-RTG.uae"
RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/BlackTiger_RTG.hdf"
if [ ! -f "$RTG_BASE" ] && [ -f "$DIST/../Black_Tiger/dist/BlackTiger_RTG.hdf" ]; then
  RTG_BASE="$DIST/../Black_Tiger/dist/BlackTiger_RTG.hdf"
fi
if [ ! -f "$RTG_BASE" ] && [ -f "${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/RTG1.hdf" ]; then
  RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/RTG1.hdf"
fi
if [ ! -f "$RTG_BASE" ] && [ -f "/home/jon/Downloads/RTG1 [RTG Boot Disk .hdf]/RTG1.hdf" ]; then
  RTG_BASE="/home/jon/Downloads/RTG1 [RTG Boot Disk .hdf]/RTG1.hdf"
fi
rm -f "$HDF"
if [ -f "$RTG_BASE" ]; then
  cp -f "$RTG_BASE" "$HDF"
  if xdftool "$RTG_BASE" read S/startup-sequence "$B/base-startup-sequence" >/dev/null 2>&1; then
    sed -e 's/Black Tiger RTG/Commando RTG/g' \
        -e 's/SYS:BlackTiger/SYS:Commando/g' \
        "$B/base-startup-sequence" > "$B/startup-sequence"
  else
    printf 'Echo "Commando RTG"\nStack 65536\nSYS:Commando\nC:UAEquit\nEndCLI >NIL:\n' > "$B/startup-sequence"
  fi
  normalize_rtg_startup "$B/startup-sequence"
  xdftool "$HDF" delete S/startup-sequence >/dev/null 2>&1 || true
  xdftool "$HDF" delete BlackTiger >/dev/null 2>&1 || true
  xdftool "$HDF" delete BlackTiger.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete "Black Tiger" >/dev/null 2>&1 || true
  xdftool "$HDF" delete "Black Tiger.info" >/dev/null 2>&1 || true
  xdftool "$HDF" delete Commando >/dev/null 2>&1 || true
  xdftool "$HDF" delete Commando.info >/dev/null 2>&1 || true
  xdftool "$HDF" write "Commando" Commando \
      + write "../assets/Commando.info" Commando.info \
      + write "$B/startup-sequence" S/startup-sequence
else
  printf 'Echo "Commando RTG"\nStack 65536\nSYS:Commando\nC:UAEquit\nEndCLI >NIL:\n' > "$B/startup-sequence"
  normalize_rtg_startup "$B/startup-sequence"
  xdftool "$HDF" create size=8M \
      + format COMMANDO ffs \
      + boot install \
      + write "Commando" Commando \
      + write "../assets/Commando.info" Commando.info \
      + makedir S \
      + write "$B/startup-sequence" S/startup-sequence
fi

echo "== write Amiberry RTG config =="
cat > "$UAE" <<EOF
; Commando RTG interpreted-Z80 config for Amiberry.
; Boots the RTG1/Picasso96-derived HDF directly into the game.

[config]
config_description=Commando RTG INTERPRETER
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

hardfile2=rw,DH0:${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/Commando_RTG.hdf,32,1,2,512,0,,uae0,0
uaehf0=hdf,rw,DH0:${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/Commando_RTG.hdf,32,1,2,512,0,,uae0,0

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

log_file=/tmp/amiberry-commando-rtg.log
log_console=1
fpu_model=68882
EOF
ls -la "$HDF" "$UAE"
