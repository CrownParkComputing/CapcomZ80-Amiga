#!/usr/bin/env python3
from pathlib import Path
import argparse
import struct
from PIL import Image, ImageEnhance

ICON_W = 96
COLORS = 64


def strip_coloricon(data: bytes) -> bytes:
    pos = data.find(b"FORM")
    return data[:pos] if pos >= 0 else data


def quantized_frame(img: Image.Image):
    rgba = img.convert("RGBA")
    w, h = rgba.size
    nh = max(1, round(h * ICON_W / w))
    rgba = rgba.resize((ICON_W, nh), Image.Resampling.LANCZOS)
    pal = rgba.convert("RGB").quantize(
        colors=COLORS,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    )
    used = max(pal.getdata()) + 1
    return pal.size, bytes(pal.getdata()), bytes(pal.getpalette()[:used * 3]), used


def imag_chunk(indices: bytes, palette: bytes, colors: int, depth: int) -> bytes:
    body = struct.pack(
        ">4sIBBBBBBHH",
        b"IMAG", 0, 0, colors - 1, 2, 0, 0, depth,
        len(indices) - 1,
        len(palette) - 1,
    )
    body += indices + palette
    chunk_len = len(body) - 8
    body = body[:4] + struct.pack(">I", chunk_len) + body[8:]
    if chunk_len & 1:
        body += b"\0"
    return body


def coloricon_form(img: Image.Image) -> bytes:
    (w, h), data1, pal1, colors1 = quantized_frame(img)
    (_, _), data2, pal2, colors2 = quantized_frame(ImageEnhance.Brightness(img).enhance(1.18))
    colors = max(colors1, colors2)
    depth = 1
    while (1 << depth) < colors:
        depth += 1
    face = struct.pack(
        ">4sI4s4sIBBBBH",
        b"FORM", 0, b"ICON", b"FACE", 6, w - 1, h - 1, 1, 0x11, colors,
    )
    payload = face + imag_chunk(data1, pal1, colors1, depth) + imag_chunk(data2, pal2, colors2, depth)
    return payload[:4] + struct.pack(">I", len(payload) - 8) + payload[8:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("template", type=Path)
    ap.add_argument("png", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--asset-out", type=Path)
    args = ap.parse_args()

    icon = strip_coloricon(args.template.read_bytes()) + coloricon_form(Image.open(args.png))
    args.out.write_bytes(icon)
    if args.asset_out:
        args.asset_out.write_bytes(icon)
    print(f"wrote {args.out} from {args.png.name} ({len(icon)} bytes)")


if __name__ == "__main__":
    main()
