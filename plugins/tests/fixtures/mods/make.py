# Writes pax.tar.xz and gnu.tar.xz beside this file (feature_mods_tests.cpp reads them):
# python3 -I make.py
import io
import lzma
import os
import tarfile

LONG = "release/" + "deep-folder-name/" * 7 + "a-file-with-a-long-name.txt"


def big():
    return bytes((i * 31 + (i >> 8)) & 0xFF for i in range(200_000))


def add(tar, name, data):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mtime = 1794528000
    tar.addfile(info, io.BytesIO(data))


def make(name, fmt, check):
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=fmt) as tar:
        folder = tarfile.TarInfo("release")
        folder.type = tarfile.DIRTYPE
        folder.mtime = 1794528000
        tar.addfile(folder)
        add(tar, "./release/lib/liblayer.so", b"LIBRARY")
        add(tar, "release/share/layer.json", b'{"file_format_version": "1.0.0"}\n')
        add(tar, "release/big.bin", big())
        link = tarfile.TarInfo("release/link.so")
        link.type = tarfile.SYMTYPE
        link.linkname = "lib/liblayer.so"
        tar.addfile(link)
        add(tar, LONG, b"long")
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, name), "wb") as out:
        out.write(lzma.compress(raw.getvalue(), format=lzma.FORMAT_XZ, check=check, preset=6))


make("pax.tar.xz", tarfile.PAX_FORMAT, lzma.CHECK_CRC64)
make("gnu.tar.xz", tarfile.GNU_FORMAT, lzma.CHECK_CRC32)
