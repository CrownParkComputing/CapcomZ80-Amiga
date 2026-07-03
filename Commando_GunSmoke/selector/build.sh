#!/bin/bash
# Build the Whitty selector intro/menu for Amiga RTG.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PT="$HERE/ptplayer"
OBJ="$HERE/build"

export PATH="${AMIGA_GCC_PATH:-$HOME/amiga-amigaos/bin}:$HOME/.local/bin:$PATH"

GCC="m68k-amigaos-gcc -m68030 -noixemul -O3 -fomit-frame-pointer -funroll-loops -DNDEBUG -I $HERE ${EXTRA_CFLAGS:-}"
AS="m68k-amigaos-as -m68020"
VASM="vasmm68k_mot -I $HERE -m68020 -phxass -nowarn=62 -Fhunk"

mkdir -p "$OBJ"

DEFAULT_PIC_SRC="$HERE/assets/pic_src.png"
PIC_SRC="${PIC_SRC:-$DEFAULT_PIC_SRC}"
export PIC_SRC
if [ "$PIC_SRC" = "$DEFAULT_PIC_SRC" ]; then
    if [ ! -f "$PIC_SRC" ] || [ "$HERE/make_1943_selector_pic.py" -nt "$PIC_SRC" ] \
       || [ "$HERE/../../Commando/assets/commando_loader.png" -nt "$PIC_SRC" ] \
       || [ "$HERE/../../Gun_Smoke/assets/gunsmoke_loader_alt1.png" -nt "$PIC_SRC" ] \
       || [ -n "${FORCE_ASSETS:-}" ]; then
        python3 "$HERE/make_1943_selector_pic.py"
    fi
fi
if [ ! -f "$HERE/demo_pic.c" ] || [ "$PIC_SRC" -nt "$HERE/demo_pic.c" ] || [ -n "${FORCE_ASSETS:-}" ]; then
    python3 "$HERE/gen_assets.py"
fi

echo "=== selector host harness ==="
cc -O2 -Wall -Wextra ${EXTRA_CFLAGS:-} -I "$HERE" -o "$OBJ/host_selector" \
    "$HERE/host_demo.c" "$HERE/demo_core.c" "$HERE/demo_pic.c"

if [ "${1:-}" = "host" ]; then
    mkdir -p "$OBJ/frames"
    "$OBJ/host_selector" "$OBJ/frames" "${@:2}"
    exit 0
fi

echo "=== selector Amiga executable ==="
$GCC -c "$HERE/demo_core.c" -o "$OBJ/demo_core.o"
$GCC -c "$HERE/demo_pic.c"  -o "$OBJ/demo_pic.o"
$GCC -c "$HERE/demo_main.c" -o "$OBJ/demo_main.o"
$AS "$PT/arcade_intro_glue.s" -o "$OBJ/arcade_intro_glue.o"
$AS "$PT/tc_ptplayer.68k"     -o "$OBJ/tc_ptplayer.o"
$AS "$PT/tc_ptplayer_glue.s"  -o "$OBJ/tc_ptplayer_glue.o"
$VASM -o "$OBJ/intro_mod.o" "$HERE/intro_mod.s"

$GCC -o "$HERE/WhittySelector" \
    "$OBJ/demo_main.o" "$OBJ/demo_core.o" "$OBJ/demo_pic.o" \
    "$OBJ/arcade_intro_glue.o" "$OBJ/tc_ptplayer.o" "$OBJ/tc_ptplayer_glue.o" \
    "$OBJ/intro_mod.o" \
    -Wl,--start-group -lamiga -lgcc -Wl,--end-group

ls -la "$HERE/WhittySelector"
