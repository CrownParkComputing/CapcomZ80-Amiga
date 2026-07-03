#!/usr/bin/env python3
"""Build 1942 native AGA attached-sprite tile images.

Output image layout per tile:
  16 lines * 4 planes * 1 word, plane-major per line.
  Pen 15 is transparent -> hardware pen 0; pixel pens 0..14 -> hw pens 1..15.
"""
import os
import sys

IN_G3 = "build/r1942/g3.bin"
OUT_BIN = "build/c1942_amiga/c1942_asprites.bin"
OUT_ASM = "build/c1942_amiga/c1942_asprites.s"
NTILES = 512


def bit(data, off):
    return (data[off >> 3] >> (7 - (off & 7))) & 1


def spr_pix(g3, code, x, y):
    xo = [0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267]
    yo = [0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240]
    h = 0x40000
    o = code * 512 + yo[y] + xo[x]
    return (bit(g3, o + h + 4) << 3) | (bit(g3, o + h) << 2) | (bit(g3, o + 4) << 1) | bit(g3, o)


def main():
    os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
    g3 = open(IN_G3, "rb").read()
    with open(OUT_BIN, "wb") as f:
        for code in range(NTILES):
            for line in range(16):
                ax = 15 - line
                words = [0, 0, 0, 0]
                for ay in range(16):
                    px = spr_pix(g3, code, ax, ay)
                    pen = 0 if px == 15 else px + 1
                    m = 0x8000 >> ay
                    for p in range(4):
                        if pen & (1 << p):
                            words[p] |= m
                for w in words:
                    f.write(w.to_bytes(2, "big"))

    with open(OUT_ASM, "w") as f:
        f.write("\tXDEF\t_c1942_asprite_tiles\n")
        f.write("\tSECTION\tdata,DATA\n")
        f.write("\tCNOP\t0,4\n")
        f.write("_c1942_asprite_tiles:\n")
        f.write("\tincbin\t\"c1942_asprites.bin\"\n")
        f.write("\tCNOP\t0,4\n")
        f.write("\tEND\n")
    print(f"1942 AGA sprite tiles: {NTILES} tiles, {NTILES * 16 * 4 * 2} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
