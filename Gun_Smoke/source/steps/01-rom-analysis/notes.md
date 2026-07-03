# Step 1 — ROM Analysis (Gun.Smoke, Capcom 1985)

Source of truth: `mame/capcom/gunsmoke.cpp` (driver by Paul Leaman). We use the
**`gunsmoke` World parent set** — no MCU, no encryption (unlike Commando/1943kai),
so the real ROM runs directly. All 32 ROMs verified against MAME CRCs (`rom_crc_table.txt`).

## Hardware spec (concise)
| | |
|---|---|
| PCB | 85113-A-1 main + 85113-B-3 video |
| Main CPU | Z80 @ 3 MHz (12 MHz XTAL / 4) |
| Sound CPU | Z80 @ 3 MHz (inside an 85H001 Capcom custom) |
| Sound chips | 2 × YM2203 (OPN) @ 1.5 MHz (12 MHz / 8) |
| Video | 256 × 224 visible, **ROT270** (vertical scroller), ~59.63 Hz |
| Layers | 1 scrolling bg (32×32 tiles) + sprites (16×16) + fg char/text (8×8) |
| Colour | 256-entry palette via PROMs (indirect); 3×256×4 RGB PROMs + LUT PROMs |
| Main IRQ | single `irq0_line_hold` at vblank → RST38 (0xff), once/frame |
| Sound IRQ | periodic, `384*262/4` ticks @ 6 MHz |
| DMA | sprite DMA to video, halts CPU ~131 µs (like GnG/Commando) |
| No watchdog | confirmed by jotego |

This is the **same family** as 1943/Commando (dual-Z80 + 2×YM2203, Capcom 85-era),
so the host harness reuses the pacland-amiga Z80 core + the c1943.c machine pattern.

## ROM map

### maincpu (Z80 #1) — region 0x18000
| ROM | offset | size | CRC32 | role |
|---|---|---|---|---|
| gs03.09n | 0x00000 | 0x8000 | 40a06cef | fixed code 0x0000-0x7fff |
| gs04.10n | 0x08000 | 0x8000 | 8d4b423f | banked code (banks 0,1) |
| gs05.12n | 0x10000 | 0x8000 | 2b5667fb | banked code (banks 2,3) |

### audiocpu (Z80 #2) — region 0x10000
| ROM | offset | size | CRC32 | role |
|---|---|---|---|---|
| gs02.14h | 0x00000 | 0x8000 | cd7a2c38 | sound program (2×YM2203 driver) |

### chars (gfx0) — region 0x4000 — 1024 × 8×8 2bpp text/HUD
| gs01.11f | 0x0000 | 0x4000 | b61ece9b |

### tiles (gfx1) — region 0x40000 — 512 × 32×32 4bpp bg tiles
| ROM | offset | CRC32 | planes |
|---|---|---|---|
| gs13.06c | 0x00000 | f6769fc5 | planes 2-3 |
| gs12.05c | 0x08000 | d997b78c | planes 2-3 |
| gs11.04c | 0x10000 | 125ba58e | planes 2-3 |
| gs10.02c | 0x18000 | f469c13c | planes 2-3 |
| gs09.06a | 0x20000 | 539f182d | planes 0-1 |
| gs08.05a | 0x28000 | e87e526d | planes 0-1 |
| gs07.04a | 0x30000 | 4382c0d2 | planes 0-1 |
| gs06.02a | 0x38000 | 4cafe7a6 | planes 0-1 |

### sprites (gfx2) — region 0x40000 — 2048 × 16×16 4bpp
| ROM | offset | CRC32 | planes |
|---|---|---|---|
| gs22.06n | 0x00000 | dc9c508c | planes 2-3 |
| gs21.04n | 0x08000 | 68883749 | planes 2-3 |
| gs20.03n | 0x10000 | 0be932ed | planes 2-3 |
| gs19.01n | 0x18000 | 63072f93 | planes 2-3 |
| gs18.06l | 0x20000 | f69a3c7c | planes 0-1 |
| gs17.04l | 0x28000 | 4e98562a | planes 0-1 |
| gs16.03l | 0x30000 | 0d99c3b3 | planes 0-1 |
| gs15.01l | 0x38000 | 7f14270e | planes 0-1 |

### bgtiles — region 0x8000 — the bg **tilemap layout** (NOT pixels)
| gs14.11c | 0x0000 | 0x8000 | 0af4f7eb | 2048 cols × 8 rows × 2 bytes/entry |

The bg layer is a fixed 65536×256-px map whose *layout* lives in this ROM (code +
attr per tile), while the tile *pixels* come from the `tiles` region. The map is
2048 tiles wide × 8 tall, scrolled horizontally by `scrollx` (→ vertical after ROT270).

### proms — region 0xa00 — 10 × 256×4
| ROM | offset | CRC32 | role |
|---|---|---|---|
| g-01.03b | 0x000 | 02f55589 | palette RED (4-bit) |
| g-02.04b | 0x100 | e1e36dd9 | palette GREEN |
| g-03.05b | 0x200 | 989399c0 | palette BLUE |
| g-04.09d | 0x300 | 906612b5 | char colour LUT |
| g-06.14a | 0x400 | 4a9da18b | tile colour LUT |
| g-07.15a | 0x500 | cb9394fc | tile palette-bank |
| g-09.09f | 0x600 | 3cee181e | sprite colour LUT |
| g-08.08f | 0x700 | ef91cdd2 | sprite palette-bank |
| g-10.02j | 0x800 | 0eaf5158 | video timing (unused) |
| g-05.01f | 0x900 | 25c90c2a | priority (unused) |

## Main Z80 memory map
```
0x0000-7fff  fixed ROM (gs03)
0x8000-bfff  banked ROM (4 × 0x4000 at region 0x8000; bank = (ctrl>>2)&3)
0xc000 (r)   SYSTEM   bit3 = VBLANK (active-low); coins/start
0xc001 (r)   P1       right/left/down/up/B1/B2/B3 (active-low)
0xc002 (r)   P2       (cocktail)
0xc003 (r)   DSW1     bonus/demo(or lives)/cabinet/difficulty/freeze/service
0xc004 (r)   DSW2     coinage/continue/demo-sounds
0xc4c9-c4cb (r) protection_r → fixed { 0xff, 0x00, 0x00 }
0xc800 (w)   soundlatch (→ sound Z80)
0xc804 (w)   control_w: b0-1 coin ctr, b2-3 ROM bank, b5 sndreset,
                        b6 screen flip, b7 chon (char layer enable)
0xc806 (w)   spriteram DMA trigger (buffered_spriteram8)
0xd000-d3ff  videoram  (fg char codes)        — fg tilemap
0xd400-d7ff  colorram  (fg char attr)         — fg tilemap
0xd800-d801  scrollx (16-bit, scroll axis)
0xd802       scrolly (8-bit)
0xd806 (w)   layer_w: b0-2 sprite3bank, b4 bgon, b5 objon
0xe000-efff  work RAM
0xf000-ffff  spriteram (DMA-buffered each frame)
```

## Sound Z80 memory map
```
0x0000-7fff  ROM (gs02)
0xc000-c7ff  RAM
0xc800 (r)   soundlatch
0xe000-e001 (w) YM2203 #1
0xe002-e003 (w) YM2203 #2
```

## gfxdecode (exact bit layouts — see `gunsmoke.cpp`)
- **charlayout** 8×8, 1024, 2bpp, planes {4,0}, x {8+3,8+2,8+1,8+0,3,2,1,0},
  y {7..0}×16, 16 bytes/char. *(NB: differs from 1943 — x/y offsets reversed.)*
- **tilelayout** 32×32, 512, 4bpp, planes {0x100000+4,0x100000,4,0} (bits),
  256 bytes/tile. **Byte-identical to the 1943 bg tile layout.**
- **spritelayout** 16×16, 2048, 4bpp, planes {0x100000+4,0x100000,4,0},
  64 bytes/sprite. **Byte-identical to the 1943 sprite layout.**

## Palette resolution (indirect, from `gunsmoke_state::palette`)
- 256 base colours: RGB = pal4bit(g01[i]), pal4bit(g02[i]), pal4bit(g03[i]).
- **chars** use palette 0x40-0x4f: `ind = 0x40 | (g04[color*4+pix] & 0xf)`.
  Transparent where the LUT nibble == 0x0f (i.e. ind == 0x4f) — via `configure_groups(...,0x4f)`.
- **bg tiles** use 0x00-0x3f: `ind = (g06[color*16+pix]&0xf) | ((g07[color*16+pix]&3)<<4)`.
  Opaque (drawn first; black fill if `!bgon`).
- **sprites** use 0x80-0xff: `ind = 0x80 | (g09[color*16+pix]&0xf) | ((g08[color*16+pix]&7)<<4)`.
  Pen 0 (pix==0) transparent.

## Sprite format (32 bytes/entry, spriteram 0xf000-0xffff)
```
+0 code (low 8 bits)
+1 attr: b0-3 color, b4 flipy, b5 sx high (sx -= (b5<<8)), b6-7 bank
+2 sy
+3 sx
bank==3 → bank += sprite3bank (layer_w b0-2); code += 256*bank
```

## Render order (screen_update)
bg tiles (opaque / black if !bgon) → sprites (if objon) → fg chars (if chon).
scrollx (16-bit) + scrolly (8-bit) applied to bg; fg + sprites are screen-absolute.

## Build set decision
Use **`gunsmoke`** (World). No protection beyond the fixed 3-byte table at 0xc4c9
(implemented verbatim) — that only matters near the level-3 boss, not for boot/attract.
