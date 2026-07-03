# Source Layout

This hardware package contains only the current Capcom Z80 / dual YM2203 RTG build sources.

- `1943/source/` - current 1943 RTG interpreter source.
- `1943_Kai/source/` - current 1943 Kai RTG interpreter source.
- `1942/source/` - current 1942 Amiga source and build helpers.
- `Side_Arms/source/` - current Side Arms RTG bezel source.
- `Black_Tiger/source/` - current Black Tiger RTG Bezel Project source.
- `Commando/source/` - current Commando RTG Bezel Project source.
- `Gun_Smoke/source/` - current Gun.Smoke RTG interpreter source.
- `shared_source/Gaplus/src/` - shared `gaplus_menu` helper used by Side Arms and Black Tiger.

The shared intro/loader code (ArcadeIntro) is centralized at the repo-root
`/home/jon/AmigaArcadePorts/ArcadeIntro` and referenced by each game's `build.sh`.

Each game folder follows the standard launcher-first layout: the executable, its
`.info`, `index.html`, and `README.md` stay at the top; `assets/` holds box art /
bezel / icon art, `screenshots/` holds gameplay shots, and `source/` holds the build
source.

Object files, built executables, host tests, old AGA presenters, old bezel
experiments, and backup files are intentionally not copied here.
