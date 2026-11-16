// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Path keys, file names safe on every device, and percent-decoding.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace den {

/// A path for comparing: normalised, ASCII lower case, '/' between parts and no '/' at the end
/// (a root keeps its own). Saved in the hack checksum cache, so its form must not change.
std::string PathKey(const std::filesystem::path& path);
/// Whether `file`'s key is below `folder_key` (a PathKey), not the folder itself.
bool IsUnderKey(const std::filesystem::path& file, std::string_view folder_key);

/// A control character or one Windows refuses in a name: < > : " / \ | ? *.
/// `allow_slash` keeps '/' for relative paths with '/' between their parts.
bool IsRefusedNameChar(char c, bool allow_slash = false);

struct FileNameRules {
    char replacement = '_';            // what a refused character becomes; '\0' drops it
    bool allow_slash = false;          // keep '/'
    bool collapse_spaces = false;      // runs of spaces become one, none at the start
    std::string_view trim_front = "";  // characters taken off the start
    std::size_t max_bytes = 120;       // 0: no limit
    bool byte_cut = true;              // cut at the byte after trimming the end (save and state
                                       // names have always been made so); false cuts at a
                                       // character and trims after
    std::string_view fallback = "game";
};

/// A name safe as a file name on every device; the end never has '.' or ' '. The default rules
/// name a game's saves, states and synced files, so they must not change.
std::string SafeFileName(std::string_view name, const FileNameRules& rules = {});

/// "%20" -> " "; a '%' not followed by two hex digits stays.
std::string PercentDecode(std::string_view text);

} // namespace den
