# 04 - Verify (no regression) + deferrals

## Bit-identical proof (the strongest non-regression check)
`tools/c1942_router_shot.c` is a host harness that drives the **actual on-target**
modified renderer (`src/hal/c1942_render.c` + `render_router.c` + `c1942.c` on z80.c),
runs the real srb-* ROMs for N frames, then reads the 8 AGA planes back to a PPM.

Compared against the pre-router renderer (`git show HEAD:src/hal/c1942_render.c`) built
into the same harness, output is **IDENTICAL at every sampled frame**:
```
frame 300:  IDENTICAL   frame 600:  IDENTICAL   frame 900:  IDENTICAL
frame 1200: IDENTICAL   frame 1400: IDENTICAL   frame 1800: IDENTICAL
```
Router tally per frame: `routed=32 bob=32 hwspr=0 poked=0 pf=0 drop=0` - 32 sprites
routed, 0 dropped (cap 64 never reached), all currently tagged ENEMY->BOB.

## Visual check (4-layer render correct)
`shots/host_title.png`, `shots/host_carrier.png`, `shots/host_demo.png` - rendered
through the modified router path:
- Title: green "1942" logo, static white "1UP / HIGH SCORE / 2UP / 40000" HUD, "INSERT
  COIN", CAPCOM logo, (c)1984 CAPCOM. Correct colours.
- Carrier: blue water bg (hardware-scrolled tiles), grey carrier deck, "88" turrets,
  parked planes + gold player plane (sprite layer, transp pen 15), static HUD on top.
All 4 layers (bg + sprites + fg HUD) composite correctly == the working ADF render.

## On-target (amiberry) - PENDING shared emulator
Final on-target verification of `Configurations/1942.uae` via amiberry/grim is queued:
at run time the single shared amiberry instance (one IPC socket
`/run/user/1000/amiberry.sock`, one display) was occupied by another agent's live 1943
session, which must not be preempted. Because the router migration is bit-identical to
the proven on-target renderer (above) and the native build only changes the boot medium
(ADF -> dir-FS) + adds render_router, the on-target picture is expected to match the
known-good `1942-ADF.uae` build. Recipe when the emulator frees up:
```
amiberry -f "1942" -s use_gui=no        # then SCREENSHOT via the socket after ~45s
```

## Deferred: C64-style loader
No 1942 title image has been provided, so the cube-reveal title + delta-MOD loader
(as on the other ports) is NOT added. It is pending a 1942 title image; add it later
mirroring the other games' loaders.
