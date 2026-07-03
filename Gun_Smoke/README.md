# Gun.Smoke

Current Amiga build: RTG presenter and interpreter build under the Capcom
Z80 / Dual YM2203 package.

## Run

Launch `Gun.Smoke` from the game folder or the AGS shared launcher.

## Controls

- Stick / cursor keys: move
- Fire / CD32 Blue or Red / Ctrl / Space: shoot
- Second fire / CD32 Yellow or Green / Alt: aim
- `5` / CD32 shoulder: coin
- `1` / Return / CD32 Play: start
- `P` / CD32 Yellow+Green: pause
- `Q` / hold all four CD32 face buttons: exit

## Status

This folder is the current Gun.Smoke RTG interpreter build under the Capcom
package: the launcher-first binary is at the folder root, with HDF images in
`dist/`. The board is the family Capcom Z80 hardware with a sound Z80 driving
two YM2203 chips through Paula. Rebuild from `source/build.sh`; this emits the
folder executable, `prebuilt/gunsmoke`, `dist/GunSmoke_RTG.hdf`, and
`dist/GunSmoke-RTG.uae`.
