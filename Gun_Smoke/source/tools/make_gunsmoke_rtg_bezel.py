#!/usr/bin/env python3
"""Build the RTG-sized Gun.Smoke bezel backdrop from The Bezel Project overlay."""
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / "assets" / "gunsmoke_bezelproject.png"
OUT_BIN = ROOT / "build" / "bezel" / "gunsmoke_bezel_864x486.bin"

RTG_W, RTG_H = 864, 486


def rgb332(r, g, b):
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)


def main():
    im = Image.open(SRC).convert("RGBA")
    sw, sh = im.size
    scale = max(RTG_W / sw, RTG_H / sh)
    nw, nh = round(sw * scale), round(sh * scale)
    im = im.resize((nw, nh), Image.Resampling.LANCZOS)
    x0 = (nw - RTG_W) // 2
    y0 = (nh - RTG_H) // 2
    im = im.crop((x0, y0, x0 + RTG_W, y0 + RTG_H)).convert("RGB")

    px = im.load()
    out = bytearray(RTG_W * RTG_H)
    for y in range(RTG_H):
        for x in range(RTG_W):
            out[y * RTG_W + x] = rgb332(*px[x, y])

    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    OUT_BIN.write_bytes(out)
    print(f"  bezel {SRC.name} -> {OUT_BIN.name} ({len(out)} bytes RGB332)")


if __name__ == "__main__":
    main()
