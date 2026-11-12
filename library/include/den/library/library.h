// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the game library. Screens use only this header. The implementation behind
// CreateLibrary is a fake (src/fake_library.cpp) until the real scanner replaces it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"

namespace den::store {
class Settings;
}

namespace den::library {

/// One game: a ROM for a core or a game for an out-of-app engine (Switch 2) alike.
struct Entry {
    std::string id;     // stable across scans and renames of the library folder
    std::string name;   // cleaned up for display ("Pokemon Emerald", no region tags)
    std::string system; // system id as plugin manifests use it: "gb", "gba", "nds", "psx", "switch2"
    std::string path;   // UTF-8 path of the file to start
    std::string cover;  // UTF-8 path of an image file; set for every entry once artwork ran
    std::uint64_t size_bytes = 0;
    bool favourite = false;
    std::int64_t last_played = 0;  // unix seconds, 0 = never
    std::int64_t play_seconds = 0; // total
    int play_count = 0;
};

struct SystemInfo {
    std::string id;
    std::string name;       // "Game Boy Advance"
    std::string short_name; // "GBA"
    int game_count = 0;
};

struct Folder {
    std::string path;   // UTF-8
    std::string system; // empty = decide per file from its extension and folder name
};

class Library {
public:
    virtual ~Library() = default;

    virtual std::vector<Folder> Folders() const = 0;
    virtual Status AddFolder(const std::string& path, const std::string& system = {}) = 0;
    virtual Status RemoveFolder(const std::string& path) = 0;

    /// Rescans every folder as a job with progress and cancel; artwork follows in the same
    /// job. The entries change while it runs, in steps.
    virtual JobPtr Scan() = 0;
    /// Grows whenever the entries or their records changed; screens rebuild when it moves.
    virtual std::uint64_t Revision() const = 0;

    /// Snapshots, sorted by name. Safe from any thread.
    virtual std::vector<Entry> Entries() const = 0;
    virtual std::optional<Entry> Find(const std::string& id) const = 0;
    /// The systems that have at least one game.
    virtual std::vector<SystemInfo> Systems() const = 0;
    virtual std::vector<Entry> RecentlyPlayed(int limit) const = 0;
    virtual std::vector<Entry> Favourites() const = 0;

    virtual void SetFavourite(const std::string& id, bool favourite) = 0;
    /// Adds one play session to the entry's records.
    virtual void RecordPlay(const std::string& id, std::int64_t started_unix, std::int64_t seconds) = 0;
};

struct Config {
    std::filesystem::path data_dir; // the library keeps its files under data_dir / "library"
    Jobs* jobs = nullptr;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Library> CreateLibrary(const Config& config);

} // namespace den::library
