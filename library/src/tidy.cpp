// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The tidy-up's plan: which games get a clean file name or only a clean name shown, and which
// junk files and empty folders go (den/library/tidy.h).

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/library/downloads.h"
#include "den/library/names.h"
#include "den/library/rom_codes.h"
#include "den/library/tidy.h"
#include "downloads_work.h"
#include "hacks_store.h"

namespace den::library::tidy {

namespace fs = std::filesystem;

namespace {

constexpr int MAX_DEPTH = 6;

// A Mac's AppleDouble file ("._name") starts with these bytes.
bool IsAppleDouble(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    unsigned char head[4] = {};
    in.read(reinterpret_cast<char*>(head), 4);
    return in.gcount() == 4 && head[0] == 0x00 && head[1] == 0x05 && head[2] == 0x16 && head[3] == 0x07;
}

bool IsJunk(const fs::path& file) {
    const std::string name = PathToUtf8(file.filename());
    return name == ".DS_Store" || (name.starts_with("._") && IsAppleDouble(file));
}

// Junk files of a folder and its subfolders; then the folders left empty, deepest first.
bool Walk(const fs::path& dir, int depth, const SystemTable& systems, bool root, Plan& plan) {
    bool empty = true;
    for (const fs::path& path : ListDirectory(dir)) {
        if (IsDirectory(path)) {
            const std::string name = PathToUtf8(path.filename());
            if (depth >= MAX_DEPTH || name.starts_with('.') ||
                !Walk(path, depth + 1, systems, false, plan)) {
                empty = false;
            }
        } else if (IsJunk(path)) {
            plan.changes.push_back({ChangeKind::RemoveJunk, {}, {}, path, {}, {}, {}, {}});
        } else {
            empty = false;
        }
    }
    // A console's folder stays even when empty: it is where that console's games go.
    const std::string name = PathToUtf8(dir.filename());
    const bool keeps = root || (systems.FromFolderName(name) != nullptr && ToLower(name) != "hacks");
    if (empty && !keeps) {
        plan.changes.push_back({ChangeKind::RemoveFolder, {}, {}, dir, {}, {}, {}, {}});
        return true;
    }
    return false;
}

bool IsSheet(const fs::path& file) {
    const std::string extension = ToLower(PathToUtf8(file.extension()));
    return extension == ".cue" || extension == ".gdi" || extension == ".m3u" || extension == ".ccd";
}

// Hacks patched before they came, by the code in their headers: the games of a console that
// share a code are compared with each other only.
void LinkHacks(const std::vector<Entry>& entries, Plan& plan, std::set<std::string>& linked) {
    std::map<std::pair<std::string, std::string>, std::vector<Entry>> by_code;
    for (const Entry& entry : entries) {
        if (entry.hack_of.empty() && entry.archive_member.empty() && IsRegularFile(PathFromUtf8(entry.path))) {
            const RomCode header = ReadRomCode(PathFromUtf8(entry.path), entry.system);
            const std::string key = !header.code.empty() ? header.code : header.title.empty() ? "" : "title:" + ToLower(header.title);
            if (!key.empty()) {
                by_code[{entry.system, key}].push_back(entry);
            }
        }
    }
    for (const auto& [key, games] : by_code) {
        for (const Entry& game : games) {
            if (games.size() < 2 || downloads::IsBiosName(PathToUtf8(PathFromUtf8(game.path).filename()))) {
                continue;
            }
            const fs::path file = PathFromUtf8(game.path);
            const auto base = FindHackBase(games, game.system, file, game.name, ReadRomCode(file, game.system));
            if (base && !linked.contains(base->id)) {
                Change change;
                change.kind = ChangeKind::LinkHack;
                change.id = game.id;
                change.system = game.system;
                change.from = file;
                change.name = base->name;
                change.base_id = base->id;
                plan.changes.push_back(std::move(change));
                linked.insert(game.id);
            }
        }
    }
}

bool Syncs(const std::vector<std::string>& synced, const std::string& system, const std::string& stem) {
    const std::string key = ToLower(system + "/" + stem);
    return std::binary_search(synced.begin(), synced.end(), key);
}

} // namespace

std::vector<std::string> SyncedStems(const fs::path& data_dir) {
    std::set<std::string> stems;
    for (const fs::path& provider : ListDirectory(data_dir / "sync")) {
        const auto text = ReadTextFile(provider / "journal.json");
        const auto root = text ? ParseJson(*text) : std::nullopt;
        const JsonValue* files = root ? root->Find("files") : nullptr;
        if (files == nullptr) {
            continue;
        }
        for (const auto& [portable, value] : files->members) {
            // "saves/gba/<stem>.srm", "states/gba/<stem>.slot1.state", "config/games/gba/<stem>.json",
            // "games/gba/<file>": everything after the console's folder up to an extension.
            std::string rest = portable;
            for (const char* category : {"saves/", "states/", "config/games/", "games/"}) {
                if (rest.starts_with(category)) {
                    rest = rest.substr(std::strlen(category));
                    const std::size_t slash = rest.find('/');
                    if (slash == std::string::npos) {
                        break;
                    }
                    // A stem may hold dots: every prefix that ends before one counts.
                    for (std::size_t dot = rest.find('.', slash); dot != std::string::npos; dot = rest.find('.', dot + 1)) {
                        stems.insert(ToLower(rest.substr(0, dot)));
                    }
                    break;
                }
            }
        }
    }
    return {stems.begin(), stems.end()};
}

Plan MakePlan(Library& library, const fs::path& data_dir) {
    Plan plan;
    const SystemTable systems = library.Consoles();
    const std::vector<std::string> synced = SyncedStems(data_dir);
    std::set<std::string> taken; // lower-case paths the plan gives to games
    std::set<std::string> linked; // games the plan links to their base games
    const std::vector<Entry> entries = library.Entries();
    LinkHacks(entries, plan, linked);
    for (const Entry& entry : entries) {
        const fs::path file = PathFromUtf8(entry.path);
        const SystemDef* def = systems.Find(entry.system);
        if (def == nullptr || !def->scanner.empty() || !entry.hack_of.empty() || !entry.archive_member.empty() ||
            entry.disc != 0 || !entry.other_versions.empty() || IsSheet(file) || !IsRegularFile(file) ||
            downloads::IsBiosName(PathToUtf8(file.filename()))) {
            continue;
        }
        ++plan.games;
        if (linked.contains(entry.id)) {
            continue; // a hack keeps its name
        }
        const std::string file_name = PathToUtf8(file.filename());
        if (downloads::TidyFileName(file_name)) {
            continue;
        }
        downloads::NameSources sources;
        sources.file_name = file_name;
        sources.header_title = downloads::HeaderName(downloads::HeaderTitle(file, entry.system));
        const std::string name = downloads::GameName(sources);
        const std::string stem = SafeFileName(name);
        if (stem.empty() || stem == PathToUtf8(file.stem())) {
            continue;
        }
        Change change;
        change.id = entry.id;
        change.system = entry.system;
        change.from = file;
        change.name = name;
        change.to = file.parent_path() / PathFromUtf8(stem + PathToUtf8(file.extension()));
        const std::string key = ToLower(PathToUtf8(change.to));
        if (Syncs(synced, entry.system, PathToUtf8(file.stem()))) {
            change.why = "its saves sync under the file's name";
        } else if (fs::exists(change.to) || taken.contains(key)) {
            change.why = "another file has that name";
        } else if (!downloads::CanWriteHere(file.parent_path())) {
            change.why = "its folder cannot be written";
        }
        change.kind = change.why.empty() ? ChangeKind::Rename : ChangeKind::ShowName;
        if (change.kind == ChangeKind::Rename) {
            taken.insert(key);
        } else if (entry.name == name) {
            continue; // shown under that name already
        }
        plan.changes.push_back(std::move(change));
    }
    for (const Folder& folder : library.Folders()) {
        const fs::path root = PathFromUtf8(folder.path);
        if (IsDirectory(root)) {
            Walk(root, 0, systems, true, plan);
        }
    }
    return plan;
}

} // namespace den::library::tidy
