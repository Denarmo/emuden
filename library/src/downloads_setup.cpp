// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Setting up one download: an archive is unpacked and what it holds is looked at; games are
// moved into their console's folder of the library (a cue sheet with its tracks, the discs of
// one game together), patches go to downloads_hacks.cpp.

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

#include "den/base/archive.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/library/content.h"
#include "den/library/names.h"
#include "den/library/scan.h"
#include "downloads_work.h"

namespace den::library::downloads {

namespace fs = std::filesystem;

namespace {

bool IsSheet(const std::string& extension) {
    return extension == "cue" || extension == "gdi" || extension == "m3u";
}

// The files a cue sheet, a GD-ROM list or a disc list names, as paths beside it.
std::vector<fs::path> Members(const fs::path& sheet) {
    std::vector<fs::path> out;
    if (ExtensionOf(sheet) == "gdi") {
        const auto text = ReadTextFile(sheet);
        std::size_t at = text ? text->find('\n') : std::string::npos; // the first line counts tracks
        while (text && at != std::string::npos && at < text->size()) {
            const std::size_t end = std::min(text->find('\n', at + 1), text->size());
            const std::string line = Trim(text->substr(at + 1, end - at - 1));
            at = end;
            // "1 0 4 2352 track01.bin 0" or a quoted name with spaces.
            std::string name;
            if (const std::size_t open = line.find('"'); open != std::string::npos) {
                name = line.substr(open + 1, line.find('"', open + 1) - open - 1);
            } else {
                std::size_t field = 0, start = 0;
                for (std::size_t i = 0; i <= line.size() && field < 5; ++i) {
                    if (i == line.size() || line[i] == ' ' || line[i] == '\t') {
                        if (i > start && ++field == 5) {
                            name = line.substr(start, i - start);
                        }
                        start = i + 1;
                    }
                }
            }
            if (!name.empty()) {
                out.push_back(sheet.parent_path() / PathFromUtf8(name));
            }
        }
        return out;
    }
    // FilesNamedBy gives lower-case keys: find each file by name in the sheet's folder.
    const std::vector<fs::path> beside = ListDirectory(sheet.parent_path());
    for (const std::string& named : FilesNamedBy(sheet)) {
        fs::path file = PathFromUtf8(named);
        for (const fs::path& candidate : beside) {
            if (Key(candidate) == Key(file) || ToLower(PathToUtf8(candidate.filename())) == ToLower(PathToUtf8(file.filename()))) {
                file = candidate;
            }
        }
        out.push_back(file);
    }
    return out;
}

void Collect(const fs::path& dir, std::vector<fs::path>& out) {
    for (const fs::path& path : ListDirectory(dir)) {
        // Hidden files and a Mac's "__MACOSX" folder and "._" resource forks are not games.
        const std::string name = PathToUtf8(path.filename());
        if (name.starts_with('.') || name == "__MACOSX") {
            continue;
        }
        if (IsDirectory(path)) {
            Collect(path, out);
        } else if (IsRegularFile(path)) {
            out.push_back(path);
        }
    }
}

} // namespace

std::string ExtensionOf(const fs::path& file) {
    const std::string extension = ToLower(PathToUtf8(file.extension()));
    return extension.empty() ? extension : extension.substr(1);
}

std::vector<fs::path> SheetMembers(const fs::path& sheet) {
    return Members(sheet);
}

std::string WorkDirName(const fs::path& file) {
    return ".unpacked-" + PathToUtf8(file.stem());
}

Status Unpack(const fs::path& archive, const fs::path& into, const std::function<bool()>& cancelled) {
    auto reader = ArchiveReader::Open(archive);
    if (!reader) {
        return Status::Fail("the archive cannot be opened (damaged, or its names are behind a password)");
    }
    std::error_code ec;
    fs::remove_all(into, ec);
    for (const ZipEntry& entry : reader->Entries()) {
        if (entry.symlink || !IsSafeRelativePath(entry.name)) {
            continue;
        }
        if (!CanExtractArchiveEntry(entry)) {
            return Status::Fail("the archive is packed in a way EmuDEN cannot unpack (a password, or an unusual method)");
        }
        const fs::path out = into / PathFromUtf8(entry.name);
        fs::create_directories(out.parent_path(), ec);
        if (Status done = reader->Extract(entry, out, cancelled); !done) {
            return done;
        }
    }
    return {};
}

bool MoveTo(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if (!ec) {
        return true;
    }
    fs::path partial = to;
    partial += ".part";
    fs::copy_file(from, partial, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
        fs::rename(partial, to, ec);
    }
    if (ec) {
        fs::remove(partial, ec);
        return false;
    }
    fs::remove(from, ec);
    return true;
}

fs::path FreeName(const fs::path& dir, const fs::path& name) {
    fs::path file = dir / name;
    for (int n = 2; fs::exists(file) && n < 1000; ++n) {
        file = dir / PathFromUtf8(std::format("{} ({}){}", PathToUtf8(name.stem()), n, PathToUtf8(name.extension())));
    }
    return file;
}

fs::path ConsoleDir(Library& library, const SystemTable& systems, const std::string& system,
                    const fs::path& data_dir, bool* made) {
    const std::vector<Folder> folders = library.Folders();
    for (const Folder& folder : folders) {
        if (folder.system == system && CanWriteHere(PathFromUtf8(folder.path))) {
            return PathFromUtf8(folder.path);
        }
    }
    for (const Folder& folder : folders) {
        const fs::path root = PathFromUtf8(folder.path);
        if (!folder.system.empty() || !IsDirectory(root)) {
            continue;
        }
        const fs::path named = root.has_filename() ? root : root.parent_path();
        if (const SystemDef* own = systems.FromFolderName(PathToUtf8(named.filename()))) {
            if (own->id == system && CanWriteHere(root)) {
                return root;
            }
            continue; // another console's folder
        }
        for (const fs::path& dir : ListDirectory(root)) {
            const SystemDef* def = IsDirectory(dir) ? systems.FromFolderName(PathToUtf8(dir.filename())) : nullptr;
            if (def != nullptr && def->id == system && CanWriteHere(dir)) {
                return dir;
            }
        }
        const fs::path dir = root / PathFromUtf8(system);
        const bool existed = IsDirectory(dir);
        if (CanWriteHere(dir)) {
            if (made != nullptr) {
                *made = !existed;
            }
            return dir;
        }
    }
    // No game folder that can be written: EmuDEN's own, added to the library once.
    const fs::path own = data_dir / "games";
    std::error_code ec;
    if (made != nullptr) {
        *made = !IsDirectory(own / PathFromUtf8(system));
    }
    fs::create_directories(own / PathFromUtf8(system), ec);
    library.AddFolder(PathToUtf8(own));
    return own / PathFromUtf8(system);
}

bool CanWriteHere(const fs::path& dir) {
    std::error_code ec;
    const bool existed = IsDirectory(dir);
    fs::create_directories(dir, ec);
    const fs::path probe = dir / ".emuden-write-test";
    bool ok = false;
    if (FILE* file = std::fopen(PathToUtf8(probe).c_str(), "wb")) {
        std::fclose(file);
        ok = true;
    }
    fs::remove(probe, ec);
    if (!ok && !existed) {
        fs::remove(dir, ec);
    }
    return ok;
}

std::vector<Entry> WaitForGames(Library& library, const std::vector<fs::path>& files,
                                const std::function<bool()>& cancelled) {
    std::vector<std::string> keys;
    for (const fs::path& file : files) {
        keys.push_back(Key(file));
    }
    library.Scan();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < until && !(cancelled && cancelled())) {
        std::vector<Entry> found;
        for (const Entry& entry : library.Entries()) {
            if (std::find(keys.begin(), keys.end(), Key(PathFromUtf8(entry.path))) != keys.end()) {
                found.push_back(entry);
            }
        }
        if (!found.empty()) {
            return found;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    return {};
}

// The games among `files` in groups that stay together, each with its console.
std::vector<GameGroup> GroupGames(const std::vector<fs::path>& files, const SystemTable& systems,
                                  const std::vector<std::string>& hints, std::vector<fs::path>& left) {
    std::vector<GameGroup> groups;
    std::vector<std::string> claimed;
    // Sheets first: they name their tracks.
    for (const fs::path& file : files) {
        if (!IsSheet(ExtensionOf(file))) {
            continue;
        }
        GameGroup group{{}, {file}, file, {}};
        for (const fs::path& member : Members(file)) {
            if (IsRegularFile(member)) {
                group.files.push_back(member);
            } else {
                group.missing.push_back(PathToUtf8(member.filename()));
            }
        }
        // Without its tracks nothing says which console it is for; it waits for them.
        const Identity identity = Identify(file, systems, hints);
        if (identity.kind != Kind::Game && group.missing.empty()) {
            continue;
        }
        group.system = identity.system;
        for (const fs::path& member : group.files) {
            claimed.push_back(Key(member));
        }
        groups.push_back(std::move(group));
    }
    // A sheet inside a disc list: one group with the list.
    for (GameGroup& outer : groups) {
        if (ExtensionOf(outer.main) != "m3u") {
            continue;
        }
        for (GameGroup& inner : groups) {
            if (&inner == &outer || inner.files.empty()) {
                continue;
            }
            if (std::any_of(outer.files.begin(), outer.files.end(), [&](const fs::path& f) { return Key(f) == Key(inner.main); })) {
                for (const fs::path& f : inner.files) {
                    if (std::none_of(outer.files.begin(), outer.files.end(), [&](const fs::path& o) { return Key(o) == Key(f); })) {
                        outer.files.push_back(f);
                    }
                }
                inner.files.clear();
            }
        }
    }
    std::erase_if(groups, [](const GameGroup& group) { return group.files.empty(); });
    for (const fs::path& file : files) {
        if (std::find(claimed.begin(), claimed.end(), Key(file)) != claimed.end()) {
            continue;
        }
        const Identity identity = Identify(file, systems, hints);
        if (identity.kind != Kind::Game) {
            left.push_back(file);
            continue;
        }
        // The discs of one game: one folder named after the set.
        const ParsedName parsed = ParseGameName(PathToUtf8(file.filename()));
        GameGroup* set = nullptr;
        if (parsed.disc != 0) {
            for (GameGroup& group : groups) {
                if (!group.set_name.empty() && group.system == identity.system && group.set_name == DiscSetName(file)) {
                    set = &group;
                }
            }
        }
        if (set != nullptr) {
            set->files.push_back(file);
        } else {
            groups.push_back({identity.system, {file}, file, {}, parsed.disc != 0 ? DiscSetName(file) : std::string()});
        }
    }
    for (GameGroup& group : groups) {
        if (group.set_name.empty() && group.files.size() > 1) {
            group.set_name = PathToUtf8(group.main.stem());
        }
    }
    return groups;
}

void CollectFiles(const fs::path& dir, std::vector<fs::path>& out) {
    Collect(dir, out);
    std::sort(out.begin(), out.end());
}

} // namespace den::library::downloads
