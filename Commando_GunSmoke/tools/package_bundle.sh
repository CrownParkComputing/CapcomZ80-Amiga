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
      -e "s|^gfx_width_fullscreen=.*|gfx_width_fullscreen=1280|" \
      -e "s|^gfx_height_fullscreen=.*|gfx_height_fullscreen=720|" \
      -e "s|^gfx_fullscreen=.*|gfx_fullscreen=1|" \
      -e "s|^gfx_fullscreen_amiga=.*|gfx_fullscreen_amiga=fullwindow|" \
      -e "s|^gfx_fullscreen_picasso=.*|gfx_fullscreen_picasso=fullwindow|" \
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
amiga_model=A1200
chipset=aga
cpu_type=68030
cpu_model=68030
cpu_speed=max
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
gfx_width_fullscreen=1280
gfx_height_fullscreen=720
gfx_fullscreen=1
gfx_fullscreen_amiga=fullwindow
gfx_fullscreen_picasso=fullwindow
gfx_linemode=none
gfx_center_horizontal=smart
gfx_center_vertical=smart
gfx_keep_aspect=true
log_file=/tmp/amiberry-commandogunsmoke-rtg.log
EOF
fi
mkdir -p "$(dirname "$INSTALL_UAE")"
cp -f "$DIST/CommandoGunSmoke-RTG.uae" "$INSTALL_UAE"

echo "installed: $INSTALL_HDF"
echo "installed: $INSTALL_UAE"
