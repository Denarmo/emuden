// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "den/base/files.h"

namespace den {

struct ZipEntry {
    /// Path inside the archive, forward slashes.
    std::string name;
    std::uint64_t size = 0;
    std::uint64_t compressed_size = 0;
    std::uint64_t header_offset = 0;
    std::uint32_t crc = 0;
    std::uint16_t method = 0; // 0 stored, 8 deflate
    bool encrypted = false;
    /// A symbolic link. It is never followed or created.
    bool symlink = false;
};

/// The files of a zip archive (folders left out), from its central directory. Zip64 archives
/// are read too. Nothing when the file is not a zip.
std::optional<std::vector<ZipEntry>> ListZip(const std::filesystem::path& archive);

/// Stored files always can be; deflated ones when the build has zlib.
bool CanExtractZipEntry(const ZipEntry& entry);

/// Writes one file of the archive to `destination` (its folder must exist) and checks its CRC.
/// The file appears under its name only once it is complete. `cancelled` is asked between blocks.
Status ExtractZipEntry(const std::filesystem::path& archive, const ZipEntry& entry,
                         const std::filesystem::path& destination,
                         const std::function<bool()>& cancelled = {});

} // namespace den
