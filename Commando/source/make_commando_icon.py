#!/usr/bin/env python3
from pathlib import Path
import struct
from PIL import Image, ImageEnhance

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
ROOT = HERE.parents[2]
TEMPLATE = ROOT / "Capcom_Z80_Dual_YM2203" / "Side_Arms" / "assets" / "Side Arms.info"
PNG = Path("/home/jon/Downloads/commandp.png")
OUT_TOP = GAME / "Commando.info"
OUT_ASSET = GAME / "assets" / "Commando.info"

ICON_W = 96
COLORS = 64


def strip_coloricon(data: bytes) -> bytes:
    pos = data.find(b"FORM")
    return data[:pos] if pos >= 0 else data


def quantized_frame(img: Image.Image):
    rgba = img.convert("RGBA")
    w, h = rgba.size
    scale = ICON_W / w
    nh = max(1, round(h * scale))
    rgba = rgba.resize((ICON_W, nh), Image.Resampling.LANCZOS)
    # P mode carries a compact palette; no dithering keeps the icon sharp.
    pal = rgba.convert("RGB").quantize(colors=COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = pal.getpalette()[:COLORS * 3]
    used = max(pal.getdata()) + 1
    palette = palette[:used * 3]
    return pal.size, bytes(pal.getdata()), bytes(palette), used


def imag_chunk(indices: bytes, palette: bytes, colors: int, depth: int) -> bytes:
    # OS3.5 ColorIcon IMAG chunk. Uncompressed chunky image + uncompressed palette.
    flags = 2               # palette supplied, no transparent pen
    img_len = len(indices)
    pal_len = len(palette)
    body = struct.pack(">4sIBBBBBBHH", b"IMAG", 0, 0, colors - 1, flags, 0, 0, depth, img_len - 1, pal_len - 1)
    body += indices + palette
    chunk_len = len(body) - 8
    body = body[:4] + struct.pack(">I", chunk_len) + body[8:]
    if chunk_len & 1:
        body += b"\0"
    return body


def coloricon_form(img: Image.Image) -> bytes:
    (w, h), data1, pal1, colors = quantized_frame(img)
    bright = ImageEnhance.Brightness(img).enhance(1.18)
    (_, _), data2, pal2, colors2 = quantized_frame(bright)
    colors = max(colors, colors2)
    depth = 1
    while (1 << depth) < colors:
        depth += 1
    face = struct.pack(">4sI4s4sIBBBBH", b"FORM", 0, b"ICON", b"FACE", 6, w - 1, h - 1, 1, 0x11, colors)
    payload = face + imag_chunk(data1, pal1, max(data1) + 1, depth) + imag_chunk(data2, pal2, max(data2) + 1, depth)
    return payload[:4] + struct.pack(">I", len(payload) - 8) + payload[8:]


def main():
    base = strip_coloricon(TEMPLATE.read_bytes())
    icon = base + coloricon_form(Image.open(PNG))
    OUT_TOP.write_bytes(icon)
    OUT_ASSET.write_bytes(icon)
    print(f"wrote {OUT_TOP} from {PNG.name} ({len(icon)} bytes)")


if __name__ == "__main__":
    main()
