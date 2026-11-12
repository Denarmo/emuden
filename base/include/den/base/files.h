// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"

namespace den {

// All strings in EmuDen are UTF-8.
std::filesystem::path PathFromUtf8(std::string_view text);
std::string PathToUtf8(const std::filesystem::path& path);

std::string ToLower(std::string_view text);
bool EqualsNoCase(std::string_view a, std::string_view b);
std::string Trim(std::string_view text);

std::optional<std::string> ReadTextFile(const std::filesystem::path& path);
std::optional<std::vector<std::uint8_t>> ReadBinaryFile(const std::filesystem::path& path);
/// Writes to a temporary file next to `path`, then renames it over `path`.
bool WriteFileAtomic(const std::filesystem::path& path, std::string_view data);

constexpr std::uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
std::uint64_t Fnv1a(std::uint64_t hash, const void* data, std::size_t size);
std::uint32_t Crc32(const void* data, std::size_t size);
/// FNV-1a of a file's contents, nullopt when it can't be read.
std::optional<std::uint64_t> HashFile(const std::filesystem::path& path);

/// Environment variable as UTF-8, empty when unset.
std::string GetEnv(const char* name);

bool IsRegularFile(const std::filesystem::path& path);
bool IsDirectory(const std::filesystem::path& path);
/// Directory entries, empty on any error. Never throws.
std::vector<std::filesystem::path> ListDirectory(const std::filesystem::path& dir);

} // namespace den
