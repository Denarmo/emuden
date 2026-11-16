// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sources of hack covers. Each one is asked through its documented, public interface;
// sites whose terms or robots.txt forbid automated access are not here (docs/covers.md).

#include <algorithm>
#include <chrono>
#include <thread>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/library/artwork.h"
#include "den/library/hack_covers.h"
#include "den/library/names.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view CLIENT = "emuden-covers";
constexpr std::string_view THUMBNAIL_HOST = "https://thumbnails.libretro.com/";
constexpr int MIN_SCORE = 30;
constexpr std::size_t MAX_CANDIDATES = 12;

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string HostOf(const std::string& url) {
    const std::size_t start = url.find("://");
    const std::size_t from = start == std::string::npos ? 0 : start + 3;
    return url.substr(from, url.find('/', from) - from);
}

bool IsFresh(const fs::path& file, int days) {
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    return !ec && fs::file_time_type::clock::now() - written < std::chrono::hours(24 * days);
}

std::string EncodePath(std::string_view path) {
    std::string out;
    std::size_t at = 0;
    while (at <= path.size()) {
        const std::size_t end = std::min(path.find('/', at), path.size());
        out += (out.empty() ? "" : "/") + UrlEncodeSegment(path.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

void Add(std::vector<CoverCandidate>& out, std::string_view source, std::string title,
         std::string url, NameMatch match, int adjust = 0) {
    out.push_back({std::string(source), std::move(title), std::move(url), match.score + adjust, match.sure});
}

} // namespace

HackSources::HackSources(fs::path cache_dir, HeaderFetcher fetcher, int interval_ms)
    : cache_dir_(std::move(cache_dir)), fetcher_(std::move(fetcher)), interval_ms_(interval_ms) {}

HttpAnswer HackSources::Get(const std::string& url, const HttpHeaders& headers) {
    if (!fetcher_) {
        return {};
    }
    if (interval_ms_ > 0) {
        // One request at a time per host, spaced out: the next caller takes the next slot.
        std::int64_t wait = 0;
        {
            const std::lock_guard lock(mutex_);
            std::int64_t& slot = last_request_ms_[HostOf(url)];
            const std::int64_t now = NowMs();
            slot = std::max(now, slot + interval_ms_);
            wait = slot - now;
        }
        if (wait > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        }
    }
    HttpHeaders all = headers;
    all.emplace_back("User-Agent", std::string(CLIENT));
    return fetcher_(url, all);
}

std::string HackSources::Listing(const std::string& cache_name, const std::string& url,
                                 const HttpHeaders& headers, int days, bool& answered) {
    {
        const std::lock_guard lock(mutex_);
        if (const auto found = listings_.find(cache_name); found != listings_.end()) {
            return found->second;
        }
    }
    const fs::path file = cache_dir_ / "index" / PathFromUtf8(cache_name);
    std::string text;
    if (IsFresh(file, days)) {
        text = ReadTextFile(file).value_or(std::string());
    }
    if (text.empty()) {
        const HttpAnswer response = Get(url, headers);
        if (response.status == 200 && ParseJson(response.body)) {
            text = response.body;
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            WriteFileAtomic(file, text);
        } else {
            answered = answered && response.status != 0;
            // Offline or refused: an old copy is better than nothing.
            text = ReadTextFile(file).value_or(std::string());
        }
    }
    const std::lock_guard lock(mutex_);
    return listings_[cache_name] = std::move(text);
}

// A repository of cover images, in folders named like consoles: one request for its file
// list (GitHub's tree interface), kept for a week.
void HackSources::FromCollection(const std::string& repo, const HackQuery& query,
                                 std::vector<CoverCandidate>& out, bool& answered) {
    std::string cache_name = "github-" + repo + ".json";
    std::replace(cache_name.begin(), cache_name.end(), '/', '_');
    const std::string text =
        Listing(cache_name, "https://api.github.com/repos/" + repo + "/git/trees/HEAD?recursive=1",
                {{"Accept", "application/vnd.github+json"}}, 7, answered);
    const auto json = ParseJson(text);
    const JsonValue* tree = json ? json->Find("tree") : nullptr;
    if (tree == nullptr) {
        return;
    }
    for (const JsonValue& item : tree->items) {
        const std::string path = item.String("path");
        const fs::path file = PathFromUtf8(path);
        const std::string extension = ToLower(PathToUtf8(file.extension()));
        if (extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
            continue;
        }
        const std::string title = PathToUtf8(file.stem());
        const NameMatch match = MatchHackName(query.name, title, query.header);
        if (match.score < MIN_SCORE) {
            continue;
        }
        // The main picture before its "(alt)" variants, the game's own console first.
        const bool same_console = path.starts_with(query.system + "/");
        const int adjust = (same_console ? 3 : 0) - (title.find('(') != std::string::npos ? 5 : 0);
        Add(out, "collection", title,
            "https://raw.githubusercontent.com/" + repo + "/HEAD/" + EncodePath(path), match, adjust);
    }
}

// Hacks the libretro collection has under another spelling ("Pokemon - Brown Version",
// "Pokemon - Glazed Version (Hack)").
void HackSources::FromLibretro(const HackQuery& query, std::vector<CoverCandidate>& out) {
    if (query.libretro == nullptr || query.libretro_folder.empty()) {
        return;
    }
    for (const std::string& name : query.libretro->names) {
        const NameMatch match = MatchHackName(query.name, name, query.header);
        if (match.score >= 60) {
            Add(out, "libretro", name,
                std::string(THUMBNAIL_HOST) + UrlEncodeSegment(query.libretro_folder) +
                    "/Named_Boxarts/" + UrlEncodeSegment(name) + ".png",
                match, HasHackTag(name) ? 0 : -2);
        }
    }
}

// RetroAchievements lists hacks that have achievement sets ("~Hack~ Pokemon Brown"); the web
// API needs the user's key. The console's list is kept for a month, as its documentation asks.
void HackSources::FromRetroAchievements(const HackQuery& query, const std::string& key,
                                        std::vector<CoverCandidate>& out, bool& answered) {
    const int console = query.ra_console;
    if (console == 0) {
        return;
    }
    const std::string api = "https://retroachievements.org/API/";
    const std::string text = Listing("retroachievements-" + std::to_string(console) + ".json",
                                     api + "API_GetGameList.php?i=" + std::to_string(console) +
                                         "&y=" + UrlEncodeSegment(key),
                                     {}, 30, answered);
    const auto list = ParseJson(text);
    if (!list) {
        return;
    }
    struct Hit {
        NameMatch match;
        std::string title;
        int id;
    };
    std::vector<Hit> hits;
    for (const JsonValue& game : list->items) {
        const std::string title = game.String("Title");
        const JsonValue* id = game.Find("ID");
        const NameMatch match = MatchHackName(query.name, title, query.header);
        if (id != nullptr && match.score >= 60 && title.find("[Subset") == std::string::npos) {
            hits.push_back({match, title, static_cast<int>(id->number)});
        }
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit& a, const Hit& b) { return a.match.score > b.match.score; });
    hits.resize(std::min<std::size_t>(hits.size(), 2));
    for (const Hit& hit : hits) {
        const HttpAnswer response =
            Get(api + "API_GetGame.php?i=" + std::to_string(hit.id) + "&y=" + UrlEncodeSegment(key));
        answered = answered && response.status != 0;
        const auto game = response.status == 200 ? ParseJson(response.body) : std::nullopt;
        const std::string box = game ? game->String("ImageBoxArt") : std::string();
        // 000002.png is the site's "no image" picture.
        if (!box.empty() && !box.ends_with("/000002.png")) {
            Add(out, "retroachievements", hit.title, "https://media.retroachievements.org" + box,
                hit.match);
        }
    }
}

// SteamGridDB has fan-made grids for many hacks; its API needs the user's key.
void HackSources::FromSteamGridDb(const HackQuery& query, const std::string& key,
                                  std::vector<CoverCandidate>& out, bool& answered) {
    const std::string api = "https://www.steamgriddb.com/api/v2/";
    const HttpHeaders auth{{"Authorization", "Bearer " + key}};
    const HttpAnswer search =
        Get(api + "search/autocomplete/" + UrlEncodeSegment(ParseGameName(query.name).base), auth);
    answered = answered && search.status != 0;
    const auto found = search.status == 200 ? ParseJson(search.body) : std::nullopt;
    const JsonValue* games = found ? found->Find("data") : nullptr;
    if (games == nullptr) {
        return;
    }
    int asked = 0;
    for (const JsonValue& game : games->items) {
        const std::string title = game.String("name");
        const JsonValue* id = game.Find("id");
        const NameMatch match = MatchHackName(query.name, title, query.header);
        if (id == nullptr || match.score < 60 || asked >= 2) {
            continue;
        }
        ++asked;
        const HttpAnswer response =
            Get(api + "grids/game/" + std::to_string(static_cast<long long>(id->number)) +
                    "?dimensions=600x900,342x482,660x930&types=static",
                auth);
        answered = answered && response.status != 0;
        const auto grids = response.status == 200 ? ParseJson(response.body) : std::nullopt;
        const JsonValue* data = grids ? grids->Find("data") : nullptr;
        int taken = 0;
        for (const JsonValue& grid : data != nullptr ? data->items : std::vector<JsonValue>()) {
            const std::string url = grid.String("url");
            const std::string lower = ToLower(url);
            if (taken < 3 && (lower.ends_with(".png") || lower.ends_with(".jpg"))) {
                Add(out, "steamgriddb", title, url, match, -taken++);
            }
        }
    }
}

std::vector<CoverCandidate> HackSources::Find(const HackQuery& query, const HackCoverOptions& options,
                                              bool& answered, const std::function<bool()>& stopping) {
    std::vector<CoverCandidate> out;
    answered = true;
    if (!fetcher_ || !options.online) {
        return out;
    }
    const auto stopped = [&] { return stopping && stopping(); };
    for (const std::string& repo : options.collections) {
        if (!stopped() && !repo.empty()) {
            FromCollection(repo, query, out, answered);
        }
    }
    FromLibretro(query, out);
    const auto sure = [&] {
        return std::any_of(out.begin(), out.end(), [](const CoverCandidate& c) { return c.sure; });
    };
    // Sources with a key are asked only when the free ones are not sure, and one at a time.
    if (!options.retroachievements_key.empty() && !sure() && !stopped()) {
        FromRetroAchievements(query, options.retroachievements_key, out, answered);
    }
    if (!options.steamgriddb_key.empty() && !sure() && !stopped()) {
        FromSteamGridDb(query, options.steamgriddb_key, out, answered);
    }
    std::stable_sort(out.begin(), out.end(), [](const CoverCandidate& a, const CoverCandidate& b) {
        return a.sure != b.sure ? a.sure : a.score > b.score;
    });
    out.resize(std::min(out.size(), MAX_CANDIDATES));
    return out;
}

} // namespace den::library
