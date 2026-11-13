#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the 7z archives base/tests/archive_tests.cpp reads, with the 7-Zip command line
tool (7z, 7za or 7zz on the path, or the Windows install folder). The archives are
committed; run this only to change them, then copy the sizes and checksums it prints into
the test.

    python3 base/tests/data/make_vectors.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def tool():
    for name in ("7z", "7za", "7zz"):
        found = shutil.which(name)
        if found:
            return found
    installed = os.path.join(os.environ.get("ProgramFiles", ""), "7-Zip", "7z.exe")
    if os.path.exists(installed):
        return installed
    sys.exit("no 7z tool found")


def text():
    lines = []
    for i in range(700):
        lines.append("Line %d of the read-me: patch %d of %d, checked.\n" % (i, i * 7 % 31, i % 13))
    return "".join(lines).encode()


def code():
    # Looks like x86 code to the BCJ filter: calls (E8) with relative addresses.
    out = bytearray()
    state = 12345
    while len(out) < 24000:
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        if state % 5 == 0:
            out += b"\xE8" + ((state >> 4) & 0xFFF).to_bytes(4, "little")
        else:
            out.append((state >> 16) & 0xFF)
    return bytes(out[:24000])


FILES = {
    "text.txt": text(),
    "sub/code.bin": code(),
    "empty.txt": b"",
    "Pokémon ポケ.txt": b"names are UTF-16 in a 7z\n",
}

# name -> (switches, files)
ARCHIVES = {
    "lzma2-solid.7z": (["-m0=lzma2:d=64k", "-ms=on"], list(FILES)),
    "lzma-each.7z": (["-m0=lzma:d=64k", "-ms=off"], ["text.txt", "sub/code.bin"]),
    "bcj-lzma.7z": (["-m0=bcj", "-m1=lzma:d=64k"], ["sub/code.bin"]),
    "bcj2-lzma.7z": (["-m0=bcj2", "-m1=lzma:d=64k", "-m2=lzma:d=64k", "-m3=lzma:d=64k",
                      "-mb0:1", "-mb0s1:2", "-mb0s2:3"], ["sub/code.bin"]),
    "delta-lzma2.7z": (["-m0=delta:2", "-m1=lzma2:d=64k"], ["sub/code.bin", "text.txt"]),
    "copy.7z": (["-m0=copy", "-mhc=off"], ["text.txt", "sub/code.bin"]),
    "ppmd.7z": (["-m0=ppmd:mem=1m:o=4"], ["text.txt"]),
    "password.7z": (["-m0=lzma2:d=64k", "-psecret"], ["text.txt"]),
    "password-names.7z": (["-m0=lzma2:d=64k", "-psecret", "-mhe=on"], ["text.txt"]),
}


def main():
    seven = tool()
    with tempfile.TemporaryDirectory() as work:
        for name, data in FILES.items():
            path = os.path.join(work, *name.split("/"))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as out:
                out.write(data)
            os.utime(path, (1700000000, 1700000000))
        for archive, (switches, names) in ARCHIVES.items():
            target = os.path.join(HERE, archive)
            if os.path.exists(target):
                os.remove(target)
            command = [seven, "a", "-t7z", "-mmt=1", "-mtm=off", "-bso0", "-bsp0"] + switches
            subprocess.run(command + [target] + names, cwd=work, check=True)
            print("%-20s %6d bytes" % (archive, os.path.getsize(target)))
    for name, data in FILES.items():
        print("%-24s size %6d crc 0x%08X" % (name.encode("unicode_escape").decode(), len(data),
                                             zlib.crc32(data)))


if __name__ == "__main__":
    main()
