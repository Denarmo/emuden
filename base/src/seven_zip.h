// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include "den/base/archive.h"

namespace den {

/// The largest solid block that is unpacked (it is held in memory).
constexpr std::uint64_t SEVEN_ZIP_MAX_BLOCK = 1ull << 30;

/// A 7z archive read with the LZMA SDK's decoder. Nothing when it cannot be listed.
std::unique_ptr<ArchiveReader> OpenSevenZip(const std::filesystem::path& archive);

} // namespace den
