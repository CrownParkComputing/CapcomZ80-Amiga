#!/usr/bin/env python3
from pathlib import Path
import hashlib
import zipfile

HERE = Path(__file__).resolve().parent
GAME = HERE.parent
ROOT = HERE.parents[2]
ROMS = GAME / "roms"
PACKAGE_ZIP = ROOT / "packages" / "zips" / "Side Arms.zip"
FALLBACK_EXE = GAME / "Side Arms"

ROM_SHA1 = {
    "sa03.bin": "ae59461768d044f14b9aac3e4e491c76cec7adac",
    "a_14e.rom": "b11dbd9889db89cff008ca21beb6b1b70d983e16",
    "a_12e.rom": "5c1d154f9c1de6b5f5d7abf5d413e9c493461e6f",
    "a_10j.rom": "9c821a2ee30c222987f0d4192133776490d6a4e0",
    "b_13d.rom": "5459a5795cf13012674993aa55bbd39e9a5c2f1b",
    "b_13e.rom": "54fe6f258fda509a92eb0f5aa238102efce729e0",
    "b_13f.rom": "19477e284967beafc4e7cd0d0da3534eb6dec388",
    "b_13g.rom": "963e105aa0b0174e8aa5e1f7676c5c604ca72d1c",
    "b_14d.rom": "ad5ed9a81805dde54fb2703345b2ab7b56853ec6",
    "b_14e.rom": "491e880e85d5256fa2eea6d0fb402f0a1176b675",
    "b_14f.rom": "0f8fe5dc32ee50ebb2051c0c0c4d635582416317",
    "b_14g.rom": "499b17eeb5e7256ede477510b0547df520316996",
    "b_11b.rom": "15e250aa98ee69ac3983d4511976c35833b37cab",
    "b_13b.rom": "6557344ce8bc05309ab8ebe846871ed554b256b8",
    "b_11a.rom": "00b3cab899e5ac1af6300f2ec2a54303df9ab014",
    "b_13a.rom": "b1bfb7604791950aa0454b68b24f6ad3b9131be8",
    "b_12b.rom": "c33b0ab6f7f0f886410a3943988b737d175635be",
    "b_14b.rom": "27144834b5b2849be8c46e97aaaeaa8b304ea810",
    "b_12a.rom": "2235281449247cb2446b008b36077788c5b15026",
    "b_14a.rom": "87b3b3437bc4bd727ce7e34dd914e6fe23bcac3d",
    "b_03d.rom": "b500bc32ba47e9cc9dcf2254b9455ac4d61992db",
    "a_04k.rom": "e1d8895c113e4dee1a132e2471d75dfa6c36b620",
}

LAYOUT = [
    ("sa03.bin", 0x8000),
    ("a_14e.rom", 0x8000),
    ("a_12e.rom", 0x8000),
    ("__zero__", 0x10000),
    ("a_10j.rom", 0x4000),
]
LAYOUT += [(name, 0x8000) for name in [
    "b_13d.rom", "b_13e.rom", "b_13f.rom", "b_13g.rom",
    "b_14d.rom", "b_14e.rom", "b_14f.rom", "b_14g.rom",
    "b_11b.rom", "b_13b.rom", "b_11a.rom", "b_13a.rom",
    "b_12b.rom", "b_14b.rom", "b_12a.rom", "b_14a.rom",
    "b_03d.rom", "a_04k.rom",
]]


def sha1(data):
    return hashlib.sha1(data).hexdigest()


def roms_ok():
    return all((ROMS / name).exists() and sha1((ROMS / name).read_bytes()) == want
               for name, want in ROM_SHA1.items())


def find_embedded_romdata(data):
    total = sum(size for _, size in LAYOUT)
    for off in range(0, len(data) - total + 1, 2):
        p = off
        ok = True
        for name, size in LAYOUT:
            blob = data[p:p + size]
            if name == "__zero__":
                ok = blob == b"\0" * size
            else:
                ok = sha1(blob) == ROM_SHA1[name]
            if not ok:
                break
            p += size
        if ok:
            return off
    return -1


def extract_from_exe(data):
    off = find_embedded_romdata(data)
    if off < 0:
        raise ValueError("embedded Side Arms ROM block not found")
    p = off
    for name, size in LAYOUT:
        blob = data[p:p + size]
        if name != "__zero__":
            (ROMS / name).write_bytes(blob)
        p += size


def main():
    ROMS.mkdir(parents=True, exist_ok=True)
    if roms_ok():
        print(f"reusing staged Side Arms ROM blobs -> {ROMS}")
        return
    if PACKAGE_ZIP.exists():
        with zipfile.ZipFile(PACKAGE_ZIP) as zf:
            extract_from_exe(zf.read("Side Arms/Side Arms"))
    elif FALLBACK_EXE.exists():
        extract_from_exe(FALLBACK_EXE.read_bytes())
    else:
        raise SystemExit("missing Side Arms package/executable for ROM recovery")
    if not roms_ok():
        raise SystemExit("recovered Side Arms ROM blobs failed hash validation")
    print(f"recovered Side Arms ROM blobs -> {ROMS}")


if __name__ == "__main__":
    main()
