#!/usr/bin/env python3
"""Build the Commando + GunSmoke two-square selector composite.

The selector renderer follows the Chase H.Q. launcher layout: a 2S x S baked
image, with the left S x S square for Commando and the right S x S square for
GunSmoke. The visible gap and frames are added by the renderer.
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
S = 275


def square(path, bias):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    side = min(w, h)
    left = int(round((w - side) * bias))
    top = (h - side) // 2
    return im.crop((left, top, left + side, top + side)).resize((S, S), Image.LANCZOS)


left = square(os.path.join(ROOT, "Commando", "assets", "commando_loader.png"), bias=0.5)
right = square(os.path.join(ROOT, "Gun_Smoke", "assets", "gunsmoke_loader_alt1.png"), bias=0.5)

comp = Image.new("RGB", (2 * S, S))
comp.paste(left, (0, 0))
comp.paste(right, (S, 0))

out = os.path.join(HERE, "assets", "pic_src.png")
comp.save(out)
print("wrote %s (%dx%d)" % (out, comp.size[0], comp.size[1]))
