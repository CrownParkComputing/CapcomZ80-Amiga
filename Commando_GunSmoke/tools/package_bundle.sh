#!/bin/bash
# Package a combined Commando + GunSmoke RTG HDF that boots into WhittyDemo.
set -euo pipefail

BUNDLE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO_ROOT="$(cd "$BUNDLE_ROOT/.." && pwd)"
DIST="$BUNDLE_ROOT/dist"

export PATH="$HOME/.local/bin:$PATH"
. "$BUNDLE_ROOT/tools/hdf_safety.sh"

BASE_HDF="${BASE_HDF:-}"
if [ -z "$BASE_HDF" ]; then
    for candidate in \
        "$HOME/Amiberry/HardDrives/RTG_boot_template.hdf" \
        "$HOME/Amiberry/HardDrives/BlackTiger_RTG.hdf" \
        "$HOME/Amiberry/HardDrives/Commando_RTG.hdf" \
        "$HOME/Amiberry/HardDrives/GunSmoke_RTG.hdf" \
        "$HOME/Amiberry/HardDrives/RTG1.hdf"
    do
        if [ -f "$candidate" ]; then
            BASE_HDF="$candidate"
            break
        fi
    done
fi

TEMPLATE_UAE="${TEMPLATE_UAE:-$HOME/Amiberry/Configurations/Commando-RTG.uae}"
INSTALL_HDF="${INSTALL_HDF:-$HOME/Amiberry/HardDrives/CommandoGunSmoke_RTG.hdf}"
INSTALL_UAE="${INSTALL_UAE:-$HOME/Amiberry/Configurations/CommandoGunSmoke-RTG.uae}"
FS_RES="$(cat /sys/class/drm/*/modes 2>/dev/null | grep -m1 -E '^[0-9]+x[0-9]+' || echo '1920x1080')"
FS_W="${FS_RES%x*}"
FS_H="${FS_RES#*x}"

[ -n "$BASE_HDF" ] && [ -f "$BASE_HDF" ] || { echo "missing base HDF; set BASE_HDF=/path/to/rtg-template.hdf" >&2; exit 1; }
[ -f "$DIST/WhittySelector" ] || { echo "missing selector; run Commando_GunSmoke/tools/build_all.sh first" >&2; exit 1; }
[ -f "$DIST/Commando" ] || { echo "missing Commando; run Commando_GunSmoke/tools/build_all.sh first" >&2; exit 1; }
[ -f "$DIST/GunSmoke" ] || { echo "missing GunSmoke; run Commando_GunSmoke/tools/build_all.sh first" >&2; exit 1; }

mkdir -p "$DIST"
BUILD="$DIST/building_$$_CommandoGunSmoke_RTG.hdf"
STARTUP="$DIST/startup-sequence.$$"
cleanup() {
    rm -f "$BUILD" "$STARTUP"
}
trap cleanup EXIT

cp -f "$BASE_HDF" "$BUILD"
xdftool "$BUILD" delete WhittyDemo >/dev/null 2>&1 || true
xdftool "$BUILD" delete WhittySelector >/dev/null 2>&1 || true
xdftool "$BUILD" delete S/startup-sequence >/dev/null 2>&1 || true
xdftool "$BUILD" delete Games >/dev/null 2>&1 || true
xdftool "$BUILD" makedir Games
xdftool "$BUILD" makedir Games/Commando
xdftool "$BUILD" makedir Games/GunSmoke
xdftool "$BUILD" write "$DIST/WhittySelector" WhittyDemo
xdftool "$BUILD" write "$DIST/Commando" Games/Commando/Commando
xdftool "$BUILD" write "$DIST/GunSmoke" Games/GunSmoke/GunSmoke
if [ -f "$REPO_ROOT/Commando/assets/Commando.info" ]; then
    xdftool "$BUILD" write "$REPO_ROOT/Commando/assets/Commando.info" Games/Commando/Commando.info
fi

cat > "$STARTUP" <<'STARTUP'
; Commando / GunSmoke bundle: WhittyDemo boots first.
; Selector writes ENV:WHITTY_CG_* marker before returning.

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
Echo "Commando / GunSmoke - Whitty Arcade"
Stack 200000
Echo >ENV:WHITTY_NO_GAME_LOADER "1" NOLINE

Lab selector
Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
SYS:WhittyDemo SELECT
IF EXISTS ENV:WHITTY_CG_QUIT
  Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
  C:UAEquit
  EndCLI >NIL:
EndIF
IF EXISTS ENV:WHITTY_CG_GUNSMOKE
  Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
  CD SYS:Games/GunSmoke
  GunSmoke
  CD SYS:
  Skip selector BACK
EndIF
Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
CD SYS:Games/Commando
Commando
CD SYS:
Skip selector BACK
STARTUP
xdftool "$BUILD" write "$STARTUP" S/startup-sequence

hdf_install_verified "$BUILD" "$DIST/CommandoGunSmoke_RTG.hdf" "$INSTALL_HDF"

if [ -f "$TEMPLATE_UAE" ]; then
    sed \
      -e "1s|.*|; Commando + GunSmoke WhittyDemo selector config for Amiberry.|" \
      -e "2s|.*|; Boots WhittyDemo as a two-game selector.|" \
      -e "s|^config_description=.*|config_description=Commando + GunSmoke Whitty selector|" \
      -e "s|^hardfile2=.*|hardfile2=rw,DH0:$INSTALL_HDF,32,1,2,512,0,,uae0,0|" \
      -e "s|^uaehf0=.*|uaehf0=hdf,rw,DH0:$INSTALL_HDF,32,1,2,512,0,,uae0,0|" \
      -e "s|^gfx_width=.*|gfx_width=1280|" \
      -e "s|^gfx_height=.*|gfx_height=720|" \
      -e "s|^gfx_width_windowed=.*|gfx_width_windowed=1280|" \
      -e "s|^gfx_height_windowed=.*|gfx_height_windowed=720|" \
      -e "s|^gfx_width_fullscreen=.*|gfx_width_fullscreen=$FS_W|" \
      -e "s|^gfx_height_fullscreen=.*|gfx_height_fullscreen=$FS_H|" \
      -e "s|^gfx_fullscreen=.*|gfx_fullscreen=1|" \
      -e "s|^gfx_fullscreen_amiga=.*|gfx_fullscreen_amiga=false|" \
      -e "s|^gfx_fullscreen_picasso=.*|gfx_fullscreen_picasso=true|" \
      -e "s|^log_file=.*|log_file=/tmp/amiberry-commandogunsmoke-rtg.log|" \
      "$TEMPLATE_UAE" > "$DIST/CommandoGunSmoke-RTG.uae"
else
    cat > "$DIST/CommandoGunSmoke-RTG.uae" <<EOF
; Commando + GunSmoke WhittyDemo config for Amiberry.
; Boots WhittyDemo as a two-game selector.

[config]
config_description=Commando + GunSmoke Whitty selector
config_version=8.2.0
config_hardware=1
config_host=1
use_gui=no
sound=1
sound_output=exact
sound_auto=false
amiga_model=A1200
chipset=aga
cpu_type=68020
cpu_model=68030
cpu_speed=max
cpu_throttle=0.0
cpu_compatible=false
cpu_24bit_addressing=false
cpu_data_cache=false
cpu_cycle_exact=false
cpu_memory_cycle_exact=false
blitter_cycle_exact=false
cycle_exact=false
comp_trustbyte=direct
comp_trustword=direct
comp_trustlong=direct
comp_trustnaddr=direct
comp_nf=true
comp_constjump=true
comp_flushmode=hard
comp_catchfault=true
cachesize=16384
chipmem_size=4
fastmem_size=8
z3mem_size=512
gfxcard_size=16
gfxcard_type=ZorroIII
kickstart_rom_file=${KICKSTART_ROM:-$HOME/Amiberry/ROMs/kick40068.A1200.rom}
nr_floppies=0
floppy0type=-1
hardfile2=rw,DH0:$INSTALL_HDF,32,1,2,512,0,,uae0,0
uaehf0=hdf,rw,DH0:$INSTALL_HDF,32,1,2,512,0,,uae0,0
gfx_width=1280
gfx_height=720
gfx_x_windowed=64
gfx_y_windowed=48
gfx_width_windowed=1280
gfx_height_windowed=720
gfx_width_fullscreen=$FS_W
gfx_height_fullscreen=$FS_H
gfx_fullscreen=1
gfx_fullscreen_amiga=false
gfx_fullscreen_picasso=true
gfx_framerate=1
gfx_backbuffers=2
gfx_backbuffers_rtg=1
gfx_vsync=false
gfx_vsyncmode=normal
gfx_vsync_picasso=false
gfx_vsyncmode_picasso=normal
gfx_linemode=none
gfx_center_horizontal=smart
gfx_center_vertical=smart
gfx_keep_aspect=true
gfx_api=sdl3
gfx_api_options=hardware
gfx_colour_mode=32bit
immediate_blits=true
display_optimizations=full
gfxcard_hardware_vblank=false
gfxcard_hardware_sprite=false
gfxcard_multithread=false
gfxcard_zerocopy=true
rtg_nocustom=false
rtg_modes=0x3dfe
rtg_noautomodes=false
log_file=/tmp/amiberry-commandogunsmoke-rtg.log
EOF
fi

CFG="$DIST/CommandoGunSmoke-RTG.uae"
ensure_cfg() {
    local key="$1"
    local value="$2"
    grep -q "^${key}=" "$CFG" || printf '%s=%s\n' "$key" "$value" >> "$CFG"
}

ensure_cfg sound 1
ensure_cfg sound_output exact
ensure_cfg sound_auto false
ensure_cfg cpu_type 68020
ensure_cfg cpu_model 68030
ensure_cfg cpu_speed max
ensure_cfg cpu_throttle 0.0
ensure_cfg cpu_compatible false
ensure_cfg cpu_24bit_addressing false
ensure_cfg cpu_data_cache false
ensure_cfg cpu_cycle_exact false
ensure_cfg cpu_memory_cycle_exact false
ensure_cfg blitter_cycle_exact false
ensure_cfg cycle_exact false
ensure_cfg comp_trustbyte direct
ensure_cfg comp_trustword direct
ensure_cfg comp_trustlong direct
ensure_cfg comp_trustnaddr direct
ensure_cfg comp_nf true
ensure_cfg comp_constjump true
ensure_cfg comp_flushmode hard
ensure_cfg comp_catchfault true
ensure_cfg cachesize 16384
ensure_cfg gfx_framerate 1
ensure_cfg gfx_backbuffers 2
ensure_cfg gfx_backbuffers_rtg 1
ensure_cfg gfx_vsync false
ensure_cfg gfx_vsyncmode normal
ensure_cfg gfx_vsync_picasso false
ensure_cfg gfx_vsyncmode_picasso normal
ensure_cfg gfx_api sdl3
ensure_cfg gfx_api_options hardware
ensure_cfg gfx_colour_mode 32bit
ensure_cfg immediate_blits true
ensure_cfg display_optimizations full
ensure_cfg gfxcard_hardware_vblank false
ensure_cfg gfxcard_hardware_sprite false
ensure_cfg gfxcard_multithread false
ensure_cfg gfxcard_zerocopy true
ensure_cfg rtg_nocustom false
ensure_cfg rtg_modes 0x3dfe
ensure_cfg rtg_noautomodes false
mkdir -p "$(dirname "$INSTALL_UAE")"
cp -f "$DIST/CommandoGunSmoke-RTG.uae" "$INSTALL_UAE"

echo "installed: $INSTALL_HDF"
echo "installed: $INSTALL_UAE"
