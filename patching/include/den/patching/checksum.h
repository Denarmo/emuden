// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Checksums of a ROM in every form a patch may have been made against: as the file is, without
// its header, in the reference byte order.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"

namespace den::patching {

/// SHA-1 fed in pieces.
class Sha1 {
public:
    Sha1();
    void Update(const void* data, std::size_t size);
    /// Lower-case hex. The object is finished afterwards.
    std::string FinishHex();

private:
    void Block(const std::uint8_t* p);

    std::array<std::uint32_t, 5> h;
    std::array<std::uint8_t, 64> pending{};
    std::size_t pending_size = 0;
    std::uint64_t total = 0;
};

std::string Sha1Hex(const void* data, std::size_t size);

struct Checksums {
    std::uint32_t crc32 = 0;
    std::string md5, sha1; // lower-case hex
    std::uint64_t size = 0;
};

struct RomForm {
    std::string form; // "raw", "headerless", "z64"
    Checksums sums;
    std::uint64_t header_bytes = 0; // bytes removed in front
    bool reordered = false;         // N64 byte order was normalised
};

/// Every form the system has for this file; the first is always "raw". NES and FDS: without
/// the 16-byte header. SNES: without a 512-byte copier header. Lynx: without the 64-byte
/// header. N64: big-endian ("z64") when the file is byte-swapped or little-endian.
/// `cancelled` is asked between blocks.
Result<std::vector<RomForm>> RomChecksums(const std::filesystem::path& rom, std::string_view system,
                                          const std::function<bool()>& cancelled = {});

/// Writes that form of the ROM to `out` (the file a patch is applied to).
Status WriteForm(const std::filesystem::path& rom, std::string_view system, std::string_view form,
                 const std::filesystem::path& out);

/// Puts the header `form` removed from `original` back in front of `patched`. A reordered
/// N64 ROM stays big-endian.
Status RestoreHeader(const std::filesystem::path& original, const RomForm& form,
                     const std::filesystem::path& patched);

} // namespace den::patching
