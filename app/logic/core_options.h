// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The core options a core last described, noted per core in <data>/cache/core_options, and
// the settings keys they are kept under (game_prefs.h).

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace den::app {

/// A core option as the core last described it (noted whenever a game runs).
struct KnownOption {
    std::string key, label, fallback;
    std::vector<std::string> values;
};

/// The noted options as the cache file holds them.
std::string WriteKnownOptions(const std::vector<KnownOption>& options);
/// Those of a cache file with a key and values; nothing for a file that does not parse.
std::vector<KnownOption> ParseKnownOptions(std::string_view text);

/// The cache file's name for a core: letters and digits, '_' for the rest, and ".json".
std::string KnownOptionsFileName(const std::string& core_id);
/// The settings key of a core option in a game's or console's layer.
std::string OptionKey(const std::string& core_id, const std::string& option_key);
/// The key the player keeps a core option under in the global settings.
std::string GlobalOptionKey(const std::string& core_name, const std::string& option_key);

} // namespace den::app
