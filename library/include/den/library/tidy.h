// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Tidying the library up: the clean names of the download setup (downloads.h GameName) over
// every game already there, and the junk a Mac leaves in game folders. A plan is made first
// and shown; applying it renames files, moves what is kept by a game's id (play time,
// favourite, saves, states, the game's settings through a callback) to the new id, and
// keeps a log so the last tidy-up can be taken back.
//
// A file whose saves, states or settings sync is never renamed: sync names them after the
// ROM's file name on every device (docs/sync.md section 2), so a new name would leave the
// copies online behind. Such a game, and one in a folder that cannot be written, only gets
// its clean name shown. BIOS files, packed games, cue sheets and their tracks, discs of a set,
// hacks (named by EmuDEN already) and anything outside the library folders are left alone.
//
// A game whose header says it was made from another game in the library (rom_codes.h
// FindHackBase) is linked to it as a hack where it is: its file does not move.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "den/base/result.h"
#include "den/library/library.h"

namespace den::library::tidy {

enum class ChangeKind {
    Rename,      // the file gets the clean name
    ShowName,    // only the name shown changes (synced, or a folder that cannot be written)
    RemoveJunk,  // a Mac's "._" resource fork or ".DS_Store"
    RemoveFolder, // an empty folder in a game folder (not a console's folder)
    LinkHack      // a hack patched before it came, shown with the game it was made from
};

struct Change {
    ChangeKind kind = ChangeKind::Rename;
    std::string id, system;      // the game's (Rename, ShowName)
    std::filesystem::path from;  // the file or folder
    std::filesystem::path to;    // Rename: the new file
    std::string name;            // Rename, ShowName: the clean name
    std::string why;             // ShowName: why the file keeps its name
    std::string base_id;         // LinkHack: the game it is a hack of (`name` is that game's)
};

struct Plan {
    std::vector<Change> changes;
    int games = 0; // games looked at
};

/// The ROM stems that sync, as "<system>/<stem>" in lower case, from the sync journals in
/// <data>/sync/<provider>/journal.json.
std::vector<std::string> SyncedStems(const std::filesystem::path& data_dir);

/// What tidying would change. Reads files, changes nothing.
Plan MakePlan(Library& library, const std::filesystem::path& data_dir);

/// A game that went from one id to another, for what the caller keeps by id (its settings).
struct Moved {
    std::string system, old_id, new_id;
};
using MovedFn = std::function<void(const Moved&)>;

struct Done {
    int renamed = 0, named = 0, removed = 0, linked = 0;
    std::vector<std::string> errors;
};

/// Carries the plan out and writes <data>/library/tidy-log.json. `progress` gets 0..1.
/// Blocking: call from a worker; the caller scans the library afterwards.
Done Apply(Library& library, const Plan& plan, const std::filesystem::path& data_dir, const MovedFn& moved,
           const std::function<void(float)>& progress = {});
/// Whether a tidy-up can be taken back (its log is there).
bool CanUndo(const std::filesystem::path& data_dir);
/// Renames back and shows the files' own names again; the removed junk stays gone.
Status Undo(Library& library, const std::filesystem::path& data_dir, const MovedFn& moved);

/// Moves <data>/saves/<system>/<old>.* and <data>/states/<system>/<old>.* (the player names
/// a game's files after its id) to the new id.
void MoveGameFiles(const std::filesystem::path& data_dir, const std::string& system, const std::string& old_id,
                   const std::string& new_id);

} // namespace den::library::tidy
