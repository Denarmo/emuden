// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A game's cheats: kept per game under the player's data folder, imported from RetroArch's
// .cht files, and handed to the core (retro_cheat_set) or the engine (set_cheats) as written.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"

namespace den::player {

struct Cheat {
    std::string name;
    /// As the core takes it: Game Genie, Action Replay, GameShark or raw codes; several codes
    /// joined by '+'.
    std::string code;
    bool enabled = false;

    bool operator==(const Cheat&) const = default;
};

/// <data>/cheats/<system>/<game id>.json, next to the game's saves and states.
std::filesystem::path CheatFile(const std::filesystem::path& data_dir, const std::string& system,
                                const std::string& game_id);
/// The cheats kept in `file`; none when it does not exist.
Result<std::vector<Cheat>> ReadCheats(const std::filesystem::path& file);
/// Writes the list; an empty one removes the file.
Status WriteCheats(const std::filesystem::path& file, const std::vector<Cheat>& cheats);
/// The cheats of a game as it starts: those kept in `file`, or for a game that has none yet
/// those of a .cht beside `content` (kept in `file` from then on).
std::vector<Cheat> LoadGameCheats(const std::filesystem::path& file, const std::filesystem::path& content);

/// A RetroArch cheat file: `cheats = N`, then `cheatK_desc`, `cheatK_code` and
/// `cheatK_enable` per cheat. Cheats without a code (RetroArch's memory search ones) are left out.
Result<std::vector<Cheat>> ParseCht(std::string_view text);
Result<std::vector<Cheat>> ReadChtFile(const std::filesystem::path& file);
/// `<folder>/<rom stem>.cht` beside the ROM, when there is one; empty otherwise.
std::filesystem::path ChtBeside(const std::filesystem::path& content);

/// The code tidied for the core: no spaces around '+', lines joined by '+'.
std::string CleanCode(std::string_view code);
/// Why a cheat cannot be kept (an empty code); empty when it can.
std::string CheatProblem(const Cheat& cheat);
/// Every cheat's code cleaned and an empty name made its code; fails on the first that
/// CheatProblem refuses.
Status TidyCheats(std::vector<Cheat>& cheats);
/// `imported` added to `cheats`, leaving out the codes already there; the count added.
int MergeCheats(std::vector<Cheat>& cheats, const std::vector<Cheat>& imported);

} // namespace den::player
