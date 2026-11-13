# Writes the xdelta3 patches of this folder. Run with an xdelta3 3.1.0 built with LZMA:
#   python -I make_vectors.py <xdelta3 executable> <scratch folder>
# The source is MakeRom(200000, 1) of ../encoders.h; the target is made here, and the tests
# know it by its size and CRC-32 (printed at the end).
import os, subprocess, sys, zlib

def make_rom(size, seed):
    rom = bytearray(size)
    state = (seed * 2654435761 + 1) & 0xFFFFFFFF
    i = 0
    while i < size:
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        rom[i] = state >> 24
        if (state >> 8) % 97 == 0:
            run = min(size - i, 40 + (state >> 16) % 300)
            rom[i:i + run] = b'\xff' * run
            i += run - 1
        i += 1
    return bytes(rom)

def text(size, seed):
    # Bytes of a small alphabet with repeats, like a script table: they pack well.
    words = [b'POTION', b'the ', b'route ', b'gym ', b'\x00\x01', b'badge', b' and ', b'CUT', b'\xfe\xfe']
    out = bytearray()
    state = seed
    while len(out) < size:
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        out += words[(state >> 16) % len(words)]
    return bytes(out[:size])

rom = make_rom(200000, 1)
changed = bytearray(rom[50000:80000])
for at in range(0, len(changed), 997):
    changed[at] ^= 0x5A
target = rom[:20000] + text(4000, 3) + bytes(changed) + text(5000, 4) + rom[10000:30000] + text(1500, 5)

# xdelta3 cannot read its own FGK patch when a window leaves a section unpacked that a later
# window packs (its coder's tree has moved on, the decoder's has not). So FGK gets a target
# without a source and without matches (-N): every window packs its added bytes.
def skewed(size, seed):
    out = bytearray(size)
    state = seed
    for i in range(size):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        out[i] = b'AAAAAAAAAAABBBCD'[state >> 28] if (state >> 8) & 63 else (state >> 16) & 0xFF
    return bytes(out)

fgk_target = skewed(40000, 11)
# Enough added bytes in one window for DJW to use several codes, which it does as "djw9".
def letters(size, seed):
    out = bytearray(size)
    state = seed
    for i in range(size):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        out[i] = 32 + (state >> 24) % 40 if i % 3000 < 1500 else 200 + (state >> 24) % 9
    return bytes(out)

djw_target = target + letters(12000, 12)

def crc(data):
    return 'size %d crc32 %08x' % (len(data), zlib.crc32(data))

exe, scratch = sys.argv[1], sys.argv[2]
here = os.path.dirname(os.path.abspath(__file__))
os.makedirs(scratch, exist_ok=True)
source_file = os.path.join(scratch, 'source.bin')
open(source_file, 'wb').write(rom)
for name, data, flags in (('lzma', target, ['-s', source_file]), ('djw', djw_target, ['-s', source_file]),
                          ('fgk', fgk_target, ['-N'])):
    target_file = os.path.join(scratch, name + '-target.bin')
    open(target_file, 'wb').write(data)
    out, back = os.path.join(here, name + '.xdelta'), os.path.join(scratch, name + '-back.bin')
    subprocess.check_call([exe, '-e', '-f', '-A', '-W', '16384', '-S', 'djw9' if name == 'djw' else name] + flags + [target_file, out])
    subprocess.check_call([exe, '-d', '-f'] + (flags[-2:] if name != 'fgk' else []) + [out, back])
    assert open(back, 'rb').read() == data
    print(name, os.path.getsize(out), 'bytes; target', crc(data))
