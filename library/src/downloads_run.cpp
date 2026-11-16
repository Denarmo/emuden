// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One download from start to end: what it is, then games into the library, a patch onto one
// of the user's games, or the file left in Downloads with the reason.

#include <algorithm>
#include <format>

#include "den/base/archive.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/library/names.h"
#include "downloads_work.h"
#include "hacks_store.h"

namespace den::library::downloads {

namespace fs = std::filesystem;

namespace {

constexpr const char* BIOS_TEXT = "EmuDEN doesn't use BIOS files; it stays in Downloads";

std::string ConsoleName(const SystemTable& systems, const std::string& id) {
    const SystemDef* def = systems.Find(id);
    return def != nullptr ? def->name : id;
}

// A name that can be a folder: no separators or characters Windows refuses.
std::string FolderName(const std::string& name) {
    return SafeFileName(name, {.replacement = ' ', .trim_front = " ", .max_bytes = 0, .fallback = "Game"});
}

bool SameSize(const fs::path& a, const fs::path& b) {
    std::error_code ec, ec2;
    return fs::file_size(a, ec) == fs::file_size(b, ec2) && !ec && !ec2;
}

} // namespace

void SetupWork::Run() {
    Record& record = out.record;
    const fs::path& file = request.file;
    if (!IsRegularFile(file)) {
        record.outcome = Outcome::Failed;
        record.message = record.download + " is not in Downloads any more";
        return;
    }
    if (!request.page_title.empty()) {
        hints.push_back(request.page_title);
    }
    if (ExtensionOf(file) == "rar") {
        record.outcome = Outcome::Unknown;
        record.message = record.download + " was not added: EmuDEN can't open rar archives. It is in EmuDEN's Downloads folder";
        return;
    }
    const Identity identity = Identify(file, systems, hints);
    switch (identity.kind) {
    case Kind::Bios:
        record.outcome = Outcome::Bios;
        record.message = record.download + ": " + BIOS_TEXT;
        return;
    case Kind::Patch:
        SetUpPatch({file});
        return;
    case Kind::Archive: {
        const fs::path work = setup.downloads_ / PathFromUtf8(WorkDirName(file));
        const auto members = ListArchive(file);
        if (members && !members->empty() &&
            std::all_of(members->begin(), members->end(),
                        [](const ZipEntry& entry) { return IsBiosName(PathToUtf8(PathFromUtf8(entry.name).filename())); })) {
            record.outcome = Outcome::Bios;
            record.message = record.download + " holds BIOS files. " + BIOS_TEXT;
            return;
        }
        if (Status unpacked = Unpack(file, work, cancelled); !unpacked) {
            record.outcome = Outcome::Failed;
            record.message = record.download + " was not added: " + unpacked.error + ". It is in EmuDEN's Downloads folder";
            std::error_code ec;
            fs::remove_all(work, ec);
            return;
        }
        hints.push_back(PathToUtf8(file.stem()));
        std::vector<fs::path> files;
        CollectFiles(work, files);
        for (const fs::path& inner : files) {
            for (fs::path dir = inner.parent_path(); dir != work && dir.has_parent_path(); dir = dir.parent_path()) {
                hints.push_back(PathToUtf8(dir.filename()));
            }
        }
        SetUpFiles(files, work, true);
        std::error_code ec;
        fs::remove_all(work, ec);
        // The games are in the library now; the archive would only be a second copy.
        if (record.outcome == Outcome::Added) {
            fs::remove(file, ec);
        }
        return;
    }
    case Kind::Game:
    case Kind::Unknown:
        SetUpFiles({file}, setup.downloads_, false);
        return;
    }
}

void SetupWork::SetUpFiles(const std::vector<fs::path>& files, const fs::path& base, bool from_archive) {
    Record& record = out.record;
    std::vector<fs::path> others, patches;
    int bios = 0;
    std::vector<fs::path> queue = files;
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const fs::path file = queue[i];
        const Identity identity = Identify(file, systems, hints);
        if (identity.kind == Kind::Bios) {
            ++bios;
        } else if (identity.kind == Kind::Patch) {
            patches.push_back(file);
        } else if (identity.kind == Kind::Archive && from_archive && PathKey(file.parent_path()) == PathKey(base)) {
            // An archive in the archive, one level deep.
            const fs::path inner = file.parent_path() / PathFromUtf8(WorkDirName(file));
            if (Unpack(file, inner, cancelled)) {
                std::vector<fs::path> more;
                CollectFiles(inner, more);
                queue.insert(queue.end(), more.begin(), more.end());
            }
        } else {
            others.push_back(file);
        }
    }
    // A track that arrived after its cue sheet: the sheet in Downloads is the game.
    if (!from_archive && others.size() == 1 && !IsSheetFile(others.front())) {
        for (const fs::path& sheet : ListDirectory(setup.downloads_)) {
            if (!IsSheetFile(sheet)) {
                continue;
            }
            for (const fs::path& member : SheetMembers(sheet)) {
                if (PathKey(member) == PathKey(others.front())) {
                    others = {sheet};
                    record.download = PathToUtf8(sheet.filename());
                }
            }
        }
    }
    std::vector<fs::path> left;
    std::vector<GameGroup> groups = GroupGames(others, systems, hints, left);
    if (!groups.empty()) {
        SetUpGames(std::move(groups), base);
        if (bios > 0 && record.outcome == Outcome::Added) {
            record.message += std::format(". Left out {} BIOS file{}: EmuDEN doesn't use them", bios, bios == 1 ? "" : "s");
        }
    } else if (!patches.empty()) {
        SetUpPatch(patches);
    } else if (bios > 0) {
        record.outcome = Outcome::Bios;
        record.message = record.download + (from_archive ? " holds BIOS files. " : ": ") + BIOS_TEXT;
    } else {
        record.outcome = Outcome::Unknown;
        const Identity identity = left.size() == 1 ? Identify(left.front(), systems, hints) : Identity{};
        const std::string why = !identity.why.empty() ? identity.why : "nothing in it is a game or a patch EmuDEN knows";
        record.message = record.download + " was not added: " + why + ". It is in EmuDEN's Downloads folder";
    }
}

bool IsSheetFile(const fs::path& file) {
    const std::string extension = ExtensionOf(file);
    return (extension == "cue" || extension == "gdi" || extension == "m3u") && IsRegularFile(file);
}

void SetupWork::SetUpGames(std::vector<GameGroup> groups, const fs::path& base) {
    Record& record = out.record;
    std::vector<fs::path> mains;
    std::vector<std::string> already, waiting;
    std::vector<std::pair<Entry, std::string>> hacks; // already patched ROMs and their bases' names
    for (GameGroup& group : groups) {
        if (!group.missing.empty()) {
            waiting.push_back(PathToUtf8(group.main.filename()) + " (missing " + group.missing.front() +
                              (group.missing.size() > 1 ? std::format(" and {} more", group.missing.size() - 1) : "") + ")");
            continue;
        }
        bool made = false;
        fs::path dir = ConsoleDir(setup.library_, systems, group.system, setup.data_dir_, &made);
        if (made) {
            record.folders.push_back(PathToUtf8(dir));
        }
        if (!group.set_name.empty()) {
            dir /= PathFromUtf8(FolderName(group.set_name));
            if (!IsDirectory(dir)) {
                record.folders.push_back(PathToUtf8(dir));
            }
        }
        const fs::path sheet_dir = group.main.parent_path();
        // A game of one file is filed under its clean name; a sheet's files keep the names it lists.
        // A hack of a game in the library goes with it, as the hacks made from patches do.
        fs::path clean;
        if (group.files.size() == 1 && !IsSheetFile(group.main)) {
            NameSources sources;
            sources.file_name = PathToUtf8(group.main.filename());
            sources.page_title = groups.size() == 1 ? request.page_title : std::string();
            sources.archive_name = PathKey(base) != PathKey(setup.downloads_) ? record.download : std::string();
            sources.header_title = HeaderName(HeaderTitle(group.main, group.system));
            const std::string name = GameName(sources);
            if (auto hack = SetUpPatchedRom(group, name)) {
                hacks.push_back(std::move(*hack));
                continue;
            }
            if (!TidyFileName(PathToUtf8(group.main.filename()))) {
                clean = PathFromUtf8(HackFileName(name) + PathToUtf8(group.main.extension()));
            }
        }
        fs::path main;
        bool moved_any = false;
        for (const fs::path& file : group.files) {
            const fs::path relative = file.lexically_relative(sheet_dir);
            fs::path target = !clean.empty()                                   ? dir / clean
                              : !relative.empty() && *relative.begin() != ".." ? dir / relative
                                                                               : dir / file.filename();
            if (IsRegularFile(target)) {
                if (SameSize(file, target)) {
                    if (file == group.main) {
                        already.push_back(PathToUtf8(file.filename()));
                    }
                    continue;
                }
                target = FreeName(target.parent_path(), target.filename());
            }
            if (!MoveTo(file, target)) {
                record.outcome = Outcome::Failed;
                record.message = "Could not move " + PathToUtf8(file.filename()) + " to " + PathToUtf8(target.parent_path()) +
                                 ". It is in EmuDEN's Downloads folder";
                return;
            }
            moved_any = true;
            record.moved.push_back({PathToUtf8(file.filename()), PathToUtf8(target)});
            if (file == group.main) {
                main = target;
            }
        }
        if (moved_any && !main.empty()) {
            mains.push_back(main);
            record.system_name = ConsoleName(systems, group.system);
        }
    }
    for (const auto& [hack, base_name] : hacks) {
        record.games.push_back({hack.id, hack.name, hack.system});
        record.system_name = ConsoleName(systems, hack.system);
    }
    if (mains.empty() && !hacks.empty()) {
        record.outcome = Outcome::HackAdded;
        record.message = hacks.size() == 1
                             ? std::format("{} has been added to the library, made from {}", hacks[0].first.name, hacks[0].second)
                             : std::format("{} hacks have been added to the library: {}", hacks.size(), hacks[0].first.name);
        log::Info("downloads", "{}", record.message);
        return;
    }
    if (mains.empty()) {
        record.outcome = !waiting.empty() ? Outcome::Waiting : Outcome::Unknown;
        record.message = !waiting.empty() ? "Waiting for the rest of " + waiting.front() + "; it stays in Downloads"
                         : !already.empty() ? already.front() + " is already in your library"
                                            : record.download + " was not added. It is in EmuDEN's Downloads folder";
        return;
    }
    const std::vector<Entry> entries = WaitForGames(setup.library_, mains, cancelled);
    for (const Entry& entry : entries) {
        record.games.push_back({entry.id, entry.name, entry.system});
    }
    record.outcome = Outcome::Added;
    if (record.games.empty()) {
        // The scan did not list them in time; they are in place and show up with the next one.
        record.message = ParseGameName(PathToUtf8(mains.front().stem())).base + " has been added to the library";
    } else if (record.games.size() == 1) {
        record.message = record.games.front().name + " has been added to the library";
    } else if (record.games.size() == 2) {
        record.message = record.games[0].name + " and " + record.games[1].name + " have been added to the library";
    } else {
        record.message = std::format("{} games have been added to the library: {}", record.games.size(),
                                     record.games.front().name);
        for (std::size_t i = 1; i < record.games.size() && i < 4; ++i) {
            record.message += ", " + record.games[i].name;
        }
    }
    log::Info("downloads", "{}", record.message);
}

} // namespace den::library::downloads
