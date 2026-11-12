// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>

#include "den/base/files.h"
#include "den/library/artwork.h"
#include "den/library/content.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view THUMBNAIL_HOST = "https://thumbnails.libretro.com/";
// A lookup that found nothing is not repeated for this long.
constexpr auto MISS_LIFETIME = std::chrono::hours(24 * 14);
constexpr auto INDEX_LIFETIME = std::chrono::hours(24 * 30);

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

} // namespace

Artwork::Artwork(fs::path cache_dir, HttpFetcher fetcher)
    : cache_dir_(std::move(cache_dir)), fetcher_(std::move(fetcher)) {}

bool Artwork::Download(const std::string& url, const fs::path& file_stem, fs::path& out,
                       bool& answered) {
    const HttpAnswer response = fetcher_(url);
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

ArtworkResult Artwork::Resolve(const Entry& entry, const SystemDef* system,
                               const std::function<bool()>& stopping) {
    if (!entry.cover.empty() && !entry.placeholder_cover) {
        if (fs::path local = PathFromUtf8(entry.cover); IsRegularFile(local)) {
            return {std::move(local), ArtworkSource::Local};
        }
    }
    const auto stopped = [&] { return stopping && stopping(); };
    const std::string original = entry.original_name.empty() ? entry.name : entry.original_name;
    if (system != nullptr && !system->libretro_thumbnails.empty()) {
        const fs::path stem =
            cache_dir_ / "roms" / PathFromUtf8(system->id) / PathFromUtf8(CacheFileName(original));
        if (fs::path cached = FindCached(stem); !cached.empty()) {
            return {std::move(cached), ArtworkSource::Cache};
        }
        // The suffix changes with the matching rules, so older misses are looked up again.
        fs::path miss = stem;
        miss += ".miss2";
        if (fetcher_ && !stopped() && !IsFresh(miss, MISS_LIFETIME)) {
            const ThumbnailIndex& index = IndexFor(*system);
            // Without a listing (it could not be fetched) only the exact name can be tried.
            const bool listed = !index.names.empty();
            std::string name = listed ? MatchThumbnail(index, original) : ThumbnailFileName(original);
            if (listed && name.empty()) {
                // Not in the collection under its own name: a hack or a translation gets the
                // box of the game it was made from.
                const std::string header = entry.archive_member.empty()
                                               ? ReadRomHeaderTitle(*system, PathFromUtf8(entry.path))
                                               : std::string();
                name = MatchBaseGame(index, original, header);
            }
            // No candidate in a listing is an answer too: the collection has no such game.
            bool answered = name.empty() && listed;
            if (!name.empty() && !stopped()) {
                const std::string url = std::string(THUMBNAIL_HOST) +
                                        UrlEncodeSegment(system->libretro_thumbnails) +
                                        "/Named_Boxarts/" + UrlEncodeSegment(name) + ".png";
                if (fs::path file; Download(url, stem, file, answered)) {
                    return {std::move(file), ArtworkSource::Downloaded};
                }
            }
            // Remember a real "not there"; a failed connection is tried again next time.
            if (answered) {
                std::error_code ec;
                fs::create_directories(miss.parent_path(), ec);
                WriteFileAtomic(miss, "");
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
