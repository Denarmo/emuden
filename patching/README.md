# patching

ROM patches and ROM checksums. Depends on `base` only; the library uses it for hacks
(`den/library/hacks.h`).

| Header | What |
|---|---|
| `patch.h` | `Inspect(patch)` (format, source and target size and CRC-32 when the patch carries them), `Apply(patch, source, output, options)`, `IsPatchExtension` |
| `checksum.h` | `RomChecksums(rom, system)`: CRC-32, MD5 and SHA-1 of the file and of its other form; `WriteForm`, `RestoreHeader`, `Sha1` |

| Format | Checked | Notes |
|---|---|---|
| IPS | nothing (the format has no check) | RLE records, the cut size after `EOF`, a record at the offset that spells `EOF` |
| UPS | source, target and patch CRC-32 | source to target only; the patched game given as the base is named as such |
| BPS | source, target and patch CRC-32 | the four actions, metadata skipped |
| APS (GBA) | sizes, CRC-16 of every 64 KiB block | not the N64 `APS10` |
| PPF 1, 2, 3 | v2: size; v2/v3: the 1024-byte check block | undo data and the description block skipped |
| xdelta3 / VCDIFF | Adler-32 of every target window | standard instruction table, source and target windows. Sections packed by a secondary compressor (LZMA, DJW, FGK) are refused with that reason: `xdelta3 -S none` patches apply |

`Apply` reads the source through a page cache and writes the result to `<output>.part`, renamed
when every check passed, so a disc image is never in memory. The patch file is, and so is one target window of a
VCDIFF patch at a time (xdelta3 writes 8 MiB windows; more than 256 MiB is refused).

Forms: NES and FDS without the 16-byte header, SNES without a 512-byte copier header, Lynx
without its 64-byte header (`headerless`); N64 `.v64` and `.n64` as big-endian (`z64`).

Tests make their patches with the encoders in `tests/encoders.h` and `tests/vcdiff_writer.h`;
no ROM or patch file is in the repository.
