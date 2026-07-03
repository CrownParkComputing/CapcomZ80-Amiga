# Step 3 — Video Decode (faithful title/attract render)

Goal: decode char/tile/sprite gfx + the PROM indirect palette **exactly** per
`gunsmoke.cpp`'s gfxdecode, render the real attract/title to PNG = faithful video.

## What was built
- `tools/gunsmoke_shot.c` — host renderer modelled on `pacland-amiga/tools/c1943_shot.c`.
  Runs the booted main Z80 N frames, then composites bg → sprites → fg chars into a
  256×224 arcade-native buffer (visible Y = absolute 16..239). Caller rotates 270°
  (ROT270) for upright viewing.

Build / run:
```
gcc -O2 -Isrc -Icores tools/gunsmoke_shot.c src/gunsmoke.c cores/z80.c -o build/gunsmoke_shot
./build/gunsmoke_shot 600 build/gs_600.ppm
magick build/gs_600.ppm -rotate 270 build/gs_600.png
```

## Decode recipe (verified faithful)
- **chars** 8×8 2bpp, planes {4,0}, x-offsets `{11,10,9,8,3,2,1,0}`, y-offsets
  `{112,96,80,64,48,32,16,0}`, 16 B/char. `o = code*128 + yo[y] + xo[x]`,
  `pix = (bit(o+4)<<1)|bit(o)`. **Char x/y offsets differ from 1943** — this was the
  only decode change needed; tiles + sprites are byte-identical to the 1943 layouts.
- **bg tiles** 32×32 4bpp, `txo` (1943-identical), `H = 0x100000` bits (0x20000 B →
  planes-0-1 half), `o = code*2048 + y*16 + txo[x]`,
  `pix = (bit(o+H+4)<<3)|(bit(o+H)<<2)|(bit(o+4)<<1)|bit(o)`.
- **sprites** 16×16 4bpp, `sxo = {0,1,2,3,8,9,10,11,256..259,264..267}`, `H = 0x100000`,
  `o = code*512 + y*16 + sxo[x]` (1943-identical).
- **bg tilemap layout** from `bgmap` (gs14): `ti = col*8 + row` (SCAN_COLS, 8 rows),
  `col=(worldx>>5)&2047`, `row=(worldy>>5)&7`; `attr=bgmap[ti*2+1]`,
  `code=bgmap[ti*2]+((attr&1)<<8)`, `color=(attr&0x3c)>>2`, flipx=b6, flipy=b7.
  `worldx=(x+scrollx)&0xffff`, `worldy=(absy+scrolly)&0xff`.
- **fg char map**: `idx=(absy>>3)*32+(x>>3)` (SCAN_ROWS), `attr=colorram[idx]`,
  `code=videoram[idx]+((attr&0xe0)<<2)`, `color=attr&0x1f`.

## Palette (indirect, pal4bit = (v<<4)|v)
```
RGB[ind]      = ( pal4bit(g01[ind]), pal4bit(g02[ind]), pal4bit(g03[ind]) )
char ind      = 0x40 | (g04[color*4 + pix] & 0xf)         ; transparent if ==0x4f
bg tile ind   = (g06[color*16+pix]&0xf) | ((g07[color*16+pix]&3)<<4)   ; opaque
sprite ind    = 0x80 | (g09[color*16+pix]&0xf) | ((g08[color*16+pix]&7)<<4) ; pix0 transp
```
Render order (= screen_update): bg (opaque / black if `!bgon`) → sprites (`objon`,
pix0 transparent) → fg chars (`chon`, ind-0x4f transparent).

## Sprite addressing
`spriteram 0xf000-0xffff`, 32 B/entry, drawn back-to-front (high offs first):
`bank=(attr&0xc0)>>6; if(bank==3) bank+=sprite3bank; code=spr[+0]+256*bank;`
`color=attr&0xf; flipy=attr&0x10; sx=spr[+3]-((attr&0x20)<<3); sy=spr[+2]`.

## Results — FAITHFUL (artifacts in this folder)
| frame | PNG | content |
|---|---|---|
| 120 | `01-warning-screen.png` | "WARNING / IF YOU ARE PLAYING THIS VIDEO GAME IN JAPAN…" green-on-black — chars + palette correct |
| 600 | `02-title-screen.png` | **GUN.SMOKE logo (red/yellow), RANKING TABLE, © CAPCOM 1985**, sky/cliff + wooden-building bg tiles |
| 900 | `03-attract-gameplay.png` | attract demo: player cowboy, enemies, buildings, dirt ground (sprites + scroll) |
| 1500 | `04-attract-gameplay2.png` | attract demo: player, enemies, bullets (orange), well/barrel |

All four layers (bg tiles, sprites, fg text) decode with correct geometry and
correct colours straight from the PROMs — no fudging. The title screen
(`02-title-screen.png`) is the **faithful-video milestone**.

## Carry-forward for step 4+ (classify → render_router → audio → Amiga)
- Tiles + sprites share the 1943 decode → the Amiga build-time gfx-decoder from
  the 1943 port is directly reusable (only the char offsets change).
- bg is a single horizontal-scrolling playfield (scrollx) → maps cleanly to one
  hwscroll playfield; fg text/HUD → bitplane (per the routing rule); player +
  enemies → hw sprite / bobs.
- Indirect palette is ≤256 entries — fits AGA directly.
