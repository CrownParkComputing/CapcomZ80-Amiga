#!/bin/bash
set -e
export PATH="${AMIGA_GCC_PATH:-$HOME/.local/bin}:$PATH"
cd "$(dirname "$0")"
B=obj_interp
DIST="../dist"
mkdir -p "$B" "$B/libgcc_extract" "$DIST"
AI="${AI:-}"  # optional external ArcadeIntro module; unset = skip intro
GAP=../../shared_source/Gaplus/src
AS="m68k-amigaos-as -m68020"
GCC="m68k-amigaos-gcc -m68030 -noixemul -O3 -fomit-frame-pointer -funroll-loops -DNDEBUG -I . -I $AI -I $GAP"
GCC_AUD="m68k-amigaos-gcc -m68030 -noixemul -O1 -fno-strict-aliasing -fomit-frame-pointer -DNDEBUG -I ."
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

echo "== stage Side Arms ROMs + bezel =="
python3 make_sidearms_roms.py
python3 make_sidearms_rtg_bezel.py >/dev/null

echo "== compile engine + presenter =="
$GCC -DZ80_MAP_SIDEARMS -c z80.c -o "$B/z80.o"
$GCC -c sidearms_machine.c -o "$B/sidearms_machine.o"
$GCC -c sidearms_render.c -o "$B/sidearms_render.o"
$GCC -c sidearms_aga_main.c -o "$B/sidearms_aga_main.o"
$GCC -c "$GAP/gaplus_menu.c" -o "$B/gaplus_menu.o"
$GCC -c "$AI/arcade_intro.c" -o "$B/arcade_intro.o"

echo "== compile sound =="
$GCC_AUD $YMDEF -I ym -c ym/fm.c -o "$B/fm.o"
$GCC_AUD $YMDEF -I ym -c sidearms_audio.c -o "$B/sidearms_audio.o"
$GCC_AUD -c sidearms_audio_amiga.c -o "$B/sidearms_audio_amiga.o"

LIBGCC="../../Black_Tiger/source/obj_interp/libgcc_extract"
if [ ! -f "$LIBGCC/_udivdi3.o" ]; then LIBGCC="../../Commando/source/obj_interp/libgcc_extract"; fi
if [ ! -f "$LIBGCC/_udivdi3.o" ]; then LIBGCC="../../Gun_Smoke/build/gunsmoke_interp_obj/libgcc_extract"; fi
cp "$LIBGCC/_udivdi3.o" "$B/libgcc_extract/_udivdi3.o"
cp "$LIBGCC/_umoddi3.o" "$B/libgcc_extract/_umoddi3.o"

echo "== assemble entry + ROMs + intro =="
$VASM -o "$B/slave.o" slave.s
$VASM -o "$B/amiga.o" amiga.s
$VASM -o "$B/hal_sysvars.o" hal_sysvars.s
$VASM -o "$B/pl_support.o" pl_support.s
$VASM -o "$B/romdata.o" sidearms_romdata.s
$VASM -o "$B/cgx.o" sidearms_cgx.s
$VASM -I build/bezel -o "$B/rtg_bezeldata.o" sidearms_rtg_bezeldata.s
$AS "$AI/arcade_intro_glue.s" -o "$B/arcade_intro_glue.o"
$AS "$AI/tc_ptplayer.68k" -o "$B/tc_ptplayer.o"
$AS "$AI/tc_ptplayer_glue.s" -o "$B/tc_ptplayer_glue.o"
$VASM -I "$AI" -o "$B/intro_mod.o" "$AI/intro_mod.s"

echo "== link =="
FAST_HUNK=4
vlink -b amigahunk -Bstatic -Cexestack -mrel -sc \
    -hunkattr code=$FAST_HUNK -hunkattr .text=$FAST_HUNK -hunkattr text=$FAST_HUNK \
    -hunkattr data=$FAST_HUNK -hunkattr .data=$FAST_HUNK \
    -hunkattr bss=$FAST_HUNK -hunkattr .bss=$FAST_HUNK \
    -o sidearms_aga \
    "$B/slave.o" "$B/amiga.o" "$B/hal_sysvars.o" "$B/pl_support.o" \
    "$B/sidearms_aga_main.o" "$B/sidearms_render.o" "$B/sidearms_machine.o" \
    "$B/gaplus_menu.o" "$B/sidearms_audio.o" "$B/sidearms_audio_amiga.o" "$B/fm.o" \
    "$B/arcade_intro.o" "$B/arcade_intro_glue.o" "$B/tc_ptplayer.o" "$B/tc_ptplayer_glue.o" "$B/intro_mod.o" \
    "$B/z80.o" "$B/romdata.o" "$B/cgx.o" "$B/rtg_bezeldata.o" \
    "$B/libgcc_extract/_udivdi3.o" "$B/libgcc_extract/_umoddi3.o"
ls -la sidearms_aga
cp -f sidearms_aga "../Side Arms"
ls -la "../Side Arms"

echo "== build direct-boot RTG HDF =="
HDF="$DIST/SideArms_RTG.hdf"
UAE="$DIST/SideArms-RTG.uae"
RTG_BASE="${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/BlackTiger_RTG.hdf"
if [ ! -f "$RTG_BASE" ] && [ -f "../Black_Tiger/dist/BlackTiger_RTG.hdf" ]; then
  RTG_BASE="../Black_Tiger/dist/BlackTiger_RTG.hdf"
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
    sed -e 's/Black Tiger RTG/Side Arms RTG/g' \
        -e 's/Commando RTG/Side Arms RTG/g' \
        -e 's/Gun.Smoke RTG/Side Arms RTG/g' \
        -e 's/SYS:BlackTiger/SYS:SideArms/g' \
        -e 's/SYS:Commando/SYS:SideArms/g' \
        -e 's/SYS:gunsmoke/SYS:SideArms/g' \
        "$B/base-startup-sequence" > "$B/startup-sequence"
  else
    printf 'Echo "Side Arms RTG"\nStack 65536\nSYS:SideArms\nC:UAEquit\nEndCLI >NIL:\n' > "$B/startup-sequence"
  fi
  normalize_rtg_startup "$B/startup-sequence"
  xdftool "$HDF" delete S/startup-sequence >/dev/null 2>&1 || true
  xdftool "$HDF" delete BlackTiger >/dev/null 2>&1 || true
  xdftool "$HDF" delete BlackTiger.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete Commando >/dev/null 2>&1 || true
  xdftool "$HDF" delete Commando.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete gunsmoke >/dev/null 2>&1 || true
  xdftool "$HDF" delete gunsmoke.info >/dev/null 2>&1 || true
  xdftool "$HDF" delete SideArms >/dev/null 2>&1 || true
  xdftool "$HDF" delete SideArms.info >/dev/null 2>&1 || true
  xdftool "$HDF" write "sidearms_aga" SideArms \
      + write "../Side Arms.info" SideArms.info \
      + write "$B/startup-sequence" S/startup-sequence
else
  printf 'Echo "Side Arms RTG"\nStack 65536\nSYS:SideArms\nC:UAEquit\nEndCLI >NIL:\n' > "$B/startup-sequence"
  normalize_rtg_startup "$B/startup-sequence"
  xdftool "$HDF" create size=8M \
      + format SIDEARMS ffs \
      + boot install \
      + write "sidearms_aga" SideArms \
      + write "../Side Arms.info" SideArms.info \
      + makedir S \
      + write "$B/startup-sequence" S/startup-sequence
fi

echo "== write Amiberry RTG config =="
cat > "$UAE" <<EOF
; Side Arms RTG interpreted-Z80 config for Amiberry.

[config]
config_description=Side Arms RTG INTERPRETER
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
hardfile2=rw,DH0:${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/SideArms_RTG.hdf,32,1,2,512,0,,uae0,0
uaehf0=hdf,rw,DH0:${RTG_BASE_DIR:-/home/jon/Amiberry/HardDrives}/SideArms_RTG.hdf,32,1,2,512,0,,uae0,0
gfx_display=0
gfx_display_rtg=0
gfx_width=864
gfx_height=486
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
sound=1
sound_output=exact
sound_channels=stereo
sound_frequency=44100
sound_interpol=anti
sound_volume=80
joyport0=none
joyport1=joy0
joyport1mode=cd32joy
input.config=0
input.joystick_deadzone=33
log_file=/tmp/amiberry-sidearms-rtg.log
log_console=1
fpu_model=68882
EOF
ls -la "$HDF" "$UAE"
