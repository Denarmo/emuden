// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Portable names (docs/sync.md, section 2): a remote file is named by what does not depend on
// where a game lives on a device, the system and the ROM's file name. The library entries map
// those names to the local files, which are named by library id.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "den/library/library.h"

namespace den::sync {

inline constexpr const char* SAVES = "saves";
inline constexpr const char* STATES = "states";
inline constexpr const char* CONFIG = "config";
inline constexpr const char* GAMES = "games";
inline constexpr const char* BIOS = "bios";

/// The Google Drive provider: it never receives anything of a PC console, in any form.
inline constexpr const char* GDRIVE_PROVIDER = "emuden.sync.gdrive";

/// A game of the library as sync sees it.
struct GameRef {
    std::string id, system;
    std::string stem;          // the ROM's file name without its extension, as on disk
    std::filesystem::path file; // the file the library starts (a multi-file game's index)
};

/// The library's games by portable name. Games of `never` systems (PC consoles) are left out.
class GameIndex {
public:
    GameIndex() = default;
    GameIndex(const std::vector<library::Entry>& entries, std::set<std::string> never);

    /// The game of `system` whose stem matches, ignoring case; the first one when two share it.
    const GameRef* Find(const std::string& system, std::string_view stem) const;
    const GameRef* ById(const std::string& id) const;
    /// The game whose stem the file name starts with ("Game.srm", "Game.slot1.state"): the
    /// longest such stem followed by a dot.
    const GameRef* ByFileName(const std::string& system, std::string_view name) const;
    const std::vector<GameRef>& Games() const {
        return games_;
    }
    bool Never(const std::string& system) const {
        return never_.contains(system);
    }
    /// "system/stem" of games that share a stem with an earlier one (they do not sync).
    const std::vector<std::string>& Duplicates() const {
        return duplicates_;
    }

private:
    std::vector<GameRef> games_;
    std::set<std::string> never_;
    std::vector<std::string> duplicates_;
};

/// A name safe as a file name on every device, as the player names a game's files.
std::string SafeFileName(const std::string& name);

/// The category of a portable path ("saves/gba/Game.srm" gives "saves"), "" when unknown.
std::string CategoryOf(std::string_view path);
/// Its system: the second part of saves, states, games and config/games paths, else "".
std::string SystemOf(std::string_view path);
/// A relative path with '/', no empty, "." or ".." parts and nothing a file system refuses.
bool IsPortablePath(std::string_view path);
/// Whether the path belongs to one of `pc` systems, or names one anywhere in it.
bool IsPcPath(std::string_view path, const std::set<std::string>& pc);

/// Where the local file of a remote portable path is, when this device has its game.
struct LocalTarget {
    std::filesystem::path file; // empty for config files, which are applied, not stored
    std::string game_id;
};
std::optional<LocalTarget> TargetOf(const std::string& path, const GameIndex& games,
                                    const std::filesystem::path& data_dir,
                                    const std::filesystem::path& games_folder);

} // namespace den::sync
