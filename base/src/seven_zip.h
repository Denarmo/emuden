// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "den/base/archive.h"

namespace den {

/// How a 7z's solid blocks are unpacked.
struct SevenZipLimits {
    /// Blocks up to this size are unpacked whole in memory (and kept for the block's other
    /// files); larger ones are decoded in pieces where their coders allow it.
    std::uint64_t memory_block = 256ull << 20;
    /// The largest block unpacked in memory when it cannot be decoded in pieces (BCJ2).
    std::uint64_t max_memory_block = 1ull << 30;
    /// The largest dictionary a block decoded in pieces may use: it is held in memory.
    std::uint64_t max_window = 256ull << 20;
};

/// The file's block is decoded in pieces rather than unpacked whole in memory.
inline bool SevenZipStreams(const ZipEntry& entry, const SevenZipLimits& limits) {
    return entry.streams && entry.block_size > limits.memory_block && entry.window <= limits.max_window;
}

/// A 7z archive read with the LZMA SDK's decoder. Nothing when it cannot be listed.
std::unique_ptr<ArchiveReader> OpenSevenZip(const std::filesystem::path& archive,
                                            const SevenZipLimits& limits = {});

/// Why a file of the 7z `archive_name` cannot be unpacked, as a sentence; empty when it can.
std::string SevenZipRefusal(const ZipEntry& entry, const std::string& archive_name,
                            const SevenZipLimits& limits = {});

} // namespace den
