// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Setting up what the user downloaded: a file in EmuDEN's Downloads folder is looked at
// (unpacked first when it is an archive) and becomes a game of the library, a ROM hack of one
// of the user's games, or stays where it is with the reason. BIOS files are never used.
// See docs/in-app-browser.md, "Browser plugin".

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "den/base/result.h"
#include "den/library/library.h"
#include "den/library/systems.h"

namespace den::library::downloads {

enum class Kind { Game, Patch, Archive, Bios, Unknown };

struct Identity {
    Kind kind = Kind::Unknown;
    std::string system; // for a game: the console it is for
    std::string why;    // for Unknown: what is missing, for the user
};

/// What a file is, from its name, its first bytes and `hints` (the folders it was in inside an
/// archive, the archive's name, the title of the page it came from) for a file that more than
/// one console could take. `name` is the file's name ("" = the file's own).
Identity Identify(const std::filesystem::path& file, const SystemTable& systems,
                  const std::vector<std::string>& hints = {}, const std::string& name = {});
/// A BIOS or firmware file by its name ("scph1001.bin", "gba_bios.bin", "[BIOS] ...").
bool IsBiosName(const std::string& file_name);
/// The console a game file's first bytes say it is for, among `candidates` (all when empty);
/// "" when they say nothing.
std::string SniffSystem(const std::filesystem::path& file, const std::vector<std::string>& candidates);

/// The name of a hack, from the title of the page it was downloaded from and the file's name:
/// without its version, the site's name and file-name noise.
std::string HackTitle(const std::string& page_title, const std::string& file_name);
/// "1.2.1" from "v1.2.1", "Version 1.2", "_v12"; "" when neither text has one.
std::string HackVersion(const std::string& page_title, const std::string& file_name);
/// How well a game's name fits a text (the hack's title and file name): the number of the
/// game's own words the text has, 0 for none. "Fire Red" fits "FireRed".
int NameScore(const std::string& game_name, const std::string& text);

enum class Outcome {
    Added,       // games went into the library
    HackAdded,   // a patch was applied to one of the user's games
    NeedsBase,   // a patch without a checksum: the user picks the game (`choices`)
    MissingBase, // a patch for a game the library does not have
    Waiting,     // a part of a set (a cue sheet without its tracks): kept until the rest arrives
    Bios,        // left alone: EmuDEN does not use BIOS files
    Unknown,     // left in Downloads
    Failed,
};
const char* OutcomeName(Outcome outcome);

struct Request {
    std::filesystem::path file; // in the Downloads folder
    std::string page_title;     // the page the download started on, "" when not known
    std::string host;           // where it came from
    std::string base_id;        // the game the user chose after NeedsBase
    std::string plugin;         // who asks; recorded on the hacks it makes
};

struct AddedGame {
    std::string id, name, system;
};

/// One setup, as the Downloads page lists it.
struct Record {
    std::string id;
    std::int64_t time = 0;
    Outcome outcome = Outcome::Unknown;
    std::string download; // the file's name in the Downloads folder
    std::string message;  // what the user is told
    std::string system_name;
    std::vector<AddedGame> games; // what went into the library (hacks too)
    /// Moved files, for undo: (name in the Downloads folder, path in the library).
    std::vector<std::pair<std::string, std::string>> moved;
    std::vector<std::string> hacks;   // ids of the hacks made
    std::vector<std::string> folders; // folders made for sets, removed by undo when empty
    bool replaced = false;          // a hack of the same name was there and was updated
    bool undone = false;
};

struct Outcomes {
    Record record;
    std::vector<Entry> choices; // NeedsBase: the games it may be for, best first
};

class Setup {
public:
    /// `downloads_dir` is EmuDEN's Downloads folder; the history is kept in `data_dir`.
    Setup(Library& library, std::filesystem::path downloads_dir, std::filesystem::path data_dir);

    /// Blocking: unpacks, hashes, patches, moves and waits for the library to list the
    /// result. One at a time; call from a worker.
    Outcomes Run(const Request& request, const std::function<bool()>& cancelled = {});
    /// Newest first.
    std::vector<Record> History() const;
    /// Takes a setup back: games go back to the Downloads folder, hacks are removed (their
    /// saves stay). Blocking.
    Status Undo(const std::string& record_id);
    const std::filesystem::path& Folder() const {
        return downloads_;
    }

private:
    friend struct SetupWork;
    void Save() const;

    Library& library_;
    std::filesystem::path downloads_, history_file_, data_dir_;
    mutable std::mutex mutex_; // the history
    std::mutex running_;       // one setup or undo at a time
    std::vector<Record> history_;
};

} // namespace den::library::downloads
