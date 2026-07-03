#!/usr/bin/env python3
from pathlib import Path
import hashlib
import shutil
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
REPO = HERE.parents[2]
SRC = ROOT / "Commando" / "ports" / "commando" / "v1" / "roms"
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
PACKAGE_ZIP = REPO / "packages" / "zips" / "Commando.zip"
SHARED_EXE = ROOT / "AGS_UAE" / "SHARED" / "Capcom_Z80_Dual_YM2203" / "Commando" / "Commando"
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
        if PACKAGE_ZIP.exists():
            with zipfile.ZipFile(PACKAGE_ZIP) as zf:
                extract_from_exe(zf.read("Commando/Commando"))
            print(f"recovered Commando ROM blobs from {PACKAGE_ZIP} -> {OUT}")
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
