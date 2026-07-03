#!/bin/bash
# Install a loose AGS/Pimiga SHARED: version of the Commando/GunSmoke selector bundle.
set -euo pipefail

BUNDLE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO_ROOT="$(cd "$BUNDLE_ROOT/.." && pwd)"
DIST="$BUNDLE_ROOT/dist"
SHARED_ROOT="${SHARED_ROOT:-$HOME/AGS_UAE/SHARED}"
WA="$SHARED_ROOT/WhittyArcade"
GAME_DIR="$WA/Games/CommandoGunSmoke"
CFG="$WA/S/ArcadeLauncher.cfg"
LAUNCH="$WA/S/Launch-CommandoGunSmoke"
ROOT_LAUNCH="$SHARED_ROOT/Commando + GunSmoke"

[ -d "$SHARED_ROOT" ] || { echo "missing SHARED root: $SHARED_ROOT" >&2; exit 1; }
[ -f "$DIST/WhittySelector" ] || { echo "missing selector; run Commando_GunSmoke/tools/build_all.sh first" >&2; exit 1; }
[ -f "$DIST/Commando" ] || { echo "missing Commando; run Commando_GunSmoke/tools/build_all.sh first" >&2; exit 1; }
[ -f "$DIST/GunSmoke" ] || { echo "missing GunSmoke; run Commando_GunSmoke/tools/build_all.sh first" >&2; exit 1; }

mkdir -p "$GAME_DIR/Commando" "$GAME_DIR/GunSmoke" "$WA/S" "$WA/Artwork" "$WA/Saves/CommandoGunSmoke"
rm -f "$WA/S/Launch-Commando" "$WA/S/Launch-GunSmoke" "$SHARED_ROOT/Commando" "$SHARED_ROOT/GunSmoke" "$SHARED_ROOT/Gun.Smoke"

cp -f "$DIST/WhittySelector" "$GAME_DIR/WhittyDemo"
cp -f "$DIST/WhittySelector" "$GAME_DIR/WhittySelector"
cp -f "$DIST/Commando" "$GAME_DIR/Commando/Commando"
cp -f "$DIST/GunSmoke" "$GAME_DIR/GunSmoke/GunSmoke"
chmod 0755 "$GAME_DIR/WhittyDemo" "$GAME_DIR/WhittySelector" "$GAME_DIR/Commando/Commando" "$GAME_DIR/GunSmoke/GunSmoke"

if [ -f "$REPO_ROOT/Commando/assets/Commando.info" ]; then
    cp -f "$REPO_ROOT/Commando/assets/Commando.info" "$GAME_DIR/Commando/Commando.info"
fi
cp -f "$BUNDLE_ROOT/selector/assets/pic_src.png" "$WA/Artwork/CommandoGunSmoke.png"

cat > "$LAUNCH" <<'EOF'
FailAt 21
Echo "Commando / GunSmoke - Whitty Arcade"
Stack 200000
Echo >ENV:WHITTY_NO_GAME_LOADER "1" NOLINE

Lab selector
CD "PROGDIR:WhittyArcade/Games/CommandoGunSmoke"
Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
WhittyDemo SELECT
IF EXISTS ENV:WHITTY_CG_QUIT
  Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
  CD PROGDIR:
  SetEnv WHITTY_NO_GAME_LOADER 0
  Quit 0
EndIF
IF EXISTS ENV:WHITTY_CG_GUNSMOKE
  Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
  CD "PROGDIR:WhittyArcade/Games/CommandoGunSmoke/GunSmoke"
  GunSmoke
  CD PROGDIR:
  Skip selector BACK
EndIF
Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
CD "PROGDIR:WhittyArcade/Games/CommandoGunSmoke/Commando"
Commando
CD PROGDIR:
Skip selector BACK
EOF
chmod 0644 "$LAUNCH"

cat > "$ROOT_LAUNCH" <<'EOF'
FailAt 21
Echo "Commando / GunSmoke - Whitty Arcade"
Stack 200000
Echo >ENV:WHITTY_NO_GAME_LOADER "1" NOLINE

Lab selector
CD "SHARED:WhittyArcade/Games/CommandoGunSmoke"
Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
WhittyDemo SELECT
IF EXISTS ENV:WHITTY_CG_QUIT
  Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
  CD SHARED:
  SetEnv WHITTY_NO_GAME_LOADER 0
  Quit 0
EndIF
IF EXISTS ENV:WHITTY_CG_GUNSMOKE
  Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
  CD "SHARED:WhittyArcade/Games/CommandoGunSmoke/GunSmoke"
  GunSmoke
  CD SHARED:
  Skip selector BACK
EndIF
Delete >NIL: ENV:WHITTY_CG_COMMANDO ENV:WHITTY_CG_GUNSMOKE ENV:WHITTY_CG_QUIT QUIET
CD "SHARED:WhittyArcade/Games/CommandoGunSmoke/Commando"
Commando
CD SHARED:
Skip selector BACK
EOF
chmod 0644 "$ROOT_LAUNCH"

if [ -f "$CFG" ]; then
    tmp="$CFG.tmp"
    grep -v -e '^Commando / GunSmoke|' -e '^Commando|' -e '^GunSmoke|' -e '^Gun.Smoke|' "$CFG" > "$tmp" || true
    mv "$tmp" "$CFG"
fi
cat >> "$CFG" <<'EOF'
Commando / GunSmoke|Execute "PROGDIR:WhittyArcade/S/Launch-CommandoGunSmoke"|Capcom Z80 / RTG|Installed|Whitty selector for Commando and GunSmoke|PROGDIR:WhittyArcade/Artwork/CommandoGunSmoke.png|PROGDIR:WhittyArcade/Saves/CommandoGunSmoke/none.state|1985|Capcom
EOF

cat > "$GAME_DIR/README.txt" <<'EOF'
Commando / GunSmoke selector bundle for AGS/Pimiga SHARED:

Launch through Whitty Arcade, or from Shell:

  Execute SHARED:WhittyArcade/S/Launch-CommandoGunSmoke

The selector writes the selected game into ENV: before the script launches it.
Both game executables are built without their embedded per-game loaders.
EOF

echo "installed Shared bundle:"
echo "  $GAME_DIR"
echo "  $LAUNCH"
echo "  $ROOT_LAUNCH"
echo "  $CFG"
