"""
smoke_check.py - offline sanity checks for Parry Loot repository and build artifacts.

Checks:
  1. VERSION matches semver, has non-empty notes in CHANGELOG.md, and equals
     kVersion in src/ParryLoot.cpp (the version the DLL writes to its log).
  2. Default INI contains the expected keys.
  3. Nexus page description.bbcode & summary.txt validate cleanly.
  4. If build/ParryLoot.dll or dist/ParryLoot-<version>.zip exist, verifies
     x64 PE/COFF headers, zero MinGW/VC runtime DLL imports, and zip contents
     (exactly the four release files, every entry name with '/').
"""
from __future__ import annotations

import os
import re
import struct
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOWED_IMPORTS = {"kernel32.dll", "msvcrt.dll"}


def fail(msg: str) -> None:
    print(f"::error::{msg}", file=sys.stderr)
    sys.exit(1)


def read_pe_imports(pe_bytes: bytes) -> tuple[int, list[str]]:
    if pe_bytes[:2] != b"MZ":
        fail("ParryLoot.dll missing MZ header")
    e_lfanew = struct.unpack_from("<I", pe_bytes, 0x3C)[0]
    if pe_bytes[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
        fail("ParryLoot.dll missing PE signature")
    machine, nsec = struct.unpack_from("<HH", pe_bytes, e_lfanew + 4)
    opt_sz = struct.unpack_from("<H", pe_bytes, e_lfanew + 20)[0]
    magic = struct.unpack_from("<H", pe_bytes, e_lfanew + 24)[0]
    if magic != 0x20B:
        fail(f"Expected PE32+ (64-bit), got magic 0x{magic:x}")

    sec_off = e_lfanew + 24 + opt_sz
    secs = []
    for i in range(nsec):
        s = pe_bytes[sec_off + i * 40:sec_off + (i + 1) * 40]
        vsz, va, rsz, ra = struct.unpack_from("<4I", s, 8)
        secs.append((va, max(vsz, rsz), ra))

    def rva_to_off(rva: int) -> int:
        for va, span, ra in secs:
            if va <= rva < va + span:
                return ra + (rva - va)
        fail(f"Unmapped import RVA 0x{rva:x}")

    # Import Directory is DataDirectory[1], at offset 120 inside PE32+ Optional Header
    imp_rva, imp_size = struct.unpack_from("<II", pe_bytes, e_lfanew + 24 + 120)
    imports: list[str] = []
    if imp_rva and imp_size:
        pos = rva_to_off(imp_rva)
        while True:
            orig_thunk, ts, fwd, name_rva, first_thunk = struct.unpack_from("<5I", pe_bytes, pos)
            if name_rva == 0 and first_thunk == 0:
                break
            noff = rva_to_off(name_rva)
            end = pe_bytes.index(b"\x00", noff)
            imports.append(pe_bytes[noff:end].decode("ascii", errors="replace"))
            pos += 20
    return machine, imports


def check_dll(pe_bytes: bytes, label: str, version: str) -> list[str]:
    machine, imports = read_pe_imports(pe_bytes)
    if machine != 0x8664:
        fail(f"{label}: expected AMD64 (0x8664), got 0x{machine:x}")
    unexpected = [d for d in imports if d.lower() not in ALLOWED_IMPORTS]
    if unexpected:
        fail(f"{label} has unexpected external DLL imports: {unexpected}")
    # The DLL logs "ParryLoot <kVersion> initializing..."; without both strings it was built
    # from older source than this VERSION.
    if b"ParryLoot %s initializing" not in pe_bytes or version.encode("ascii") + b"\0" not in pe_bytes:
        fail(f"{label} was not built from this source (no version string {version!r}). "
             f"Run .\\pack.ps1 -Build.")
    return imports


def main() -> int:
    with open(os.path.join(ROOT, "VERSION"), encoding="utf-8") as f:
        version = f.read().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        fail(f"Invalid VERSION: {version!r}")

    # 1. Check changelog & nexus page helpers
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import nexus_page
    import release_notes

    with open(os.path.join(ROOT, "CHANGELOG.md"), encoding="utf-8") as f:
        if not release_notes.section(f.read(), version):
            fail(f"CHANGELOG.md has no section for version {version}")

    with open(os.path.join(ROOT, "src", "ParryLoot.cpp"), encoding="utf-8") as f:
        m = re.search(r'constexpr char kVersion\[\] = "([^"]*)";', f.read())
    if not m:
        fail("src/ParryLoot.cpp has no kVersion constant")
    if m.group(1) != version:
        fail(f"kVersion in src/ParryLoot.cpp is {m.group(1)!r}, VERSION is {version!r}")

    probs = nexus_page.problems()
    if probs:
        fail("Nexus page problems: " + "; ".join(probs))

    # 2. Check INI defaults
    ini_path = os.path.join(ROOT, "assets", "mods", "ParryLoot.ini")
    with open(ini_path, encoding="utf-8") as f:
        ini_text = f.read()
    for key in ("enabled", "duration", "item_discovery_bonus", "melee_parry", "spell_parry", "sp_effect_id", "log"):
        if not re.search(rf"(?m)^{key}\s*=", ini_text):
            fail(f"ParryLoot.ini missing key '{key}'")

    # 3. Check compiled DLL if present
    dll_path = os.path.join(ROOT, "build", "ParryLoot.dll")
    if os.path.exists(dll_path):
        with open(dll_path, "rb") as f:
            imports = check_dll(f.read(), dll_path, version)
        print(f"PE check OK: {dll_path} (x64, version {version}, imports={imports})")

    # 4. Check release zip if present
    zip_path = os.path.join(ROOT, "dist", f"ParryLoot-{version}.zip")
    if os.path.exists(zip_path):
        expected = {
            "mods/ParryLoot.dll",
            "mods/ParryLoot.ini",
            # Named so that Vortex keeps them (see pack.ps1): no README.txt, no LICENSE without
            # an extension.
            "mods/ParryLoot/ParryLoot-README.txt",
            "mods/ParryLoot/LICENSE.txt",
        }
        with zipfile.ZipFile(zip_path) as zf:
            # orig_filename is the name as stored: on Windows, ZipInfo.filename turns
            # backslashes into slashes and would hide an archive that breaks elsewhere.
            stored = [i.orig_filename for i in zf.infolist() if not i.orig_filename.endswith("/")]
            backslashed = [n for n in stored if "\\" in n]
            if backslashed:
                fail(f"{zip_path} has entry names with backslashes (the zip format requires '/'; "
                     f"Linux tools extract them as flat files): {backslashed}")
            names = set(stored)
            missing = expected - names
            if missing:
                fail(f"{zip_path} missing expected entries: {sorted(missing)}")
            extra = names - expected
            if extra:
                fail(f"{zip_path} contains unexpected entries: {sorted(extra)}")
            check_dll(zf.read("mods/ParryLoot.dll"), f"{zip_path}: mods/ParryLoot.dll", version)
        print(f"Zip check OK: {zip_path} ({sorted(names)})")

    print(f"All smoke checks passed for Parry Loot v{version}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
