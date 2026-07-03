# Gun.Smoke (Capcom 1985) — three fixes

Native A1200 AGA port. Build: `tools/build_gunsmoke_hw.sh` → `build/gunsmokehw_boot/`.
Run: `amiberry -f run/gunsmoke-a1200.uae -s use_gui=no`.

Files touched:
- `src/hal/cgunsmoke_input.c`  (Fix 1 — 3-button fire)
- `src/hal/cgunsmoke_hwrender.c` (Fix 2 — credits card, Fix 3 — centring)

---

## Fix 1 — 3-button joystick fire

Gun.Smoke's P1 port (`0xc001`) wants three fire buttons for the 3-direction shooting:
`BUTTON1=0x10  BUTTON2=0x20  BUTTON3=0x40` (active-low). MAME `capcom/gunsmoke.cpp`
confirms IN1 is simply `JOYSTICK_RIGHT/LEFT/DOWN/UP + BUTTON1 + BUTTON2 + BUTTON3`
(no PORT_NAME labels — the left/straight/right aim is a cabinet convention, not encoded
in the driver, so the physical→aim assignment is a stick-layout tuning detail).

### The POTGO / POTINP bit math (the load-bearing detail)

All four sibling Capcom ports (1942/1943/Commando/Gun.Smoke) read the joystick on the
Amiga **gameport = port 1** (`JOY1DAT 0xdff00c` for directions, `CIAA-PRA bit7` for the
red button). The 2nd/3rd fire buttons sit on that port's two **pot pins**, read via
`POTINP 0xdff016`. Per the Amiga Hardware Reference Manual, the port-1 pot bits are:

| pin | signal | POTGO/POTINP bit | mask  |
|-----|--------|------------------|-------|
| 5   | DATRY  | 14               | 0x4000|
| 9   | DATRX  | 12               | 0x1000|

(out-enables: OUTRY=bit15, OUTRX=bit13.) **Bits 10/11 are the PORT-0 / mouse pot pins**
(DATLY + OUTLY) — reading buttons there misses the gameport entirely, so the fix uses
the HRM-correct port-1 bits 12 & 14, *not* 10/11.

`POTGO` is written once to **drive those pins HIGH as outputs** so a pressed button
(pin→GND) pulls the pin LOW and reads back 0:

```
POTGO = 0xFF01;   /* OUTRY|DATRY|OUTRX|DATRX (port1) + port0 pot-outs + START */
```

Read + map:
```
fire1 = !(CIAA_PRA & 0x80);   /* red,  pin 6           -> BUTTON1 0x10 */
fire2 = !(POTINP & 0x1000);   /* blue, pin 9 (DATRX)   -> BUTTON2 0x20 */
fire3 = !(POTINP & 0x4000);   /* yel,  pin 5 (DATRY)   -> BUTTON3 0x40 */
```

**What changed vs. before:** the prior code read `fire2` from bit14 and `fire3` from
bit12 — i.e. the two pot pins were swapped relative to the documented wiring (button2 on
pin 9, button3 on pin 5). POTGO was also the bare `0xFF00` with no START. Now POTGO is
set explicitly (named bits + START) and the two reads match the pin assignment, so all
three buttons reach `c001` as `0x10 / 0x20 / 0x40`.

Keyboard fallback retained: Space / L-Ctrl → BUTTON1, L-Alt → BUTTON2, X → BUTTON3,
plus '5' = coin, '1' = start, cursor keys = stick.

> Verification note: headless Amiberry cannot inject discrete pot-pin (pin 5 / pin 9)
> presses, so the three buttons are confirmed by the bit math above + the matching
> already-shipped 1943 fire2/fire3 path. Final confirmation is on a real 3-button stick
> / CD32 pad by the user.

---

## Fix 2 — credits card (centred, cube-drawn)

The Capcom WARNING phase (boot, `objon==0`) is replaced by a typewriter/cube-revealed
credits card, drawn one glyph CELL at a time using the game's own 8x8 font as blitter
char-bobs. New text (3 lines), each line horizontally **centred** within the 28-column
playfield (`x0 = (28-len)*4`), block vertically centred:

```
        CONVERSION BY
      WHITTY ARCADE 2026
      CREDIT TO JOTD666
```

`CARD_LINES` dropped 7→3. The Capcom font has no '.' glyph (`card_code` returns blank),
so the trailing period on "JOTD666." is dropped to keep that line truly centred. The
prior 7-line card had a 28-char line ("POSSIBLE WITHOUT JOTD666 WHO") that ran flush to
both edges and clipped on the right; the new max line is 18 cols, well inside 28.

---

## Fix 3 — centre the display

The playfield window now uses the **canonical centred DIWSTRT** —
`hstart = 0x81 + (320-224)/2` with `HWIN_ADJ = 0` — which is *identical* to the 1943 /
Terra Cresta ports (both leave `hwscroll_hwin_adj = 0` and read centred). The previous
`HWIN_ADJ = -8` nudge over-corrected and shifted the playfield right (big left margin /
right-edge clip). Sprites use the same `s_spr_base_h = 0x81+(320-DISPW)/2+hwin_adj`, so
fixing the window also re-centres the bobs and the credits card.

---

## Screenshots

See `shots/` (captured via `grim` from `tools/gunsmoke_grimshots.sh`):
- `card_*.png` — centred cube-drawn credits card during the warning phase
- `attract*.png` / `mid.png` — centred playfield (equal L/R margins)
