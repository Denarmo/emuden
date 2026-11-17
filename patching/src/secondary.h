// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The secondary compressors of xdelta3: a section of a VCDIFF window (added bytes,
// instructions or addresses) may be packed by one of them. Each kind of section has its own
// decoder for the whole patch, because LZMA and FGK carry their state from window to window.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "io.h"

namespace den::patching {

constexpr std::uint8_t SECONDARY_DJW = 1, SECONDARY_LZMA = 2, SECONDARY_FGK = 16;

class Secondary {
public:
    virtual ~Secondary() = default;
    /// Fills `out` from `in`. False when the input is damaged or does not give `size` bytes.
    virtual bool Decode(Cursor& in, std::uint8_t* out, std::size_t size) = 0;
};

/// Null for an id that is none of the three.
std::unique_ptr<Secondary> MakeSecondary(std::uint8_t id);

std::unique_ptr<Secondary> MakeDjwDecoder();
std::unique_ptr<Secondary> MakeFgkDecoder();
std::unique_ptr<Secondary> MakeLzmaDecoder();

/// Fills XZ Embedded's checksum tables once; before any other call of it (xz_file.cpp).
void PrepareXz();

/// The bits of a section, as xdelta3 packs them: the low bit of a byte first.
struct BitReader {
    Cursor& in;
    unsigned byte = 0, mask = 0x100;

    unsigned Bit() {
        if (mask == 0x100) {
            byte = in.U8();
            mask = 1;
        }
        const unsigned bit = (byte & mask) != 0 ? 1 : 0;
        mask <<= 1;
        return bit;
    }
    /// A number of `count` bits, its high bit first.
    unsigned Number(int count) {
        unsigned value = 0;
        for (int i = 0; i < count; ++i) {
            value = (value << 1) | Bit();
        }
        return value;
    }
};

} // namespace den::patching
