// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The files of Artwork's download cache, shared by its parts.

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace den::library::artwork_cache {

/// A lookup that found nothing is not repeated for this long.
inline constexpr auto MISS_LIFETIME = std::chrono::hours(24 * 14);

/// A name that is safe as a file name everywhere, unique through a hash of the original.
std::string CacheFileName(std::string_view name);
bool IsFresh(const std::filesystem::path& file, std::chrono::hours lifetime);
/// ".png" or ".jpg" for image data, nullptr for anything else.
const char* ImageExtension(std::string_view data);
std::filesystem::path WithSuffix(std::filesystem::path stem, const char* suffix);
/// Writes an empty marker file.
void Touch(const std::filesystem::path& file);
/// The image `file_stem` plus ".png" or ".jpg", empty when neither is there.
std::filesystem::path FindCached(const std::filesystem::path& file_stem);

} // namespace den::library::artwork_cache
