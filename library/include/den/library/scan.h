// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Finds the games in the library folders. Reads the disk: run it from a job.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/library/library.h"
#include "den/library/systems.h"

namespace den::library {

struct ScanOptions {
    /// A folder with a system holds that console's games. One without holds system folders
    /// ("gba", "PSX", "Game Boy Advance", ...) and ROM files whose extension names one console;
    /// when its own name is a console's, it is that console's folder.
    std::vector<Folder> folders;
    const SystemTable* systems = nullptr; // null = the built-in consoles
    /// Optional box art of the player: <covers_dir>/<system id>/<rom name>.png (or .jpg).
    /// Images named like the ROM next to it, or in a "media", "covers", "boxart" or "images"
    /// folder beside it, also count.
    std::filesystem::path covers_dir;
    /// Where the icons Switch 2 games carry are written (<icons_dir>/<id>.jpg); none when empty.
    std::filesystem::path icons_dir;
    /// Regional versions of one game become one entry, the first region of this order shown.
    /// Empty = every file is an entry.
    std::vector<std::string> region_order;
    /// Asked between files; the scan stops early (with what it has) once it returns true.
    std::function<bool()> cancelled;
};

/// A game is listed once: by its playlist (.m3u) and not by the discs that names, by its cue
/// sheet and not by the images that describes, and a zip archive only when the same game is
/// not there unpacked. Entries have no play records and a cover only when one is on disk.
std::vector<Entry> ScanFolders(const ScanOptions& options);

/// The id of a ROM: "<system>:<hash>" of its path below the console's folder, any case. It
/// stays the same when the library folder is renamed or moved.
std::string RomId(std::string_view system, std::string_view relative_path);
/// "switch2:<16 hex digits>".
std::string Switch2Id(std::uint64_t program_id);

/// The lines of a playlist or the FILE entries of a cue sheet: the files it stands for, as
/// lower-case normal paths.
std::vector<std::string> FilesNamedBy(const std::filesystem::path& list);

// Switch 2 game files.

struct Switch2Info {
    std::uint64_t program_id = 0;
    std::string title; // the game's own title, "" when the file carries none
    std::string icon;  // image bytes (JPEG or PNG), empty when there is none or it was not asked
};

/// What a Switch 2 package (.nup) says about itself; nullopt for other files.
std::optional<Switch2Info> ReadSwitch2Package(const std::filesystem::path& file, bool with_icon);
/// The program id a file name carries in brackets ("Game [0400486024DEC000].nsp"), 0 if none.
std::uint64_t ProgramIdFromName(std::string_view file_name);
/// The title of a known program id, "" otherwise.
std::string_view KnownSwitch2Title(std::uint64_t program_id);

} // namespace den::library
