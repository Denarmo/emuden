// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Covers for ROM hacks: games the console's box art collection does not know by name. Their
// own art is looked for in sources that allow automated access (see docs/covers.md): community
// collections on GitHub and the hack entries of the libretro collection without an account,
// RetroAchievements and SteamGridDB when the user has entered a key.

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "den/library/library.h"

namespace den::library {

struct ThumbnailIndex;

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;
/// A GET with request headers (an API key, the client's name).
using HeaderFetcher = std::function<HttpAnswer(const std::string& url, const HttpHeaders& headers)>;

/// The settings of the hack cover lookup, read by the library before every scan.
struct HackCoverOptions {
    bool online = true;                   // "library.hack_covers.online"
    std::vector<std::string> collections; // GitHub "owner/repo"; "library.hack_covers.collections"
    std::string retroachievements_key;    // "library.hack_covers.retroachievements_key"
    std::string steamgriddb_key;          // "library.hack_covers.steamgriddb_key"
};
std::vector<std::string> DefaultHackCollections();

/// Whether a file name says it is a hack: "(Hack)", "[Hack by ...]", "(Romhack)".
bool HasHackTag(std::string_view rom_name);

struct NameMatch {
    int score = 0;     // 0 no match .. 100 the same name
    bool sure = false; // good enough to take without asking
};
/// Compares a ROM's name with a title at a source. Accents, case, punctuation, tags in
/// brackets, "~Hack~" prefixes and words like "Version" are ignored: "Pokemon - Heart & Soul"
/// is "Pokémon Heart and Soul (v1.2)". A title with more words is sure only when the ROM's
/// header names them ("Pokemon - Emerald Seaglass" for "Pokemon - Seaglass" with the header
/// "POKEMON EMER"); one with fewer words never is (the game the hack was made from).
NameMatch MatchHackName(std::string_view rom_name, std::string_view title,
                        std::string_view header_title = {});

struct CoverCandidate {
    std::string source; // "collection", "libretro", "retroachievements", "steamgriddb", "base"
    std::string title;  // the name at the source
    std::string url;    // the image
    int score = 0;
    bool sure = false;
};

/// What the sources are asked about one game.
struct HackQuery {
    std::string name;   // the ROM's file name without extension
    std::string system; // system id
    std::string header; // the title in the ROM's header, if any
    const ThumbnailIndex* libretro = nullptr;
    std::string libretro_folder; // the system's folder in the libretro collection
};

/// Asks the sources. Blocking: call it from a job. Safe from several threads; requests to
/// one host are kept `interval_ms` apart, listings are cached on disk for a week or more.
class HackSources {
public:
    HackSources(std::filesystem::path cache_dir, HeaderFetcher fetcher, int interval_ms);

    /// The covers that may be this game's, best first. `answered` becomes false when a source
    /// could not be reached (the lookup is then worth repeating soon).
    std::vector<CoverCandidate> Find(const HackQuery& query, const HackCoverOptions& options,
                                     bool& answered, const std::function<bool()>& stopping = {});

    HttpAnswer Get(const std::string& url, const HttpHeaders& headers = {});

private:
    void FromCollection(const std::string& repo, const HackQuery& query,
                        std::vector<CoverCandidate>& out, bool& answered);
    void FromLibretro(const HackQuery& query, std::vector<CoverCandidate>& out);
    void FromRetroAchievements(const HackQuery& query, const std::string& key,
                               std::vector<CoverCandidate>& out, bool& answered);
    void FromSteamGridDb(const HackQuery& query, const std::string& key,
                         std::vector<CoverCandidate>& out, bool& answered);
    /// A listing kept on disk for `days`; "" when it cannot be had.
    std::string Listing(const std::string& cache_name, const std::string& url,
                        const HttpHeaders& headers, int days, bool& answered);

    std::filesystem::path cache_dir_;
    HeaderFetcher fetcher_;
    int interval_ms_;
    std::mutex mutex_;
    std::map<std::string, std::int64_t> last_request_ms_; // by host
    std::map<std::string, std::string> listings_;
};

} // namespace den::library
