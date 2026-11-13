// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cover art for every library entry. Local art wins (an image next to a ROM, the icon a
// Switch 2 game carries); otherwise the cover is downloaded once from the libretro thumbnail
// collection and kept on disk. A game the collection does not know by name (a hack) gets its
// own art from the hack sources (hack_covers.h), else the box of the game it was made from
// with a "HACK" tag in its corner. When nothing is found a placeholder is drawn.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "den/library/hack_covers.h"
#include "den/library/library.h"
#include "den/library/systems.h"

namespace den::library {

/// The box art names of one system in the libretro thumbnail collection (without ".png"), with
/// the match key of each (see MatchKey) worked out once.
struct ThumbnailIndex {
    std::vector<std::string> names;
    std::vector<std::string> keys;
};
ThumbnailIndex BuildThumbnailIndex(std::vector<std::string> names);

/// Picks the box art for a ROM. `original_name` is the ROM's file name without extension. The
/// exact dump name wins, then the same game in the ROM's region, then in the usual region
/// order; a long name that is a letter or two off still matches unless it differs in a number
/// (a sequel). "" when no name fits.
std::string MatchThumbnail(const ThumbnailIndex& index, std::string_view original_name);

/// For a ROM MatchThumbnail finds nothing for (a hack, a translation): the box art of the game
/// it was made from. That is a game of the same series (the name's first word) whose other
/// words the ROM's file name or its header title all name: "Pokemon - Lazarus" with the header
/// "POKEMON EMER" is "Pokemon - Emerald Version". "" when no game fits.
std::string MatchBaseGame(const ThumbnailIndex& index, std::string_view original_name,
                          std::string_view header_title);

/// The names in an Apache/nginx directory listing of a Named_Boxarts folder.
std::vector<std::string> ParseThumbnailIndex(std::string_view html);

/// The name of a box in the collection's file names (some characters become '_').
std::string ThumbnailFileName(std::string_view name);

std::string UrlEncodeSegment(std::string_view segment);
std::string UrlDecode(std::string_view text);

/// Draws a 200x300 placeholder cover (a colour per system, the title's initials) as a PNG.
bool WritePlaceholderCover(const std::filesystem::path& file, std::string_view title,
                           std::string_view system_id);

/// A copy of `image` (at most 512 pixels a side) with a "HACK" tag in its top left corner, as
/// a PNG: the box of the original game on a hack. False when the image cannot be read.
bool WriteHackMarkedCover(const std::filesystem::path& image, const std::filesystem::path& out);

enum class ArtworkSource {
    Local,       // the entry already had a cover on this machine
    Cache,       // downloaded in an earlier run
    Downloaded,  // downloaded just now
    HackArt,     // a hack's own art, from a hack source
    BaseGame,    // a hack with the box of the game it was made from, tagged
    Placeholder, // nothing was found: a generated image
};

struct ArtworkResult {
    std::filesystem::path path;
    ArtworkSource source = ArtworkSource::Placeholder;
};

/// Resolves covers. Resolve and Choices block on the disk and the network: call them from a
/// job. Safe from several threads at once.
class Artwork {
public:
    /// `cache_dir` is created on demand. With an empty `fetcher` nothing is downloaded. The
    /// hack sources use `header_fetcher` (`fetcher` without headers when it is empty), with
    /// `interval_ms` between two requests to one host.
    Artwork(std::filesystem::path cache_dir, HttpFetcher fetcher, HeaderFetcher header_fetcher = {},
            int interval_ms = 0);

    void SetHackOptions(HackCoverOptions options);

    /// Every entry ends with a path, a placeholder at worst. A lookup that found nothing is
    /// not repeated for two weeks (a hack's own art: one week); a failed connection is
    /// retried on the next run. Nothing new is fetched once `stopping` returns true.
    ArtworkResult Resolve(const Entry& entry, const SystemDef* system,
                          const std::function<bool()>& stopping = {});

    /// For the user to pick from: what the hack sources have for this game, downloaded, and
    /// the tagged box of the game it was made from. Asks again whatever was cached as missing.
    std::vector<CoverChoice> Choices(const Entry& entry, const SystemDef* system,
                                     const std::function<bool()>& stopping = {});

private:
    const ThumbnailIndex& IndexFor(const SystemDef& system);
    bool Download(const std::string& url, const std::filesystem::path& file_stem,
                  std::filesystem::path& out, bool& answered, bool polite = false);
    bool ResolveHack(const Entry& entry, const SystemDef& system, const ThumbnailIndex& index,
                     const std::string& original, const std::function<bool()>& stopping,
                     ArtworkResult& result);
    HackQuery QueryFor(const Entry& entry, const SystemDef& system, const ThumbnailIndex& index,
                       const std::string& original);
    HackCoverOptions HackOptions();

    std::filesystem::path cache_dir_;
    HttpFetcher fetcher_;
    HackSources sources_;
    std::mutex index_mutex_;
    std::map<std::string, ThumbnailIndex> indexes_;
    std::mutex options_mutex_;
    HackCoverOptions hack_options_;
};

} // namespace den::library
