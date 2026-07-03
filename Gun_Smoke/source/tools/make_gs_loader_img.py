#!/usr/bin/env python3
"""tools/make_gs_loader_img.py -- build the Gun.Smoke LOADER intro image.

Gun-Smoke-namespaced sibling of tools/make_tc_loader_img.py / make_loader_img.py.
Converts a 320x256 PNG (the GUN.SMOKE title -- wooden sign, CAPCOM, western
street) into an Amiga AGA planar bitplane blob + palette, emitted as
build/gunsmoke_loader/gunsmoke_loaderimg.bin (incbin'd by
src/hal/gunsmoke_loaderimg.s, shown by src/hal/cgunsmoke_loader.c while the game
boots in the background).

It RESERVES two pens:
  * pen 0 = BLACK -- the blank canvas the runtime draws the picture onto, one
    16x8 "cube" at a time, so the slow reveal happens on black; and
  * the TOP pen (ncol-1) = a bright "PRESS FIRE TO CONTINUE" prompt that the
    runtime keeps invisible (== background) until armed, then flashes.

=== SWAP IN YOUR OWN ARTWORK ==================================================
Drop a 320x256 PNG at  assets/gunsmoke_loader.png  (any number of colours up to
~254; two pens are reserved) and re-run  bash src/tools/build_gunsmoke_hw.sh .
A different path can be passed as argv[1]. If no PNG is found a "GUN.SMOKE"
placeholder is drawn so the build always succeeds.
=============================================================================

Output blob layout (big-endian, 68k-native) -- identical to make_loader_img.py:
    u16 width            (320)
    u16 height           (256)
    u16 nplanes          (1..8)
    u16 ncolours         (1..256)   -- prompt pen is the LAST entry (ncol-1)
    ncolours * u16       palette, 12-bit 0x0RGB (4 bits/chan)
    nplanes * (width/8 * height) bytes   planar, plane-major, MSB=leftmost pixel
"""
import os, sys, struct, math

W, H = 320, 256
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUTDIR = os.path.join(ROOT, "build", "gunsmoke_loader")
OUTBIN = os.path.join(OUTDIR, "gunsmoke_loaderimg.bin")
DEFAULT_PNG = os.path.join(ROOT, "assets", "gunsmoke_loader.png")

PROMPT_TEXT = "PRESS FIRE TO CONTINUE"

from PIL import Image, ImageDraw, ImageFont


def font(sz):
    try:
        return ImageFont.load_default(size=sz)
    except TypeError:
        return ImageFont.load_default()


def text_width(d, text, fnt):
    try:
        bb = d.textbbox((0, 0), text, font=fnt)
        return bb[2] - bb[0]
    except Exception:
        return len(text) * 6


def make_placeholder():
    """Draw a simple 'GUN.SMOKE' loader screen (used until real art arrives)."""
    img = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for y in range(H):                       # sandy western sky gradient
        t = y / (H - 1)
        r = int(120 + t * 80); g = int(80 + t * 50); b = int(40 + t * 24)
        d.line([(0, y), (W, y)], fill=(r & 0xF0, g & 0xF0, b & 0xF0))
    d.rectangle([40, 70, W - 40, 150], fill=(96, 56, 24))   # wooden sign

    def centred(text, y, fnt, fill, shadow=(0, 0, 0)):
        tw = text_width(d, text, fnt)
        x = (W - tw) // 2
        d.text((x + 2, y + 2), text, font=fnt, fill=shadow)
        d.text((x, y), text, font=fnt, fill=fill)

    centred("GUN.SMOKE", 92, font(40), (248, 216, 96))
    centred("CAPCOM", 156, font(20), (224, 96, 32))
    return img


def load_source():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PNG
    if os.path.isfile(path):
        print(f"  source artwork: {path}")
        img = Image.open(path).convert("RGB")
        if img.size != (W, H):
            print(f"  resizing {img.size} -> {W}x{H}")
            img = img.resize((W, H))
        return img
    print(f"  no PNG at {path} -- drawing GUN.SMOKE placeholder")
    return make_placeholder()


def prompt_mask():
    """1-bit mask of the flashing 'PRESS FIRE TO CONTINUE' prompt, near the bottom."""
    m = Image.new("1", (W, H), 0)
    d = ImageDraw.Draw(m)
    fnt = font(20)
    tw = text_width(d, PROMPT_TEXT, fnt)
    d.text(((W - tw) // 2, H - 40), PROMPT_TEXT, font=fnt, fill=1)
    return m.load()


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    rgb = load_source()

    # Colour cap (=> bitplane count). Two pens reserved (black canvas + prompt),
    # so quantise to maxcol-2. 64 colours (6 planes) is a rich western palette.
    maxcol = int(os.environ.get("LOADER_COLORS", "64"))
    maxcol = max(4, min(256, maxcol))

    pq = rgb.quantize(colors=maxcol - 2, method=Image.MEDIANCUT)
    idx = pq.load()
    pal = pq.getpalette()  # flat [r,g,b, ...]
    used = max(pq.getextrema()) + 1
    nplanes = max(1, math.ceil(math.log2(used + 2)))   # +1 black canvas, +1 prompt
    ncol = 1 << nplanes
    prompt_pen = ncol - 1
    print(f"  image colours used={used} -> palette {ncol} ({nplanes} planes), "
          f"black canvas pen=0, prompt pen={prompt_pen}")

    pm = prompt_mask()
    cells = [[idx[x, y] + 1 for x in range(W)] for y in range(H)]
    for y in range(H):
        for x in range(W):
            if pm[x, y]:
                cells[y][x] = prompt_pen

    out = bytearray()
    out += struct.pack(">HHHH", W, H, nplanes, ncol)
    for i in range(ncol):
        if i == 0:
            r, g, b = 0x00, 0x00, 0x00            # black canvas (unrevealed area)
        elif i == prompt_pen:
            r, g, b = 0xF8, 0xF0, 0x40            # bright amber prompt (runtime flashes)
        elif 1 <= i <= used:
            q = i - 1
            r = pal[q * 3 + 0] if q * 3 + 2 < len(pal) else 0
            g = pal[q * 3 + 1] if q * 3 + 2 < len(pal) else 0
            b = pal[q * 3 + 2] if q * 3 + 2 < len(pal) else 0
        else:
            r, g, b = 0, 0, 0
        v = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)   # 12-bit 0x0RGB
        out += struct.pack(">H", v)

    bytes_per_row = W // 8
    for p in range(nplanes):
        bit = 1 << p
        plane = bytearray(bytes_per_row * H)
        for y in range(H):
            ro = y * bytes_per_row
            row = cells[y]
            for bx in range(bytes_per_row):
                acc = 0
                base = bx * 8
                for col in range(8):
                    if row[base + col] & bit:
                        acc |= 0x80 >> col
                plane[ro + bx] = acc
        out += plane

    with open(OUTBIN, "wb") as f:
        f.write(out)
    print(f"DONE -> {OUTBIN}  ({len(out)} bytes, {nplanes} planes, {ncol} colours)")


if __name__ == "__main__":
    sys.exit(main())
