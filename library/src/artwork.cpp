// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <chrono>

#include "den/base/files.h"
#include "den/library/artwork.h"
#include "den/library/content.h"
#include "den/library/launchbox_covers.h"
#include "den/library/names.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view THUMBNAIL_HOST = "https://thumbnails.libretro.com/";
// A lookup that found nothing is not repeated for this long.
constexpr auto MISS_LIFETIME = std::chrono::hours(24 * 14);
constexpr auto INDEX_LIFETIME = std::chrono::hours(24 * 30);
// A hack's own art is asked for again sooner: collections of fan art grow.
constexpr auto HACK_MISS_LIFETIME = std::chrono::hours(24 * 7);
constexpr std::size_t MAX_CHOICES = 8;

// A name that is safe as a file name everywhere, unique through a hash of the original.
std::string CacheFileName(std::string_view name) {
    std::string out;
    for (const char c : name.substr(0, 80)) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '.' || c == '(' ||
                           c == ')' || c == ',';
        out.push_back(plain ? c : '_');
    }
    static constexpr char HEX[] = "0123456789abcdef";
    std::uint64_t hash = Fnv1a(FNV_OFFSET, name.data(), name.size());
    out.push_back('-');
    for (int i = 0; i < 8; ++i) {
        out.push_back(HEX[hash & 0xF]);
        hash >>= 4;
    }
    return out;
}

bool IsFresh(const fs::path& file, std::chrono::hours lifetime) {
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    return !ec && fs::file_time_type::clock::now() - written < lifetime;
}

const char* ImageExtension(std::string_view data) {
    if (data.size() >= 8 && data.starts_with("\x89PNG")) {
        return ".png";
    }
    if (data.size() >= 3 && data.starts_with("\xFF\xD8\xFF")) {
        return ".jpg";
    }
    return nullptr;
}

fs::path WithSuffix(fs::path stem, const char* suffix) {
    stem += suffix;
    return stem;
}

void Touch(const fs::path& file) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    WriteFileAtomic(file, "");
}

HeaderFetcher WithoutHeaders(HttpFetcher fetcher) {
    if (!fetcher) {
        return {};
    }
    return [fetcher = std::move(fetcher)](const std::string& url, const HttpHeaders&) { return fetcher(url); };
}

fs::path FindCached(const fs::path& file_stem) {
    for (const char* extension : {".png", ".jpg"}) {
        fs::path file = file_stem;
        file += extension;
        if (IsRegularFile(file)) {
            return file;
        }
    }
    return {};
}

// The short id a game file holds as its whole text ("620"), or "" when it holds something else.
std::string GameFileId(const fs::path& file) {
    std::error_code ec;
    if (fs::file_size(file, ec) > 64 || ec) {
        return {};
    }
    const auto text = ReadTextFile(file);
    if (!text) {
        return {};
    }
    std::string id = *text;
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back()))) {
        id.pop_back();
    }
    const bool plain = !id.empty() && id.size() <= 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
    return plain ? id : std::string();
}

std::string WithId(std::string url, const std::string& id) {
    for (std::size_t at = url.find("{id}"); at != std::string::npos; at = url.find("{id}", at + id.size())) {
        url.replace(at, 4, id);
    }
    return url;
}

} // namespace

Artwork::Artwork(fs::path cache_dir, HttpFetcher fetcher, HeaderFetcher header_fetcher, int interval_ms)
    : cache_dir_(std::move(cache_dir)), fetcher_(std::move(fetcher)),
      sources_(cache_dir_ / "hacks", header_fetcher ? std::move(header_fetcher) : WithoutHeaders(fetcher_),
               interval_ms),
      launchbox_(std::make_unique<LaunchBoxCovers>(cache_dir_ / "launchbox", fetcher_)) {
    hack_options_.collections = DefaultHackCollections();
}

Artwork::~Artwork() = default;

// The LaunchBox Games Database's front cover, matched by name on the console's platform.
bool Artwork::ResolveLaunchBox(const Entry& entry, const SystemDef& system, const std::function<bool()>& stopping,
                               ArtworkResult& result, std::string* title) {
    if (system.launchbox.empty() && system.libretro_thumbnails.empty()) {
        return false;
    }
    const std::string original = entry.original_name.empty() ? entry.name : entry.original_name;
    const fs::path stem =
        cache_dir_ / "launchbox-covers" / PathFromUtf8(system.id) / PathFromUtf8(CacheFileName(original));
    if (fs::path cached = FindCached(stem); !cached.empty() && title == nullptr) {
        result = {std::move(cached), ArtworkSource::Cache};
        return true;
    }
    const fs::path miss = WithSuffix(stem, ".miss");
    if (!fetcher_ || (stopping && stopping()) || (title == nullptr && IsFresh(miss, MISS_LIFETIME))) {
        return false;
    }
    std::vector<std::string> titles{entry.name};
    if (std::string base = ParseGameName(original).base; base != entry.name) {
        titles.push_back(std::move(base));
    }
    bool answered = true;
    const auto cover = launchbox_->Find(system, titles, RegionTags(original), answered);
    if (!cover) {
        if (answered) {
            Touch(miss);
        }
        return false;
    }
    if (title != nullptr) {
        *title = cover->title;
    }
    if (fs::path cached = FindCached(stem); !cached.empty()) {
        result = {std::move(cached), ArtworkSource::Cache};
        return true;
    }
    bool reached = false;
    if (fs::path file; Download(cover->url, stem, file, reached)) {
        result = {std::move(file), ArtworkSource::Downloaded};
        return true;
    }
    return false;
}

GameFaces Artwork::Faces(const Entry& entry, const SystemDef* system, const std::function<bool()>& stopping,
                         const std::string& base_title) {
    GameFaces faces;
    if (system == nullptr || (system->launchbox.empty() && system->libretro_thumbnails.empty())) {
        return faces;
    }
    const std::string original = entry.original_name.empty() ? entry.name : entry.original_name;
    const fs::path stem =
        cache_dir_ / "launchbox-faces" / PathFromUtf8(system->id) / PathFromUtf8(CacheFileName(original));
    std::pair<const char*, std::string*> kinds[] = {{".back", &faces.back},
                                                    {".spine", &faces.spine},
                                                    {".cart", &faces.cart},
                                                    {".cart-back", &faces.cart_back},
                                                    {".disc", &faces.disc}};
    const auto cached = [&] {
        for (auto& [suffix, path] : kinds) {
            *path = PathToUtf8(FindCached(WithSuffix(stem, suffix)));
        }
    };
    // ".faces" marks a game whose faces were all fetched, ".faces.miss" one without any.
    const fs::path done = WithSuffix(stem, ".faces"), miss = WithSuffix(stem, ".faces.miss");
    const fs::path base_mark = WithSuffix(stem, ".faces.base");
    if (IsRegularFile(done)) {
        cached();
        faces.base_game = IsRegularFile(base_mark);
        return faces;
    }
    if (!fetcher_ || (stopping && stopping()) || IsFresh(miss, MISS_LIFETIME)) {
        return faces;
    }
    std::vector<std::string> titles{entry.name};
    if (std::string base = ParseGameName(original).base; base != entry.name) {
        titles.push_back(std::move(base));
    }
    bool answered = true;
    auto found = launchbox_->FindFaces(*system, titles, RegionTags(original), answered);
    if (!found || found->cart.empty()) {
        // A hack the database knows no cartridge of: the cartridge (and, knowing nothing at
        // all of it, the box) of the game it was made from.
        std::string base = base_title;
        if (base.empty() && !system->libretro_thumbnails.empty()) {
            const ThumbnailIndex& index = IndexFor(*system);
            if (!index.names.empty() && MatchThumbnail(index, original).empty()) {
                base = MatchBaseGame(index, original, QueryFor(entry, *system, index, original).header);
            }
        }
        auto from_base = base.empty() ? std::nullopt
                                      : launchbox_->FindFaces(*system, {ParseGameName(base).base, base},
                                                              RegionTags(original), answered);
        if (from_base && (!found || !from_base->cart.empty())) {
            if (found) {
                found->cart = std::move(from_base->cart);
                found->cart_back = std::move(from_base->cart_back);
            } else {
                found = std::move(from_base);
                found->disc.clear(); // the base game's disc names the wrong game
            }
            faces.base_game = true;
            Touch(base_mark);
        }
    }
    if (!found) {
        if (answered) {
            Touch(miss);
        }
        return faces;
    }
    const std::string* urls[] = {&found->back, &found->spine, &found->cart, &found->cart_back, &found->disc};
    bool complete = true;
    for (std::size_t i = 0; i < std::size(kinds); ++i) {
        const fs::path face = WithSuffix(stem, kinds[i].first);
        if (urls[i]->empty() || !FindCached(face).empty()) {
            continue;
        }
        if (stopping && stopping()) {
            complete = false;
            break;
        }
        bool reached = false;
        fs::path file;
        complete = Download(*urls[i], face, file, reached) && complete;
    }
    if (complete) {
        Touch(done);
    }
    cached();
    return faces;
}

void Artwork::SetHackOptions(HackCoverOptions options) {
    const std::lock_guard lock(options_mutex_);
    hack_options_ = std::move(options);
}

HackCoverOptions Artwork::HackOptions() {
    const std::lock_guard lock(options_mutex_);
    return hack_options_;
}

bool Artwork::Download(const std::string& url, const fs::path& file_stem, fs::path& out,
                       bool& answered, bool polite) {
    const HttpAnswer response = polite ? sources_.Get(url) : fetcher_(url);
    answered = answered || response.status != 0;
    const char* extension = response.status == 200 ? ImageExtension(response.body) : nullptr;
    if (!extension) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(file_stem.parent_path(), ec);
    fs::path file = file_stem;
    file += extension;
    if (!WriteFileAtomic(file, response.body)) {
        return false;
    }
    out = std::move(file);
    return true;
}

const ThumbnailIndex& Artwork::IndexFor(const SystemDef& system) {
    const std::lock_guard lock(index_mutex_);
    if (const auto found = indexes_.find(system.id); found != indexes_.end()) {
        return found->second;
    }
    ThumbnailIndex& index = indexes_[system.id];
    std::vector<std::string> names;
    const auto done = [&]() -> const ThumbnailIndex& {
        index = BuildThumbnailIndex(std::move(names));
        return index;
    };
    const fs::path file = cache_dir_ / "index" / PathFromUtf8(system.id + ".txt");
    const auto read_file = [&] {
        if (const auto text = ReadTextFile(file)) {
            std::size_t at = 0;
            while (at < text->size()) {
                const std::size_t end = std::min(text->find('\n', at), text->size());
                if (end > at) {
                    names.push_back(text->substr(at, end - at));
                }
                at = end + 1;
            }
        }
    };
    if (IsFresh(file, INDEX_LIFETIME) || !fetcher_) {
        read_file();
        return done();
    }
    const HttpAnswer response = fetcher_(std::string(THUMBNAIL_HOST) +
                                         UrlEncodeSegment(system.libretro_thumbnails) +
                                         "/Named_Boxarts/");
    if (response.status == 200) {
        names = ParseThumbnailIndex(response.body);
    }
    if (names.empty()) {
        // Offline or the listing changed shape: an old copy is better than nothing.
        read_file();
        return done();
    }
    std::string text;
    for (const std::string& name : names) {
        text += name + '\n';
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    WriteFileAtomic(file, text);
    return done();
}

HackQuery Artwork::QueryFor(const Entry& entry, const SystemDef& system, const ThumbnailIndex& index,
                            const std::string& original) {
    HackQuery query;
    query.name = original;
    query.system = system.id;
    query.header = entry.archive_member.empty() ? ReadRomHeaderTitle(system, PathFromUtf8(entry.path))
                                                : std::string();
    query.libretro = &index;
    query.libretro_folder = system.libretro_thumbnails;
    return query;
}

// A game the collection does not list under its own name: a hack or a translation. Its own
// art from the hack sources, else the box of the game it was made from, tagged.
bool Artwork::ResolveHack(const Entry& entry, const SystemDef& system, const ThumbnailIndex& index,
                          const std::string& original, const std::function<bool()>& stopping,
                          ArtworkResult& result) {
    const auto stopped = [&] { return stopping && stopping(); };
    const fs::path stem =
        cache_dir_ / "hacks" / PathFromUtf8(system.id) / PathFromUtf8(CacheFileName(original));
    const fs::path own = WithSuffix(stem, ".own");
    if (fs::path cached = FindCached(own); !cached.empty()) {
        result = {std::move(cached), ArtworkSource::Cache};
        return true;
    }
    const HackCoverOptions options = HackOptions();
    const HackQuery query = QueryFor(entry, system, index, original);
    const fs::path miss = WithSuffix(stem, ".miss");
    if (fetcher_ && options.online && !stopped() && !IsFresh(miss, HACK_MISS_LIFETIME)) {
        bool answered = true;
        int tried = 0;
        for (const CoverCandidate& candidate : sources_.Find(query, options, answered, stopping)) {
            if (!candidate.sure || tried++ >= 2 || stopped()) {
                break;
            }
            bool reached = false;
            if (fs::path file; Download(candidate.url, own, file, reached, true)) {
                result = {std::move(file), ArtworkSource::HackArt};
                return true;
            }
            answered = answered && reached;
        }
        if (answered && !stopped()) {
            Touch(miss);
        }
    }

    const fs::path marked = WithSuffix(stem, ".hackbox.png");
    if (IsRegularFile(marked)) {
        result = {marked, ArtworkSource::Cache};
        return true;
    }
    const fs::path raw_stem = WithSuffix(stem, ".raw");
    fs::path raw = FindCached(raw_stem);
    const fs::path no_base = WithSuffix(stem, ".nobase");
    if (raw.empty() && fetcher_ && !stopped() && !IsFresh(no_base, MISS_LIFETIME)) {
        const std::string name = MatchBaseGame(index, original, query.header);
        bool answered = name.empty();
        if (!name.empty()) {
            Download(std::string(THUMBNAIL_HOST) + UrlEncodeSegment(system.libretro_thumbnails) +
                         "/Named_Boxarts/" + UrlEncodeSegment(name) + ".png",
                     raw_stem, raw, answered);
        }
        if (raw.empty() && answered) {
            Touch(no_base);
        }
    }
    if (raw.empty()) {
        return false;
    }
    // A picture that cannot be read to tag it is shown as it is.
    result = {WriteHackMarkedCover(raw, marked) ? marked : raw, ArtworkSource::BaseGame};
    return true;
}

std::vector<CoverChoice> Artwork::Choices(const Entry& entry, const SystemDef* system,
                                          const std::function<bool()>& stopping) {
    std::vector<CoverChoice> out;
    if (std::string title; system != nullptr) {
        if (ArtworkResult found; ResolveLaunchBox(entry, *system, stopping, found, &title)) {
            out.push_back({"launchbox", title, PathToUtf8(found.path)});
        }
    }
    if (system == nullptr || system->libretro_thumbnails.empty() || !fetcher_) {
        return out;
    }
    const auto stopped = [&] { return stopping && stopping(); };
    const std::string original = entry.original_name.empty() ? entry.name : entry.original_name;
    const ThumbnailIndex& index = IndexFor(*system);
    const HackQuery query = QueryFor(entry, *system, index, original);
    const fs::path dir = cache_dir_ / "hacks" / "candidates";
    const auto fetch = [&](const std::string& url) {
        const fs::path stem = dir / PathFromUtf8(CacheFileName(url.substr(url.rfind('/') + 1)) + "-" +
                                                 std::to_string(Fnv1a(FNV_OFFSET, url.data(), url.size())));
        fs::path file = FindCached(stem);
        bool answered = false;
        if (file.empty() && !stopped()) {
            Download(url, stem, file, answered, true);
        }
        return file;
    };
    bool answered = true;
    for (const CoverCandidate& candidate : sources_.Find(query, HackOptions(), answered, stopping)) {
        if (out.size() >= MAX_CHOICES) {
            break;
        }
        if (const fs::path file = fetch(candidate.url); !file.empty()) {
            out.push_back({candidate.source, candidate.title, PathToUtf8(file)});
        }
    }
    // The game it was made from, or the game itself when the collection knows the name.
    std::string base = MatchThumbnail(index, original);
    const bool is_hack = base.empty();
    if (is_hack) {
        base = MatchBaseGame(index, original, query.header);
    }
    if (!base.empty() && !stopped()) {
        const fs::path raw = fetch(std::string(THUMBNAIL_HOST) + UrlEncodeSegment(system->libretro_thumbnails) +
                                   "/Named_Boxarts/" + UrlEncodeSegment(base) + ".png");
        if (!raw.empty()) {
            const fs::path marked = WithSuffix(raw, ".hackbox.png");
            const bool tagged = is_hack && (IsRegularFile(marked) || WriteHackMarkedCover(raw, marked));
            out.push_back({"base", base, PathToUtf8(tagged ? marked : raw)});
        }
    }
    return out;
}

ArtworkResult Artwork::Resolve(const Entry& entry, const SystemDef* system,
                               const std::function<bool()>& stopping) {
    if (!entry.cover.empty() && !entry.placeholder_cover) {
        if (fs::path local = PathFromUtf8(entry.cover); IsRegularFile(local)) {
            return {std::move(local), ArtworkSource::Local};
        }
    }
    const auto stopped = [&] { return stopping && stopping(); };
    const std::string original = entry.original_name.empty() ? entry.name : entry.original_name;
    if (system != nullptr && !system->cover_by_id.empty()) {
        if (const std::string id = GameFileId(PathFromUtf8(entry.path)); !id.empty()) {
            const fs::path stem = cache_dir_ / "ids" / PathFromUtf8(system->id) / PathFromUtf8(id);
            if (fs::path cached = FindCached(stem); !cached.empty()) {
                return {std::move(cached), ArtworkSource::Cache};
            }
            const fs::path miss = WithSuffix(stem, ".miss");
            if (fetcher_ && !stopped() && !IsFresh(miss, MISS_LIFETIME)) {
                bool answered = false;
                for (const std::string& url : system->cover_by_id) {
                    if (fs::path file; Download(WithId(url, id), stem, file, answered)) {
                        return {std::move(file), ArtworkSource::Downloaded};
                    }
                }
                if (answered) {
                    Touch(miss);
                }
            }
        }
    }
    if (ArtworkResult result; system != nullptr && ResolveLaunchBox(entry, *system, stopping, result)) {
        return result;
    }
    if (system != nullptr && !system->libretro_thumbnails.empty()) {
        const fs::path stem =
            cache_dir_ / "roms" / PathFromUtf8(system->id) / PathFromUtf8(CacheFileName(original));
        const ThumbnailIndex& index = IndexFor(*system);
        // Without a listing (it could not be fetched) only the exact name can be tried.
        const bool listed = !index.names.empty();
        const std::string name = listed ? MatchThumbnail(index, original) : ThumbnailFileName(original);
        if (listed && name.empty()) {
            if (ArtworkResult result; ResolveHack(entry, *system, index, original, stopping, result)) {
                return result;
            }
        } else if (fs::path cached = FindCached(stem); !cached.empty()) {
            return {std::move(cached), ArtworkSource::Cache};
        } else if (fs::path miss = WithSuffix(stem, ".miss2");
                   fetcher_ && !stopped() && !IsFresh(miss, MISS_LIFETIME)) {
            bool answered = false;
            const std::string url = std::string(THUMBNAIL_HOST) +
                                    UrlEncodeSegment(system->libretro_thumbnails) +
                                    "/Named_Boxarts/" + UrlEncodeSegment(name) + ".png";
            if (fs::path file; Download(url, stem, file, answered)) {
                return {std::move(file), ArtworkSource::Downloaded};
            }
            // Remember a real "not there"; a failed connection is tried again next time.
            if (answered) {
                Touch(miss);
            }
        }
    }
    const std::string system_id = system != nullptr ? system->id : entry.system;
    const std::uint64_t hash = Fnv1a(FNV_OFFSET, entry.id.data(), entry.id.size());
    const fs::path placeholder = cache_dir_ / "placeholders" /
                                 PathFromUtf8(system_id + "-" + std::to_string(hash) + ".png");
    if (!IsRegularFile(placeholder)) {
        WritePlaceholderCover(placeholder, entry.name, system_id);
    }
    return {placeholder, ArtworkSource::Placeholder};
}

} // namespace den::library
