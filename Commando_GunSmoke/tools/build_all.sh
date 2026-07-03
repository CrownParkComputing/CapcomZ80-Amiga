#!/bin/bash
# Build stripped Commando + GunSmoke executables and the combined selector.
set -euo pipefail

BUNDLE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO_ROOT="$(cd "$BUNDLE_ROOT/.." && pwd)"
DIST="$BUNDLE_ROOT/dist"

mkdir -p "$DIST"

export AMIGA_GCC_PATH="${AMIGA_GCC_PATH:-$HOME/amiga-amigaos/bin}"

echo "== build stripped Commando =="
NO_EMBEDDED_INTRO=1 bash "$REPO_ROOT/Commando/source/build.sh"

echo "== build stripped GunSmoke =="
NO_EMBEDDED_INTRO=1 bash "$REPO_ROOT/Gun_Smoke/source/build.sh"

echo "== build selector =="
FORCE_ASSETS=1 bash "$BUNDLE_ROOT/selector/build.sh"

cp -f "$REPO_ROOT/Commando/Commando" "$DIST/Commando"
cp -f "$REPO_ROOT/Gun_Smoke/Gun.Smoke" "$DIST/GunSmoke"
cp -f "$BUNDLE_ROOT/selector/WhittySelector" "$DIST/WhittySelector"

ls -la "$DIST/Commando" "$DIST/GunSmoke" "$DIST/WhittySelector"
