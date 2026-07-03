# Commando

Capcom 1985 Z80 hardware, built as an RTG presenter for the shared
Capcom_Z80_Dual_YM2203 package.

## Run

Launch `Commando` from the game folder or the AGS shared launcher.

## Controls

- Stick / cursor keys: move
- Fire / CD32 Blue or Red / Ctrl / Space: shoot
- Second fire / CD32 Yellow or Green / Alt: grenade
- `5` / CD32 shoulder: coin
- `1` / Return / CD32 Play: start
- `F10` / CD32 L+R+Play: DIP switches and pause
- `Esc`: exit

## Status

The main and sound Z80s both use the interpreted path, matching the current
Gun.Smoke stack, with the sound board driving two YM2203 chips. Paula output
uses the 1943-style sample-clocked audio renderer through the same lead-buffer
ring approach as Black Tiger at 8040 Hz, so music tempo is not tied to RTG frame
jitter and F10/DIP pause can stop and restart audio cleanly. The renderer uses
cached software graphics decode for the 224x256 rotated play area and follows
Commando's verified priority order: background, sprites, then foreground, with
the bridge road-bike case redrawn above the foreground so the player remains
under the overpass while the motorcycle stays visible. The RTG screen is
864x486 RGB332, paints the Bezel Project backdrop once, and refreshes only the
play window during gameplay. Rebuild from `source/build.sh`; this also emits
`dist/Commando_RTG.hdf` and `dist/Commando-RTG.uae`.
