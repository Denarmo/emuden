// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the library facade needs besides its own state: the clock, the platform's HTTP client,
// the hack cover settings, and the covers the user picked (<data>/library/covers/<system>/<rom name>).

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "den/library/hack_covers.h"
#include "den/library/library.h"

namespace den::library {

std::int64_t UnixNow();
/// When the file was last written, as unix seconds; 0 when it cannot be told.
std::int64_t WrittenUnix(const std::filesystem::path& file);

HttpAnswer PlatformGet(const std::string& url);
HttpAnswer PlatformGetWithHeaders(const std::string& url, const HttpHeaders& headers);

/// "library.hack_covers.*"; the defaults without settings.
HackCoverOptions ReadHackOptions(const store::Settings* settings);

/// Where the scanner looks first for a game's cover, without the extension.
std::filesystem::path OwnCoverStem(const std::filesystem::path& library_dir, const Entry& entry);
void RemoveOwnCover(const std::filesystem::path& stem);
/// Copies a PNG or JPEG to `stem` plus its extension, replacing an earlier pick.
Status WriteOwnCover(const std::filesystem::path& stem, const std::string& image_path,
                     std::filesystem::path& file);

} // namespace den::library
