// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <iterator>
#include <chrono>
#include <cstdlib>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/library/launchbox_covers.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

// The manifest is asked for again after this long; the letter files belong to one version.
constexpr auto MANIFEST_LIFETIME = std::chrono::hours(24 * 7);

// The accented letters the data's keys fold (tools/build.py, ACCENTS), by code point.
struct Fold {
    char32_t code;
    const char* plain;
};
constexpr Fold FOLDS[] = {
    {U'à', "a"}, {U'á', "a"}, {U'â', "a"}, {U'ã', "a"}, {U'ä', "a"}, {U'å', "a"}, {U'ā', "a"},
    {U'ç', "c"}, {U'ć', "c"}, {U'č', "c"}, {U'è', "e"}, {U'é', "e"}, {U'ê', "e"}, {U'ë', "e"},
    {U'ē', "e"}, {U'ė', "e"}, {U'ę', "e"}, {U'ì', "i"}, {U'í', "i"}, {U'î', "i"}, {U'ï', "i"},
    {U'ī', "i"}, {U'ñ', "n"}, {U'ń', "n"}, {U'ò', "o"}, {U'ó', "o"}, {U'ô', "o"}, {U'õ', "o"},
    {U'ö', "o"}, {U'ø', "o"}, {U'ō', "o"}, {U'ù', "u"}, {U'ú', "u"}, {U'û', "u"}, {U'ü', "u"},
    {U'ū', "u"}, {U'ý', "y"}, {U'ÿ', "y"}, {U'ś', "s"}, {U'š', "s"}, {U'ß', "s"}, {U'ź', "z"},
    {U'ż', "z"}, {U'ž', "z"}, {U'ł', "l"}, {U'æ', "ae"}, {U'œ', "oe"},
    // Capitals of the above: the title is lowered before folding.
    {U'À', "a"}, {U'Á', "a"}, {U'É', "e"}, {U'Ö', "o"}, {U'Ü', "u"}, {U'Ñ', "n"}, {U'Ç', "c"},
    {U'Æ', "ae"}, {U'Ø', "o"},
};

// Lower case with the accents folded; other characters beyond ASCII are kept as they are.
std::string Lowered(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
            ++i;
            continue;
        }
        const std::size_t length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        char32_t code = length == 2 ? c & 0x1F : length == 3 ? c & 0x0F : c & 0x07;
        for (std::size_t k = 1; k < length && i + k < text.size(); ++k) {
            code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        }
        const auto fold = std::find_if(std::begin(FOLDS), std::end(FOLDS), [code](const Fold& f) { return f.code == code; });
        if (fold != std::end(FOLDS)) {
            out += fold->plain;
        } else {
            out.append(text.substr(i, length));
        }
        i += length;
    }
    return out;
}

void ReplaceAll(std::string& text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

bool IsFresh(const fs::path& file, std::chrono::hours lifetime) {
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    return !ec && fs::file_time_type::clock::now() - written < lifetime;
}

// LaunchBox's region names that stand for a region tag of a ROM name, best first.
std::vector<std::string> RegionsFor(const std::string& tag) {
    if (tag == "USA") {
        return {"North America", "United States", "Canada"};
    }
    if (tag == "Europe") {
        return {"Europe", "United Kingdom"};
    }
    if (tag == "UK") {
        return {"United Kingdom", "Europe"};
    }
    if (tag == "Netherlands") {
        return {"The Netherlands", "Europe"};
    }
    return {tag};
}

std::vector<std::string> RegionOrder(const std::vector<std::string>& tags) {
    std::vector<std::string> order;
    for (const std::string& tag : tags) {
        for (std::string& region : RegionsFor(tag)) {
            order.push_back(std::move(region));
        }
    }
    for (const char* region : {"North America", "World", "Europe", "United States", "", "Japan"}) {
        order.emplace_back(region);
    }
    return order;
}

// The image of the region that comes first in `order` ([[region, file], ...]), else the first.
std::pair<std::string, std::string> PickRegion(const std::vector<JsonValue>& covers,
                                               const std::vector<std::string>& order) {
    for (const std::string& region : order) {
        for (const JsonValue& cover : covers) {
            if (cover.items.size() == 2 && cover.items[0].string == region) {
                return {region, cover.items[1].string};
            }
        }
    }
    if (!covers.empty() && covers.front().items.size() == 2) {
        return {covers.front().items[0].string, covers.front().items[1].string};
    }
    return {};
}

// The best cover of one game ([id, name, [[region, file], ...]]), "" when it has none.
std::pair<std::string, std::string> PickCover(const JsonValue& game, const std::vector<std::string>& order) {
    return game.items.size() < 3 ? std::pair<std::string, std::string>{} : PickRegion(game.items[2].items, order);
}

bool SafeFileName(const std::string& name) {
    return !name.empty() && name.size() <= 128 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
               c == '_' || c == '.';
    }) && name.find("..") == std::string::npos;
}

} // namespace

std::string LaunchBoxKey(std::string_view title) {
    std::string text = Lowered(title);
    ReplaceAll(text, "&", " and ");
    for (const char* article : {", the ", ", the-", ", the:"}) {
        ReplaceAll(text, article, " ");
    }
    if (text.ends_with(", the")) {
        text.resize(text.size() - 5);
    }
    std::string trimmed = Trim(text);
    if (trimmed.starts_with("the ")) {
        trimmed.erase(0, 4);
    }
    std::string key;
    for (const char c : trimmed) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            key.push_back(c);
        }
    }
    return key;
}

LaunchBoxCovers::LaunchBoxCovers(fs::path cache_dir, HttpFetcher fetcher)
    : cache_dir_(std::move(cache_dir)), fetcher_(std::move(fetcher)) {}

LaunchBoxCovers::~LaunchBoxCovers() = default;

const LaunchBoxCovers::Manifest* LaunchBoxCovers::LoadManifest(bool& answered) {
    if (manifest_tried_) {
        return manifest_ ? &*manifest_ : nullptr;
    }
    const fs::path file = cache_dir_ / "manifest.json";
    std::optional<std::string> text;
    if (IsFresh(file, MANIFEST_LIFETIME)) {
        text = ReadTextFile(file);
        // A copy from before the data had faces, consoles or details is asked for again, so
        // they show now.
        if (text && (text->find("\"face_kinds\"") == std::string::npos ||
                     text->find("\"device\"") == std::string::npos ||
                     text->find("\"detail_kinds\"") == std::string::npos)) {
            text.reset();
        }
    }
    if (!text && fetcher_) {
        const HttpAnswer answer = fetcher_(std::string(LAUNCHBOX_DATA) + "manifest.json");
        if (answer.status == 200 && ParseJson(answer.body)) {
            std::error_code ec;
            fs::create_directories(cache_dir_, ec);
            WriteFileAtomic(file, answer.body);
            text = answer.body;
        } else {
            answered = answered && answer.status != 0;
            text = ReadTextFile(file); // an old copy beats none
        }
    }
    manifest_tried_ = true;
    const auto json = text ? ParseJson(*text) : std::nullopt;
    if (!json || json->Find("format") == nullptr || json->Find("format")->number != 1) {
        return nullptr;
    }
    Manifest manifest;
    manifest.version = json->String("version");
    manifest.images = json->String("images");
    if (const JsonValue* libretro = json->Find("libretro")) {
        for (const auto& [folder, platform] : libretro->members) {
            manifest.libretro.emplace(folder, platform.string);
        }
    }
    if (const JsonValue* platforms = json->Find("platforms")) {
        for (const auto& [name, value] : platforms->members) {
            Platform platform;
            platform.dir = value.String("dir");
            if (const JsonValue* buckets = value.Find("buckets")) {
                for (const JsonValue& bucket : buckets->items) {
                    platform.buckets.push_back(bucket.string);
                }
            }
            for (auto [kind, list] : {std::pair{"faces", &platform.faces}, std::pair{"details", &platform.details}}) {
                if (const JsonValue* buckets = value.Find(kind)) {
                    for (const JsonValue& bucket : buckets->items) {
                        list->push_back(bucket.string);
                    }
                }
            }
            if (const std::string device = value.String("device");
                device.starts_with("Platforms/") && SafeFileName(device.substr(10))) {
                platform.device = device;
            }
            if (SafeFileName(platform.dir)) {
                manifest.platforms.emplace(name, std::move(platform));
            }
        }
    }
    if (!SafeFileName(manifest.version) || !manifest.images.starts_with("https://")) {
        return nullptr;
    }
    // Letter files of older versions are not read again.
    std::error_code ec;
    for (const auto& old : fs::directory_iterator(cache_dir_, ec)) {
        if (old.is_directory() && old.path().filename() != PathFromUtf8(manifest.version)) {
            fs::remove_all(old.path(), ec);
        }
    }
    manifest_ = std::move(manifest);
    return &*manifest_;
}

const JsonValue* LaunchBoxCovers::LoadBucket(const Manifest& manifest, const Platform& platform,
                                             const std::string& bucket, bool& answered, const char* kind) {
    const bool covers = std::string_view(kind) == "covers";
    const std::string relative = std::string(kind) + "/" + platform.dir + "/" + bucket + ".json";
    if (const auto found = buckets_.find(relative); found != buckets_.end()) {
        return found->second.get();
    }
    // Cover files sit right under the version, as they always have; the others under their kind.
    const fs::path base = cache_dir_ / PathFromUtf8(manifest.version);
    const fs::path file = (covers ? base : base / kind) / PathFromUtf8(platform.dir) / PathFromUtf8(bucket + ".json");
    std::optional<JsonValue> json;
    if (const auto text = ReadTextFile(file)) {
        json = ParseJson(*text);
    }
    if (!json && fetcher_) {
        const HttpAnswer answer = fetcher_(std::string(LAUNCHBOX_DATA) + relative);
        answered = answered && answer.status != 0;
        if (answer.status == 200) {
            json = ParseJson(answer.body);
            if (json) {
                std::error_code ec;
                fs::create_directories(file.parent_path(), ec);
                WriteFileAtomic(file, answer.body);
            }
        }
    }
    if (!json) {
        return nullptr; // asked again on the next run
    }
    auto& slot = buckets_[relative];
    slot = std::make_unique<JsonValue>(std::move(*json));
    return slot.get();
}

const LaunchBoxCovers::Platform* LaunchBoxCovers::PlatformOf(const Manifest& manifest,
                                                              const SystemDef& system) const {
    std::string name = system.launchbox;
    if (name.empty()) {
        const auto mapped = manifest.libretro.find(system.libretro_thumbnails);
        if (mapped == manifest.libretro.end()) {
            return nullptr;
        }
        name = mapped->second;
    }
    const auto platform = manifest.platforms.find(name);
    return platform == manifest.platforms.end() ? nullptr : &platform->second;
}

std::optional<LaunchBoxCovers::Match> LaunchBoxCovers::FindMatch(const Manifest& manifest_ref,
                                                                  const SystemDef& system,
                                                                  const std::vector<std::string>& titles,
                                                                  const std::vector<std::string>& order,
                                                                  bool& answered) {
    const Manifest* manifest = &manifest_ref;
    const Platform* platform = PlatformOf(*manifest, system);
    if (platform == nullptr) {
        return std::nullopt;
    }
    for (const std::string& title : titles) {
        const std::string key = LaunchBoxKey(title);
        if (key.empty()) {
            continue;
        }
        const std::string bucket = key[0] >= '0' && key[0] <= '9' ? std::string("0") : key.substr(0, 1);
        const auto& listed = platform->buckets;
        if (std::find(listed.begin(), listed.end(), bucket) == listed.end()) {
            continue;
        }
        const JsonValue* games = LoadBucket(*manifest, *platform, bucket, answered);
        const JsonValue* matches = games ? games->Find(key) : nullptr;
        if (matches == nullptr || matches->items.empty()) {
            continue;
        }
        // Several games with one key (a remake, a re-release): the first with a fitting cover.
        for (const JsonValue& game : matches->items) {
            auto [region, image] = PickCover(game, order);
            if (SafeFileName(image)) {
                return Match{&game, platform, bucket, std::move(region), std::move(image)};
            }
        }
    }
    return std::nullopt;
}

std::optional<LaunchBoxCover> LaunchBoxCovers::Find(const SystemDef& system, const std::vector<std::string>& titles,
                                                    const std::vector<std::string>& region_tags, bool& answered) {
    const std::lock_guard lock(mutex_);
    const Manifest* manifest = LoadManifest(answered);
    if (manifest == nullptr) {
        return std::nullopt;
    }
    const auto match = FindMatch(*manifest, system, titles, RegionOrder(region_tags), answered);
    if (!match) {
        return std::nullopt;
    }
    return LaunchBoxCover{manifest->images + match->image, match->game->items[1].string, match->region};
}

std::optional<LaunchBoxFaces> LaunchBoxCovers::FindFaces(const SystemDef& system,
                                                         const std::vector<std::string>& titles,
                                                         const std::vector<std::string>& region_tags,
                                                         bool& answered) {
    const std::lock_guard lock(mutex_);
    const Manifest* manifest = LoadManifest(answered);
    if (manifest == nullptr) {
        return std::nullopt;
    }
    std::vector<std::string> order = RegionOrder(region_tags);
    const auto match = FindMatch(*manifest, system, titles, order, answered);
    if (!match) {
        return std::nullopt;
    }
    const auto& listed = match->platform->faces;
    if (std::find(listed.begin(), listed.end(), match->bucket) == listed.end()) {
        return std::nullopt;
    }
    const JsonValue* bucket = LoadBucket(*manifest, *match->platform, match->bucket, answered, "faces");
    const std::string id = std::to_string(static_cast<long long>(match->game->items[0].number));
    const JsonValue* faces = bucket ? bucket->Find(id) : nullptr;
    if (faces == nullptr) {
        return std::nullopt;
    }
    // The faces of one copy of the game: the front cover's region first.
    order.insert(order.begin(), match->region);
    const auto pick = [&](const char* face) -> std::string {
        const JsonValue* regions = faces->Find(face);
        if (regions == nullptr) {
            return {};
        }
        auto [region, image] = PickRegion(regions->items, order);
        return SafeFileName(image) ? manifest->images + image : std::string();
    };
    LaunchBoxFaces out{pick("back"), pick("spine"), pick("cart"), pick("cart_back"), pick("disc")};
    if (out.back.empty() && out.spine.empty() && out.cart.empty() && out.cart_back.empty() && out.disc.empty()) {
        return std::nullopt;
    }
    return out;
}

GameDetails LaunchBoxCovers::FindDetails(const SystemDef& system, const std::vector<std::string>& titles,
                                         const std::vector<std::string>& region_tags, bool& answered) {
    const std::lock_guard lock(mutex_);
    GameDetails out;
    const Manifest* manifest = LoadManifest(answered);
    const auto match = manifest ? FindMatch(*manifest, system, titles, RegionOrder(region_tags), answered) : std::nullopt;
    if (!match) {
        return out;
    }
    const auto& listed = match->platform->details;
    if (std::find(listed.begin(), listed.end(), match->bucket) == listed.end()) {
        return out;
    }
    const JsonValue* bucket = LoadBucket(*manifest, *match->platform, match->bucket, answered, "details");
    const std::string id = std::to_string(static_cast<long long>(match->game->items[0].number));
    const JsonValue* game = bucket ? bucket->Find(id) : nullptr;
    if (game == nullptr || game->kind != JsonValue::Kind::Object) {
        return out;
    }
    const auto number = [&](const char* name) {
        const JsonValue* value = game->Find(name);
        return value && value->kind == JsonValue::Kind::Number ? value->number : 0.0;
    };
    out.found = true;
    out.title = match->game->items[1].string;
    out.overview = game->String("overview");
    out.date = game->String("date");
    out.year = static_cast<int>(number("year"));
    if (out.year == 0 && out.date.size() >= 4) {
        out.year = std::atoi(out.date.substr(0, 4).c_str());
    }
    if (const JsonValue* genres = game->Find("genres")) {
        for (const JsonValue& genre : genres->items) {
            out.genres.push_back(genre.string);
        }
    }
    out.developer = game->String("developer");
    out.publisher = game->String("publisher");
    out.players = static_cast<int>(number("players"));
    out.rating = number("rating");
    out.votes = static_cast<int>(number("votes"));
    out.esrb = game->String("esrb");
    return out;
}

std::string LaunchBoxCovers::FindDevice(const SystemDef& system, bool& answered) {
    const std::lock_guard lock(mutex_);
    const Manifest* manifest = LoadManifest(answered);
    const Platform* platform = manifest ? PlatformOf(*manifest, system) : nullptr;
    return platform == nullptr || platform->device.empty() ? std::string() : manifest->images + platform->device;
}

} // namespace den::library
