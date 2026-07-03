#!/usr/bin/env python3
"""make_gunsmoke.py -- stage Gun.Smoke (gunsmoke World parent set) ROM/gfx/PROM blobs
for the native Amiga build -> build/rgunsmoke/ (incbin'd by src/hal/cgunsmoke_romdata.s).
Region layout + PROM order MATCH tools/cgunsmoke_shot.c exactly (the MAME-verified decode)."""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROMS = os.environ.get("GUNSMOKE_ROMS", os.path.join(ROOT, "roms"))
OUT = os.path.join(ROOT, "build", "rgunsmoke")
def rd(n):
    with open(os.path.join(ROMS, n), "rb") as f: return f.read()
def main():
    os.makedirs(OUT, exist_ok=True)
    # maincpu: fixed 0..7fff (gs03) + 4 banks at 0x8000 (gs04, gs05) -> 0x18000
    main_ = bytearray(0x18000)
    main_[0x00000:0x08000] = rd("gs03.09n")
    main_[0x08000:0x10000] = rd("gs04.10n")
    main_[0x10000:0x18000] = rd("gs05.12n")
    # bg tiles 0x40000: gs13,12,11,10 (planes2-3) then gs09,08,07,06 (planes0-1)
    tiles = b"".join(rd(n) for n in ("gs13.06c","gs12.05c","gs11.04c","gs10.02c",
                                     "gs09.06a","gs08.05a","gs07.04a","gs06.02a"))
    # sprites 0x40000: gs22,21,20,19 (planes2-3) then gs18,17,16,15 (planes0-1)
    sprites = b"".join(rd(n) for n in ("gs22.06n","gs21.04n","gs20.03n","gs19.01n",
                                       "gs18.06l","gs17.04l","gs16.03l","gs15.01l"))
    # PROMs (0xa00) -- shot layout: 0 red,1 grn,2 blu,3 charLUT,4 tileLUT,5 tilePalBank,
    #                  6 sprLUT,7 sprPalBank,8 g-10,9 g-05
    proms = bytearray(0xa00)
    for n, off in (("g-01.03b",0x000),("g-02.04b",0x100),("g-03.05b",0x200),
                   ("g-04.09d",0x300),("g-06.14a",0x400),("g-07.15a",0x500),
                   ("g-09.09f",0x600),("g-08.08f",0x700),("g-10.02j",0x800),
                   ("g-05.01f",0x900)):
        proms[off:off+0x100] = rd(n)
    blobs = {"main.bin":bytes(main_), "chars.bin":rd("gs01.11f"), "tiles.bin":tiles,
             "sprites.bin":sprites, "bgmap.bin":rd("gs14.11c"), "proms.bin":bytes(proms),
             "snd.bin":rd("gs02.14h")}
    for fn, d in blobs.items():
        with open(os.path.join(OUT, fn), "wb") as f: f.write(d)
        print(f"  {fn:12s} {len(d):#8x}")
    print(f"DONE -> {OUT}/")
if __name__ == "__main__": sys.exit(main())
