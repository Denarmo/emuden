// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the launcher screens share about games: which are shown, and their records as text.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "den/library/library.h"

namespace den::app {

/// Hidden games stay in the library and are left out of every screen ("library.hidden").
bool IsHidden(const std::string& id);
void SetHidden(const std::string& id, bool hidden);
int HiddenCount();
void ShowAllHidden();
std::vector<library::Entry> WithoutHidden(std::vector<library::Entry> games);

/// Moves whenever what the game screens show may have changed (library, plugins, hidden).
std::uint64_t GamesRevision();

/// "Game Boy Advance" for "gba": from the plugins, else the library, else the id.
std::string SystemName(const std::string& system);
std::string SystemShortName(const std::string& system);

std::string PlayTimeText(std::int64_t seconds);     // "6 min", "2 h 5 min"
std::string LastPlayedText(std::int64_t unix_time); // "Today", "Yesterday", "3 days ago", a date
std::string SizeText(std::uint64_t bytes);
/// "Game Boy · Played 6 min · Last played Today".
std::string RecordsLine(const library::Entry& game);
std::string ClockText(); // "14:13", local time

/// Whether every word of `query` is found in the game's name or its console's.
bool Matches(const library::Entry& game, const std::string& query);

} // namespace den::app
