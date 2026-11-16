// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Carrying a tidy-up's plan out, its log, and taking the last one back (den/library/tidy.h).

#include <chrono>
#include <format>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/library/scan.h"
#include "den/library/tidy.h"
#include "hacks_store.h"

namespace den::library::tidy {

namespace fs = std::filesystem;

namespace {

fs::path LogFile(const fs::path& data_dir) {
    return data_dir / "library" / "tidy-log.json";
}

// The player's name for a game's files: the id with the characters file systems refuse
// replaced (player/src/game_files.cpp).
std::string FileBase(const std::string& id) {
    std::string out;
    for (const char c : id) {
        out.push_back(static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos
                          ? '_'
                          : c);
    }
    return out;
}

// The id a game gets when its file is renamed: the scan's, from the path below the library
// folder its old id was made from.
std::string NewId(const std::vector<Folder>& folders, const std::string& system, const std::string& old_id,
                  const fs::path& from, const fs::path& to) {
    for (const Folder& folder : folders) {
        const fs::path root = PathFromUtf8(folder.path);
        const fs::path below = from.lexically_relative(root);
        if (below.empty() || *below.begin() == "..") {
            continue;
        }
        if (RomId(system, PathToUtf8(below)) == old_id) {
            return RomId(system, PathToUtf8(to.lexically_relative(root)));
        }
    }
    return RomId(system, PathToUtf8(to.filename()));
}

// The cover beside the ROM and the library's own cover go with the new name.
void MoveCovers(const fs::path& library_dir, const std::string& system, const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (const fs::path cover = CoverBeside(from); !cover.empty()) {
        fs::rename(cover, to.parent_path() / PathFromUtf8(PathToUtf8(to.stem()) + PathToUtf8(cover.extension())), ec);
    }
    const fs::path own = library_dir / "covers" / PathFromUtf8(system);
    for (const char* extension : {".png", ".jpg", ".jpeg", ".webp"}) {
        const fs::path old = own / PathFromUtf8(PathToUtf8(from.stem()) + extension);
        if (IsRegularFile(old)) {
            fs::rename(old, own / PathFromUtf8(PathToUtf8(to.stem()) + extension), ec);
        }
    }
}

struct Renamed {
    std::string system, old_id, new_id;
    fs::path from, to;
};

Status RenameGame(Library& library, const fs::path& data_dir, const Renamed& r, const MovedFn& moved) {
    std::error_code ec;
    fs::rename(r.from, r.to, ec);
    if (ec) {
        return Status::Fail(std::format("{} could not be renamed: {}", PathToUtf8(r.from.filename()), ec.message()));
    }
    MoveCovers(data_dir / "library", r.system, r.from, r.to);
    library.MoveRecord(r.old_id, r.new_id);
    MoveGameFiles(data_dir, r.system, r.old_id, r.new_id);
    if (moved) {
        moved({r.system, r.old_id, r.new_id});
    }
    return {};
}

} // namespace

void MoveGameFiles(const fs::path& data_dir, const std::string& system, const std::string& old_id,
                   const std::string& new_id) {
    const std::string old_base = FileBase(old_id) + ".", new_base = FileBase(new_id) + ".";
    for (const char* kind : {"saves", "states"}) {
        const fs::path dir = data_dir / kind / PathFromUtf8(system);
        for (const fs::path& file : ListDirectory(dir)) {
            const std::string name = PathToUtf8(file.filename());
            if (name.starts_with(old_base) && !fs::exists(dir / PathFromUtf8(new_base + name.substr(old_base.size())))) {
                std::error_code ec;
                fs::rename(file, dir / PathFromUtf8(new_base + name.substr(old_base.size())), ec);
            }
        }
    }
}

Done Apply(Library& library, const Plan& plan, const fs::path& data_dir, const MovedFn& moved,
           const std::function<void(float)>& progress) {
    Done done;
    const std::vector<Folder> folders = library.Folders();
    JsonValue renames = JsonArray(), names = JsonArray(), removed = JsonArray();
    for (std::size_t i = 0; i < plan.changes.size(); ++i) {
        const Change& change = plan.changes[i];
        std::error_code ec;
        switch (change.kind) {
        case ChangeKind::Rename: {
            const Renamed r{change.system, change.id, NewId(folders, change.system, change.id, change.from, change.to),
                            change.from, change.to};
            if (fs::exists(r.to)) {
                done.errors.push_back(PathToUtf8(r.to.filename()) + " is there already");
                break;
            }
            if (Status renamed = RenameGame(library, data_dir, r, moved); !renamed) {
                done.errors.push_back(renamed.error);
                break;
            }
            ++done.renamed;
            renames.items.push_back(JsonObject({{"system", JsonString(r.system)}, {"old_id", JsonString(r.old_id)},
                                                {"new_id", JsonString(r.new_id)}, {"from", JsonString(PathToUtf8(r.from))},
                                                {"to", JsonString(PathToUtf8(r.to))}}));
            log::Info("library", "tidy: {} -> {}", PathToUtf8(r.from), PathToUtf8(r.to.filename()));
            break;
        }
        case ChangeKind::ShowName:
            library.SetDisplayName(change.id, change.name);
            ++done.named;
            names.items.push_back(JsonObject({{"id", JsonString(change.id)}}));
            break;
        case ChangeKind::RemoveJunk:
        case ChangeKind::RemoveFolder:
            if (fs::remove(change.from, ec) && !ec) {
                ++done.removed;
                removed.items.push_back(JsonString(PathToUtf8(change.from)));
            } else if (ec) {
                done.errors.push_back(PathToUtf8(change.from) + ": " + ec.message());
            }
            break;
        }
        if (progress) {
            progress(static_cast<float>(i + 1) / static_cast<float>(plan.changes.size()));
        }
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
    std::error_code ec;
    fs::create_directories(LogFile(data_dir).parent_path(), ec);
    WriteFileAtomic(LogFile(data_dir),
                    WriteJson(JsonObject({{"time", JsonNumber(static_cast<double>(now.count()))},
                                          {"renamed", std::move(renames)}, {"named", std::move(names)},
                                          {"removed", std::move(removed)}})));
    return done;
}

bool CanUndo(const fs::path& data_dir) {
    return IsRegularFile(LogFile(data_dir));
}

Status Undo(Library& library, const fs::path& data_dir, const MovedFn& moved) {
    const auto text = ReadTextFile(LogFile(data_dir));
    const auto root = text ? ParseJson(*text) : std::nullopt;
    if (!root) {
        return Status::Fail("There is no tidy-up to take back");
    }
    std::string failed;
    if (const JsonValue* renamed = root->Find("renamed")) {
        for (auto it = renamed->items.rbegin(); it != renamed->items.rend(); ++it) {
            const Renamed back{it->String("system"), it->String("new_id"), it->String("old_id"),
                               PathFromUtf8(it->String("to")), PathFromUtf8(it->String("from"))};
            if (fs::exists(back.to) || !IsRegularFile(back.from)) {
                failed = PathToUtf8(back.from.filename());
                continue;
            }
            if (Status done = RenameGame(library, data_dir, back, moved); !done) {
                failed = done.error;
            }
        }
    }
    if (const JsonValue* named = root->Find("named")) {
        for (const JsonValue& item : named->items) {
            library.SetDisplayName(item.String("id"), {});
        }
    }
    std::error_code ec;
    fs::remove(LogFile(data_dir), ec);
    return failed.empty() ? Status{} : Status::Fail("Not everything could be renamed back: " + failed);
}

} // namespace den::library::tidy
