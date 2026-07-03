#!/usr/bin/env python3
"""Assemble 1942 ROM/gfx/PROM blobs for incbin into the Amiga build -> build/r1942/."""
import os, sys, zipfile
ROMS, ZIP, OUT = "games/1942/roms", "roms/1942.zip", "build/r1942"

zip_roms = None

def rd(n):
    p = os.path.join(ROMS, n)
    if os.path.exists(p):
        with open(p, "rb") as f: return f.read()
    global zip_roms
    if zip_roms is None:
        zip_roms = zipfile.ZipFile(ZIP)
    try:
        return zip_roms.read(n)
    except KeyError:
        for zname in zip_roms.namelist():
            if zname.endswith("/" + n):
                return zip_roms.read(zname)
        raise

def main():
    os.makedirs(OUT, exist_ok=True)
    main_ = bytearray(0x20000)
    for n, off, ln in [("srb-03.m3",0,0x4000),("srb-04.m4",0x4000,0x4000),
                       ("srb-05.m5",0x10000,0x4000),("srb-06.m6",0x14000,0x2000),
                       ("srb-07.m7",0x18000,0x4000)]:
        main_[off:off+ln] = rd(n)
    g2 = bytearray()
    for n in ("sr-08.a1","sr-09.a2","sr-10.a3","sr-11.a4","sr-12.a5","sr-13.a6"): g2 += rd(n)
    g3 = bytearray()
    for n in ("sr-14.l1","sr-15.l2","sr-16.n1","sr-17.n2"): g3 += rd(n)
    blobs = {
        "main.bin": bytes(main_), "g1.bin": rd("sr-02.f2"),
        "g2.bin": bytes(g2), "g3.bin": bytes(g3),
        "pr.bin": rd("sb-5.e8"), "pg.bin": rd("sb-6.e9"), "pb.bin": rd("sb-7.e10"),
        "lchr.bin": rd("sb-0.f1"), "ltile.bin": rd("sb-4.d6"), "lspr.bin": rd("sb-8.k3"),
        "snd.bin": rd("sr-01.c11"),                 # audio Z80 program (0x4000)
    }
    for fn, d in blobs.items():
        with open(os.path.join(OUT, fn), "wb") as f: f.write(d)
        print(f"  {fn:10s} {len(d):#8x}")
    print(f"DONE -> {OUT}/")

if __name__ == "__main__": sys.exit(main())
