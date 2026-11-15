// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared by the files of the download setup (downloads_*.cpp).

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "den/library/downloads.h"

namespace den::library::downloads {

/// Games that are set up together: a cue sheet and its tracks, the discs of one game.
struct GameGroup {
    std::string system;
    std::vector<std::filesystem::path> files;
    std::filesystem::path main;       // the file the library lists (the sheet, the first disc)
    std::vector<std::string> missing; // files the sheet names that are not there
    std::string set_name;             // the folder they share; "" for one file on its own
};

/// One run of Setup::Run.
struct SetupWork {
    Setup& setup;
    const Request& request;
    const std::function<bool()>& cancelled;
    SystemTable systems;
    Outcomes out;
    std::vector<std::string> hints;

    void Run();
    void SetUpFiles(const std::vector<std::filesystem::path>& files, const std::filesystem::path& base,
                    bool from_archive);
    void SetUpGames(std::vector<GameGroup> groups, const std::filesystem::path& base);
    void SetUpPatch(const std::vector<std::filesystem::path>& patches);
    bool Cancelled() const {
        return cancelled && cancelled();
    }
};

std::string ExtensionOf(const std::filesystem::path& file);
/// A lower-case normal path, for comparing.
std::string Key(const std::filesystem::path& path);
/// A cue sheet, GD-ROM list or disc list that exists.
bool IsSheetFile(const std::filesystem::path& file);
std::vector<std::filesystem::path> SheetMembers(const std::filesystem::path& sheet);
/// ".unpacked-<stem>", the folder of the Downloads folder an archive is unpacked into.
std::string WorkDirName(const std::filesystem::path& file);
Status Unpack(const std::filesystem::path& archive, const std::filesystem::path& into,
              const std::function<bool()>& cancelled);
/// Renames, or copies and removes across volumes.
bool MoveTo(const std::filesystem::path& from, const std::filesystem::path& to);
/// `dir/name`, or "name (2)" and so on when that is taken.
std::filesystem::path FreeName(const std::filesystem::path& dir, const std::filesystem::path& name);
bool CanWriteHere(const std::filesystem::path& dir);
/// The library's folder for a console's games that can be written: a folder of that
/// console, a system folder of a game folder (made when missing), else EmuDEN's own
/// `<data>/games/<system>`, which is then added to the library.
std::filesystem::path ConsoleDir(Library& library, const SystemTable& systems, const std::string& system,
                                 const std::filesystem::path& data_dir);
/// Starts a scan and waits until the library lists games of these files.
std::vector<Entry> WaitForGames(Library& library, const std::vector<std::filesystem::path>& files,
                                const std::function<bool()>& cancelled);
std::vector<GameGroup> GroupGames(const std::vector<std::filesystem::path>& files, const SystemTable& systems,
                                  const std::vector<std::string>& hints, std::vector<std::filesystem::path>& left);
/// Every file below `dir`, sorted.
void CollectFiles(const std::filesystem::path& dir, std::vector<std::filesystem::path>& out);

} // namespace den::library::downloads
