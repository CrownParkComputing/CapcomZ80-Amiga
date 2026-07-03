#!/usr/bin/env python3
from pathlib import Path
import hashlib
import shutil
import zipfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
HOME = Path.home()
SRC = HOME / "Commando" / "ports" / "commando" / "v1" / "roms"
OUT = HERE / "build" / "rcommando"
NAMES = ("main.bin", "g1.bin", "g2.bin", "g3.bin", "proms.bin", "snd.bin")
SHA1 = {
    "main.bin": "b57911da558de11d0bb5be4e71d180b710cbfdac",
    "g1.bin": "2435c87c9c9d78a6e703cf0e1f6a0288207fcd4c",
    "g2.bin": "a804eb080a66b4c1b12a4509f509f49c5374977c",
    "g3.bin": "be2c16663de5fd4536f3a953f11b1f8c099d491f",
    "proms.bin": "524236e477d1c31d834aeadf297d3285b338865f",
    "snd.bin": "f242f64b667c104ff4d0a96d39d990472d159480",
}

# Fallback for the centralised board package: the old external conversion tree
# may have been removed, but the original package zip/shared executable still
# contains the exact ROM blobs as one contiguous hunk data segment.
PACKAGE_ZIPS = (
    REPO / "packages" / "zips" / "Commando.zip",
    HOME / "AmigaArcadePorts" / "packages" / "zips" / "Commando.zip",
)
MAME_ZIPS = (
    HOME / "Downloads" / "commando.zip",
)
SHARED_EXE = HOME / "AGS_UAE" / "SHARED" / "Capcom_Z80_Dual_YM2203" / "Commando" / "Commando"
EMBEDDED = {
    "main.bin": (0x14c6e4 + 0x000004, 0x0c000),
    "g1.bin": (0x14c6e4 + 0x00c004, 0x04000),
    "g2.bin": (0x14c6e4 + 0x010004, 0x18000),
    "g3.bin": (0x14c6e4 + 0x028004, 0x18000),
    "proms.bin": (0x14c6e4 + 0x040004, 0x00500),
    "snd.bin": (0x14c6e4 + 0x040504, 0x10000),
}

def blob_ok(path, name):
    if not path.exists():
        return False
    return hashlib.sha1(path.read_bytes()).hexdigest() == SHA1[name]

def staged_ok():
    return all(blob_ok(OUT / n, n) for n in NAMES)

def extract_from_exe(data):
    for n in NAMES:
        off, size = EMBEDDED[n]
        if off + size > len(data):
            raise ValueError("packaged executable is shorter than expected")
        (OUT / n).write_bytes(data[off:off + size])
    if not staged_ok():
        raise ValueError("recovered Commando blobs failed hash validation")

def extract_from_mame_zip(zf):
    blobs = {
        "main.bin": zf.read("cm04.9m") + zf.read("cm03.8m"),
        "g1.bin": zf.read("vt01.5d"),
        "g2.bin": b"".join(zf.read(n) for n in (
            "vt11.5a", "vt12.6a", "vt13.7a", "vt14.8a", "vt15.9a", "vt16.10a")),
        "g3.bin": b"".join(zf.read(n) for n in (
            "vt05.7e", "vt06.8e", "vt07.9e", "vt08.7h", "vt09.8h", "vt10.9h")),
        "proms.bin": b"".join(zf.read(n) for n in (
            "vtb1.1d", "vtb2.2d", "vtb3.3d", "vtb4.1h", "vtb6.6e")),
        "snd.bin": zf.read("cm02.9f") + bytes(0x10000 - len(zf.read("cm02.9f"))),
    }
    for n, data in blobs.items():
        (OUT / n).write_bytes(data)
    if not staged_ok():
        raise ValueError("raw Commando zip blobs failed hash validation")

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    if staged_ok():
        print(f"reusing staged Commando ROM blobs -> {OUT}")
        return

    if all((SRC / n).exists() for n in NAMES):
        for n in NAMES:
            shutil.copyfile(SRC / n, OUT / n)
        if not staged_ok():
            raise SystemExit(f"Commando ROM blobs in {SRC} did not match the expected set")
        print(f"staged Commando ROM blobs -> {OUT}")
        return

    try:
        for mame_zip in MAME_ZIPS:
            if mame_zip.exists():
                with zipfile.ZipFile(mame_zip) as zf:
                    extract_from_mame_zip(zf)
                print(f"staged Commando ROM blobs from {mame_zip} -> {OUT}")
                return
        for package_zip in PACKAGE_ZIPS:
            if package_zip.exists():
                with zipfile.ZipFile(package_zip) as zf:
                    extract_from_exe(zf.read("Commando/Commando"))
                print(f"recovered Commando ROM blobs from {package_zip} -> {OUT}")
                return
        if SHARED_EXE.exists():
            extract_from_exe(SHARED_EXE.read_bytes())
            print(f"recovered Commando ROM blobs from {SHARED_EXE} -> {OUT}")
            return
    except (KeyError, ValueError) as e:
        raise SystemExit(f"failed to recover Commando ROM blobs: {e}")

    missing = [n for n in NAMES if not (SRC / n).exists()]
    raise SystemExit(f"missing Commando arcade ROM blobs in {SRC}: {', '.join(missing)}")

if __name__ == "__main__":
    main()
