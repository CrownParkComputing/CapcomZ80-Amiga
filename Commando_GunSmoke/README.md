# Commando / GunSmoke Selector Bundle

Combined WhittyDemo selector for the Capcom Z80 RTG Commando and GunSmoke ports.

Build everything:

```sh
Commando_GunSmoke/tools/build_all.sh
```

Install to AGS/Pimiga `SHARED:`:

```sh
Commando_GunSmoke/tools/package_shared.sh
```

Install standalone Amiberry HDF/config:

```sh
Commando_GunSmoke/tools/package_bundle.sh
```

The game binaries are built with `NO_EMBEDDED_INTRO=1`, so the combined selector is the only loader.
