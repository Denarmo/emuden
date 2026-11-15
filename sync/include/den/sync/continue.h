// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Pick up where you left off on any device" (docs/sync.md, section 3.4): the games played
// last on any of the user's devices, from this device's own play and the other devices' play
// time files, with the picture of the moment each one was left.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "den/library/library.h"

namespace den::sync {

struct ContinueItem {
    std::string key;          // "<system>/<rom stem>", as in the play time files
    std::string system, stem;
    std::string game_id;      // this device's entry; "" when it does not have the game
    std::string device;       // the device that played it last; "" = this one
    /// When it was left: the resume state's time when this device has it from that
    /// session, else the start of the last session.
    std::int64_t time = 0;
    std::filesystem::path screenshot; // the resume state's picture here; empty when none
    bool resume = false;              // a resume state is here (a newer one may come down)
};

struct ContinueRequest {
    std::filesystem::path data_dir;
    std::string own_id;                                  // this device's play file is not read
    std::vector<library::Entry> entries;                 // the library
    std::map<std::string, library::PlayTotals> own_play; // by entry id
    std::set<std::string> never;                         // PC consoles: only this device's own play
    std::size_t limit = 4;
};

/// The games last played on any device, newest first, at most `limit`.
std::vector<ContinueItem> ContinueGames(const ContinueRequest& request);

/// The picture saved with a game's resume state: <data>/states/<system>/<id>.resume.png.
std::filesystem::path ResumeScreenshot(const std::filesystem::path& data_dir, const std::string& system,
                                       const std::string& game_id);
std::filesystem::path ResumeState(const std::filesystem::path& data_dir, const std::string& system,
                                  const std::string& game_id);

/// "Just now", "5 min ago", "3 h ago", "Yesterday", "4 days ago", "3 weeks ago", "Long ago".
std::string AgoText(std::int64_t then, std::int64_t now);

} // namespace den::sync
