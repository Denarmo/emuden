// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The catalog: the local files of the enabled categories under their portable names.

#include <algorithm>
#include <chrono>
#include <map>
#include <system_error>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/json.h"
#include "den/base/sha256.h"
#include "den/sync/plan.h"

namespace den::sync {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t MAX_INDEX = 1u << 20;

bool Cancelled(const CatalogRequest& request) {
    return request.cancel != nullptr && request.cancel->load();
}

void Add(Catalog& out, HashCache* hashes, std::string path, const fs::path& file, const std::string& game) {
    std::error_code ec;
    const std::uint64_t size = fs::file_size(file, ec);
    if (ec || !IsPortablePath(path)) {
        return;
    }
    LocalItem item;
    item.path = std::move(path);
    item.file = file;
    item.size = size;
    item.modified = FileTime(file);
    item.hash = hashes != nullptr ? hashes->Hash(file, size, item.modified) : GameHash(size);
    item.game_id = game;
    if (!item.hash.empty()) {
        out[item.path] = std::move(item);
    }
}

// saves/<system>/<stem><rest> for every <id>.<rest> the player keeps for a game.
void AddGameFiles(const CatalogRequest& request, HashCache& hashes, const char* category, Catalog& out) {
    std::map<std::string, std::map<std::string, const GameRef*>> by_system;
    for (const GameRef& game : request.games->Games()) {
        if ((!request.only_game.empty() && game.id != request.only_game) ||
            request.games->Never(std::string(category) + "/" + game.system)) {
            continue;
        }
        by_system[game.system][SafeFileName(game.id) + "."] = &game;
    }
    for (const auto& [system, bases] : by_system) {
        const fs::path dir = request.data_dir / category / PathFromUtf8(SafeFileName(system));
        for (const fs::path& file : ListDirectory(dir)) {
            if (Cancelled(request) || !IsRegularFile(file)) {
                continue;
            }
            const std::string name = PathToUtf8(file.filename());
            if (name.ends_with(".sync-part") || name.ends_with(".tmp")) {
                continue;
            }
            const auto base = bases.upper_bound(name);
            if (base == bases.begin()) {
                continue;
            }
            const auto& [prefix, game] = *std::prev(base);
            if (name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            const std::string rest = name.substr(prefix.size() - 1);
            Add(out, &hashes, std::string(category) + "/" + system + "/" + game->stem + rest, file, game->id);
        }
    }
}

// A game's files: the file itself, and for an index (cue, m3u, gdi) the files beside it that
// it names.
std::vector<fs::path> GameFiles(const GameRef& game) {
    std::vector<fs::path> files{game.file};
    const std::string extension = ToLower(PathToUtf8(game.file.extension()));
    if (extension != ".cue" && extension != ".m3u" && extension != ".gdi" && extension != ".ccd") {
        return files;
    }
    std::error_code ec;
    if (fs::file_size(game.file, ec) > MAX_INDEX) {
        return files;
    }
    const std::string text = ToLower(ReadTextFile(game.file).value_or(""));
    for (const fs::path& sibling : ListDirectory(game.file.parent_path())) {
        const std::string name = ToLower(PathToUtf8(sibling.filename()));
        if (sibling != game.file && IsRegularFile(sibling) && text.find(name) != std::string::npos) {
            files.push_back(sibling);
        }
    }
    return files;
}

void AddBios(const CatalogRequest& request, HashCache& hashes, Catalog& out) {
    const fs::path root = request.data_dir / BIOS;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || Cancelled(request)) {
            continue;
        }
        std::string relative = PathToUtf8(it->path().lexically_relative(root));
        std::replace(relative.begin(), relative.end(), '\\', '/');
        if (!relative.empty() && relative.front() != '.') {
            Add(out, &hashes, std::string(BIOS) + "/" + relative, it->path(), {});
        }
    }
}

} // namespace

std::int64_t FileTime(const fs::path& file) {
    std::error_code ec;
    const auto time = fs::last_write_time(file, ec);
    if (ec) {
        return 0;
    }
    const auto system = std::chrono::system_clock::now() +
                        std::chrono::duration_cast<std::chrono::system_clock::duration>(
                            time - fs::file_time_type::clock::now());
    // Rounded: the two clocks are read a moment apart, so a time set to a whole second can
    // come back a hair below it.
    return std::chrono::round<std::chrono::seconds>(system.time_since_epoch()).count();
}

void SetFileTime(const fs::path& file, std::int64_t unix_seconds) {
    if (unix_seconds <= 0) {
        return;
    }
    const auto wanted = std::chrono::system_clock::time_point(std::chrono::seconds(unix_seconds));
    const auto time = fs::file_time_type::clock::now() +
                      std::chrono::duration_cast<fs::file_time_type::duration>(
                          wanted - std::chrono::system_clock::now());
    std::error_code ec;
    fs::last_write_time(file, time, ec);
}

HashCache::HashCache(fs::path file) : file_(std::move(file)) {
    const auto text = ReadTextFile(file_);
    const auto root = text ? ParseJson(*text) : std::nullopt;
    if (root && root->kind == JsonValue::Kind::Object) {
        for (const auto& [path, value] : root->members) {
            const JsonValue* size = value.Find("size");
            const JsonValue* modified = value.Find("modified");
            if (size && modified) {
                known_[path] = {static_cast<std::uint64_t>(size->number),
                                static_cast<std::int64_t>(modified->number), value.String("sha256")};
            }
        }
    }
}

std::string HashCache::Hash(const fs::path& file, std::uint64_t size, std::int64_t modified) {
    const std::string key = PathToUtf8(file);
    if (const auto found = known_.find(key);
        found != known_.end() && found->second.size == size && found->second.modified == modified) {
        return found->second.hash;
    }
    const std::string hash = Sha256File(file).value_or("");
    if (!hash.empty()) {
        known_[key] = {size, modified, hash};
        changed_ = true;
    }
    return hash;
}

void HashCache::Save() {
    if (!changed_) {
        return;
    }
    JsonValue root = JsonObject();
    for (const auto& [path, known] : known_) {
        if (!IsRegularFile(PathFromUtf8(path))) {
            continue; // forgotten once the file is gone
        }
        root.members.emplace_back(path, JsonObject({{"size", JsonNumber(static_cast<double>(known.size))},
                                                    {"modified", JsonNumber(static_cast<double>(known.modified))},
                                                    {"sha256", JsonString(known.hash)}}));
    }
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    if (WriteFileAtomic(file_, WriteJson(root))) {
        changed_ = false;
    }
}

Catalog BuildCatalog(const CatalogRequest& request, HashCache& hashes) {
    Catalog out;
    const auto wants = [&](const char* category) { return request.categories.contains(category); };
    if (request.games != nullptr && wants(SAVES)) {
        AddGameFiles(request, hashes, SAVES, out);
    }
    if (request.games != nullptr && wants(STATES)) {
        AddGameFiles(request, hashes, STATES, out);
    }
    if (request.games != nullptr && request.config != nullptr && wants(CONFIG)) {
        for (LocalItem& item : request.config->Export(*request.games)) {
            if (IsPortablePath(item.path)) {
                item.size = item.text.size();
                item.hash = Sha256Hex(item.text.data(), item.text.size());
                out[item.path] = std::move(item);
            }
        }
    }
    if (wants(BIOS)) {
        AddBios(request, hashes, out);
    }
    if (request.games != nullptr && wants(GAMES) && request.upload_games) {
        for (const GameRef& game : request.games->Games()) {
            if (Cancelled(request)) {
                break;
            }
            for (const fs::path& file : GameFiles(game)) {
                Add(out, nullptr, std::string(GAMES) + "/" + game.system + "/" + PathToUtf8(file.filename()), file,
                    game.id);
            }
        }
    }
    return out;
}

} // namespace den::sync
