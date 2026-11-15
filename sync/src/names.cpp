// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/sync/names.h"

namespace den::sync {

namespace fs = std::filesystem;

GameIndex::GameIndex(const std::vector<library::Entry>& entries, std::set<std::string> never)
    : never_(std::move(never)) {
    std::set<std::string> seen;
    for (const library::Entry& entry : entries) {
        if (entry.system.empty() || entry.path.empty() || never_.contains(entry.system)) {
            continue;
        }
        GameRef game;
        game.id = entry.id;
        game.system = entry.system;
        game.file = PathFromUtf8(entry.path);
        game.stem = PathToUtf8(game.file.stem());
        if (game.stem.empty()) {
            continue;
        }
        const std::string key = entry.system + "/" + ToLower(game.stem);
        if (!seen.insert(key).second) {
            duplicates_.push_back(entry.system + "/" + game.stem);
            log::Warn("sync", "{} has a second game named {}; only the first syncs", entry.system,
                      game.stem);
            continue;
        }
        games_.push_back(std::move(game));
    }
}

const GameRef* GameIndex::Find(const std::string& system, std::string_view stem) const {
    for (const GameRef& game : games_) {
        if (game.system == system && EqualsNoCase(game.stem, stem)) {
            return &game;
        }
    }
    return nullptr;
}

const GameRef* GameIndex::ById(const std::string& id) const {
    for (const GameRef& game : games_) {
        if (game.id == id) {
            return &game;
        }
    }
    return nullptr;
}

const GameRef* GameIndex::ByFileName(const std::string& system, std::string_view name) const {
    const GameRef* best = nullptr;
    const std::string lower = ToLower(name);
    for (const GameRef& game : games_) {
        if (game.system != system || game.stem.size() >= name.size() ||
            (best != nullptr && best->stem.size() >= game.stem.size())) {
            continue;
        }
        const std::string stem = ToLower(game.stem);
        if (lower.compare(0, stem.size(), stem) == 0 && lower[stem.size()] == '.') {
            best = &game;
        }
    }
    return best;
}

std::string SafeFileName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        const bool bad = u < 0x20 || std::strchr("<>:\"/\\|?*", c) != nullptr;
        out.push_back(bad ? '_' : c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    if (out.size() > 120) {
        out.resize(120);
    }
    return out.empty() ? "game" : out;
}

namespace {

std::vector<std::string_view> Parts(std::string_view path) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        parts.push_back(path.substr(start, end - start));
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return parts;
}

} // namespace

std::string CategoryOf(std::string_view path) {
    const std::string_view first = path.substr(0, path.find('/'));
    for (const char* category : {SAVES, STATES, CONFIG, GAMES, BIOS}) {
        if (first == category) {
            return category;
        }
    }
    return {};
}

std::string SystemOf(std::string_view path) {
    const std::vector<std::string_view> parts = Parts(path);
    const std::string category = CategoryOf(path);
    if ((category == SAVES || category == STATES || category == GAMES) && parts.size() >= 3) {
        return std::string(parts[1]);
    }
    if (category == CONFIG && parts.size() >= 4 && parts[1] == "games") {
        return std::string(parts[2]);
    }
    return {};
}

bool IsPortablePath(std::string_view path) {
    if (path.empty() || path.size() > 512 || CategoryOf(path).empty()) {
        return false;
    }
    for (const std::string_view part : Parts(path)) {
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
            return false;
        }
        for (const char c : part) {
            if (static_cast<unsigned char>(c) < 0x20 || std::strchr("<>:\"\\|?*", c) != nullptr) {
                return false;
            }
        }
    }
    return true;
}

bool IsPcPath(std::string_view path, const std::set<std::string>& pc) {
    const std::string system = SystemOf(path);
    if (!system.empty() && pc.contains(system)) {
        return true;
    }
    for (const std::string_view part : Parts(path)) {
        for (const std::string& name : pc) {
            if (EqualsNoCase(part, name)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<LocalTarget> TargetOf(const std::string& path, const GameIndex& games, const fs::path& data_dir,
                                    const fs::path& games_folder) {
    if (!IsPortablePath(path)) {
        return std::nullopt;
    }
    const std::string category = CategoryOf(path);
    const std::vector<std::string_view> parts = Parts(path);
    const std::string system = SystemOf(path);
    if (!system.empty() && games.Never(system)) {
        return std::nullopt;
    }
    if ((category == SAVES || category == STATES) && parts.size() == 3) {
        const std::string name(parts[2]);
        const GameRef* game = games.ByFileName(system, name);
        if (game == nullptr) {
            return std::nullopt;
        }
        const std::string local = SafeFileName(game->id) + name.substr(game->stem.size());
        return LocalTarget{data_dir / category / PathFromUtf8(SafeFileName(system)) / PathFromUtf8(local),
                           game->id};
    }
    if (category == CONFIG) {
        if (path == "config/settings.json" || path == "config/settings-overrides.json") {
            return LocalTarget{};
        }
        if (parts.size() == 4 && parts[1] == "games" && parts[3].ends_with(".json")) {
            const GameRef* game = games.Find(system, parts[3].substr(0, parts[3].size() - 5));
            if (game != nullptr) {
                return LocalTarget{{}, game->id};
            }
        }
        return std::nullopt;
    }
    if (category == BIOS && parts.size() >= 2) {
        return LocalTarget{data_dir / PathFromUtf8(path), {}};
    }
    if (category == GAMES && parts.size() >= 3 && !games_folder.empty()) {
        return LocalTarget{games_folder / PathFromUtf8(path.substr(path.find('/') + 1)), {}};
    }
    return std::nullopt;
}

} // namespace den::sync
