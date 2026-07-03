# Commando / GunSmoke Selector Bundle

Combined WhittyDemo selector for the Capcom Z80 RTG Commando and GunSmoke ports.

Build everything:

```sh
Commando_GunSmoke/tools/build_all.sh
```

ROM zips are not committed. Put `commando.zip` and `gunsmoke.zip` in one of
these locations before building:

```text
roms/
packages/zips/
Commando/roms/ and Gun_Smoke/roms/
~/Downloads/
```

You can also point at a specific GunSmoke zip with `GUNSMOKE_ZIP=/path/to/gunsmoke.zip`.

Install to AGS/Pimiga `SHARED:`:

```sh
Commando_GunSmoke/tools/package_shared.sh
```

Install standalone Amiberry HDF/config:

```sh
Commando_GunSmoke/tools/package_bundle.sh
```

The game binaries are built with `NO_EMBEDDED_INTRO=1`, so the combined selector is the only loader.

Commando and GunSmoke both build against the shared Z80/YM2203 interpreter core
in `shared_source/CapcomZ80Core`. Their machine maps and RTG renderers stay
game-specific because the video hardware layouts are different.
