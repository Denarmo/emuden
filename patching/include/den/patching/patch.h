// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// ROM patches: IPS, UPS, BPS, APS (GBA), PPF 1 to 3 and VCDIFF (xdelta3). The source and the
// result are streamed, so a disc image is never held in memory; the patch file is.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "den/base/result.h"
#include "den/patching/checksum.h"

namespace den::patching {

enum class Format { Unknown, Ips, Ups, Bps, ApsGba, Vcdiff, Ppf };

struct PatchInfo {
    Format format = Format::Unknown;
    std::string format_name; // "ips", "ups", "bps", "aps", "xdelta", "ppf"
    std::optional<std::uint64_t> source_size, target_size;
    std::optional<std::uint32_t> source_crc32, target_crc32;
};

/// What the patch says about itself, by its first bytes (the file name does not matter).
Result<PatchInfo> Inspect(const std::filesystem::path& patch);

/// The file extensions patches come with, lower case without the dot.
bool IsPatchExtension(std::string_view extension);

struct ApplyOptions {
    bool verify_source = true;                    // refuse a base that fails the patch's check
    std::function<bool(float fraction)> progress; // returning false cancels
};

/// Writes `output` (a temporary file beside it, then a rename). `source` is never written.
/// A failed check leaves no output.
Status Apply(const std::filesystem::path& patch, const std::filesystem::path& source,
             const std::filesystem::path& output, const ApplyOptions& options = {});

/// "1F1C08FB".
std::string CrcText(std::uint32_t crc);

} // namespace den::patching
