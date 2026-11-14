// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Artwork's lookups in the LaunchBox Games Database besides the cover: a game's other faces
// and details, and its system's console.

#include <format>
#include <functional>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/library/artwork.h"
#include "den/library/launchbox_covers.h"
#include "den/library/names.h"
#include "artwork_cache.h"

namespace den::library {

namespace fs = std::filesystem;
using namespace artwork_cache;

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
    // ".faces2" marks a game whose faces were all fetched, ".faces2.miss" one without any (the
    // number goes up when the rules change, so earlier answers are asked again; pictures
    // already downloaded are kept).
    const fs::path done = WithSuffix(stem, ".faces2"), miss = WithSuffix(stem, ".faces2.miss");
    const fs::path base_mark = WithSuffix(stem, ".faces2.base");
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

std::string Artwork::Device(const SystemDef* system, const std::string& url, const std::function<bool()>& stopping) {
    if (system == nullptr || (url.empty() && system->launchbox.empty() && system->libretro_thumbnails.empty()) ||
        (!url.empty() && !url.starts_with("https://"))) {
        return {};
    }
    const std::string name =
        url.empty() ? system->id : std::format("{}-{:016x}", system->id, std::hash<std::string>{}(url));
    const fs::path stem = cache_dir_ / "launchbox-devices" / PathFromUtf8(CacheFileName(name));
    if (fs::path cached = FindCached(stem); !cached.empty()) {
        return PathToUtf8(cached);
    }
    const fs::path miss = WithSuffix(stem, ".miss");
    if (!fetcher_ || (stopping && stopping()) || IsFresh(miss, MISS_LIFETIME)) {
        return {};
    }
    bool answered = true;
    const std::string address = url.empty() ? launchbox_->FindDevice(*system, answered) : url;
    fs::path file;
    if (!address.empty() && Download(address, stem, file, answered)) {
        return PathToUtf8(file);
    }
    if (answered) {
        Touch(miss);
    }
    return {};
}

namespace {

JsonValue DetailsJson(const GameDetails& d) {
    std::vector<JsonValue> genres;
    for (const std::string& genre : d.genres) {
        genres.push_back(JsonString(genre));
    }
    return JsonObject({{"title", JsonString(d.title)},
                       {"overview", JsonString(d.overview)},
                       {"date", JsonString(d.date)},
                       {"year", JsonNumber(d.year)},
                       {"genres", JsonArray(std::move(genres))},
                       {"developer", JsonString(d.developer)},
                       {"publisher", JsonString(d.publisher)},
                       {"players", JsonNumber(d.players)},
                       {"rating", JsonNumber(d.rating)},
                       {"votes", JsonNumber(d.votes)},
                       {"esrb", JsonString(d.esrb)}});
}

GameDetails DetailsFrom(const JsonValue& json) {
    GameDetails d;
    const auto number = [&](const char* name) {
        const JsonValue* value = json.Find(name);
        return value ? value->number : 0.0;
    };
    d.found = true;
    d.title = json.String("title");
    d.overview = json.String("overview");
    d.date = json.String("date");
    d.year = static_cast<int>(number("year"));
    if (const JsonValue* genres = json.Find("genres")) {
        for (const JsonValue& genre : genres->items) {
            d.genres.push_back(genre.string);
        }
    }
    d.developer = json.String("developer");
    d.publisher = json.String("publisher");
    d.players = static_cast<int>(number("players"));
    d.rating = number("rating");
    d.votes = static_cast<int>(number("votes"));
    d.esrb = json.String("esrb");
    return d;
}

} // namespace

GameDetails Artwork::Details(const Entry& entry, const SystemDef* system, const std::function<bool()>& stopping) {
    if (system == nullptr || (system->launchbox.empty() && system->libretro_thumbnails.empty())) {
        return {};
    }
    const std::string original = entry.original_name.empty() ? entry.name : entry.original_name;
    const fs::path stem =
        cache_dir_ / "launchbox-details" / PathFromUtf8(system->id) / PathFromUtf8(CacheFileName(original));
    const fs::path file = WithSuffix(stem, ".json"), miss = WithSuffix(stem, ".miss");
    if (const auto text = ReadTextFile(file)) {
        if (const auto json = ParseJson(*text); json && json->kind == JsonValue::Kind::Object) {
            return DetailsFrom(*json);
        }
    }
    if (!fetcher_ || (stopping && stopping()) || IsFresh(miss, MISS_LIFETIME)) {
        return {};
    }
    std::vector<std::string> titles{entry.name};
    if (std::string base = ParseGameName(original).base; base != entry.name) {
        titles.push_back(std::move(base));
    }
    bool answered = true;
    GameDetails found = launchbox_->FindDetails(*system, titles, RegionTags(original), answered);
    if (found.found) {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        WriteFileAtomic(file, WriteJson(DetailsJson(found)));
    } else if (answered) {
        Touch(miss);
    }
    return found;
}

} // namespace den::library
