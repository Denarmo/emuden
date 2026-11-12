// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Turns file names and store titles into names fit to show.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace den::library {

struct ParsedName {
    /// The name to show: "The Legend of Zelda - Ocarina of Time (Disc 2)".
    std::string display;
    /// The same without the disc suffix; the discs of one game share it.
    std::string base;
    /// Disc number from a "(Disc 2)" or "(CD 2)" tag, 0 when there is none.
    int disc = 0;
};

/// Cleans a ROM file name or dump name: drops a known file extension, turns underscores into
/// spaces, removes region, revision, language and dump tags in () and [], moves a trailing
/// article to the front ("Legend of Zelda, The" -> "The Legend of Zelda") and tidies spaces.
/// A name that would come out empty is returned as it was, trimmed.
ParsedName ParseGameName(std::string_view raw);

/// What the file names of one game's discs have in common: the name up to its disc tag,
/// "Final Quest (USA)" for "Final Quest (USA) (Disc 2) (Rev 1).cue". Region and language tags
/// before the disc tag stay, so two regions of a game are two names. The whole name, without
/// its extension, when it has no disc tag.
std::string NameBeforeDisc(std::string_view raw);

/// ParseGameName(raw).display
std::string CleanDisplayName(std::string_view raw);

/// Cleans a title that is already a title (Steam, game metadata): drops trademark signs and
/// tidies spaces. Text in brackets is kept.
std::string CleanStoreTitle(std::string_view title);

/// A key that is equal for names that mean the same game: the cleaned base name in lower case
/// with accents folded and everything but letters and digits removed.
std::string MatchKey(std::string_view raw);

/// The regions a ROM name is tagged with, by their usual names: "(USA, Europe)" gives USA and
/// Europe, "(E)" Europe. Tags that hold anything else ("(Rev 1)", "(En,Fr,De)") are not regions.
std::vector<std::string> RegionTags(std::string_view rom_name);

/// Europe, USA, Japan.
std::vector<std::string> DefaultRegionOrder();

/// How early a ROM's regions come in `order`, lower is better. World counts as the first, a
/// European country as Europe; a name without a region comes last.
std::size_t RegionRank(const std::vector<std::string>& tags, const std::vector<std::string>& order);

} // namespace den::library
