#!/usr/bin/env python3
"""Stage Gun.Smoke (gunsmoke World parent set) ROM/gfx/PROM blobs.

The native Amiga build incbins build/rgunsmoke/*.bin from these staged files.
Region layout and PROM order match tools/cgunsmoke_shot.c.
"""
import os
from pathlib import Path
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROMS = os.environ.get("GUNSMOKE_ROMS", os.path.join(ROOT, "roms"))
OUT = os.path.join(ROOT, "build", "rgunsmoke")
REPO = Path(ROOT).parents[0]
HOME = Path.home()
ZIP_CANDIDATES = (
    Path(os.environ["GUNSMOKE_ZIP"]) if "GUNSMOKE_ZIP" in os.environ else None,
    REPO / "roms" / "gunsmoke.zip",
    REPO / "packages" / "zips" / "gunsmoke.zip",
    Path(ROOT) / "roms" / "gunsmoke.zip",
    Path(ROOT) / "roms" / "GunSmoke.zip",
    HOME / "Downloads" / "gunsmoke.zip",
)

class RomReader:
    def __init__(self):
        self.zf = None
        self.source = None

    def open(self):
        if all((Path(ROMS) / n).exists() for n in REQUIRED):
            self.source = Path(ROMS)
            return
        for candidate in ZIP_CANDIDATES:
            if candidate and candidate.exists():
                self.zf = zipfile.ZipFile(candidate)
                self.source = candidate
                return
        missing = ", ".join(n for n in REQUIRED if not (Path(ROMS) / n).exists())
        raise SystemExit(
            f"missing GunSmoke arcade ROM files in {ROMS}: {missing}\n"
            "Place gunsmoke.zip in roms/, packages/zips/, Gun_Smoke/roms/, "
            "or set GUNSMOKE_ZIP=/path/to/gunsmoke.zip"
        )

    def close(self):
        if self.zf:
            self.zf.close()

    def read(self, name):
        if self.zf:
            return self.zf.read(name)
        return (Path(ROMS) / name).read_bytes()

REQUIRED = (
    "gs03.09n", "gs04.10n", "gs05.12n",
    "gs13.06c", "gs12.05c", "gs11.04c", "gs10.02c",
    "gs09.06a", "gs08.05a", "gs07.04a", "gs06.02a",
    "gs22.06n", "gs21.04n", "gs20.03n", "gs19.01n",
    "gs18.06l", "gs17.04l", "gs16.03l", "gs15.01l",
    "g-01.03b", "g-02.04b", "g-03.05b", "g-04.09d", "g-06.14a",
    "g-07.15a", "g-09.09f", "g-08.08f", "g-10.02j", "g-05.01f",
    "gs01.11f", "gs14.11c", "gs02.14h",
)

def main():
    os.makedirs(OUT, exist_ok=True)
    reader = RomReader()
    reader.open()
    rd = reader.read
    # maincpu: fixed 0..7fff (gs03) + 4 banks at 0x8000 (gs04, gs05) -> 0x18000
    main_ = bytearray(0x18000)
    try:
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
        print(f"staged GunSmoke ROM blobs from {reader.source} -> {OUT}/")
    finally:
        reader.close()
if __name__ == "__main__": sys.exit(main())
