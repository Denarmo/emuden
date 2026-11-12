// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the game library. Screens use only this header.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
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
    std::int64_t added = 0; // unix seconds the library first saw the game, 0 = not known
    /// The name as found (a ROM's file name without extension); artwork is matched on it.
    std::string original_name;
    /// For a ROM packed in a zip archive (`path` is the archive): the file in it to start.
    /// PrepareRom (content.h) unpacks it.
    std::string archive_member;
    int disc = 0; // disc number of a multi-disc game, 0 otherwise
    /// `cover` is a drawn placeholder (colour and initials): print the name over it.
    bool placeholder_cover = false;
    /// Files of the same game in other regions, hidden behind this entry.
    std::vector<std::string> other_versions;
};

struct SystemInfo {
    std::string id;
    std::string name;       // "Game Boy Advance"
    std::string short_name; // "GBA"
    int game_count = 0;
};

struct SystemDef;

/// One time a game was played, as RecordPlay booked it.
struct Session {
    std::string id; // the entry's id
    std::int64_t started = 0; // unix seconds
    std::int64_t seconds = 0;
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
    /// The games the library saw first most recently, newest first.
    virtual std::vector<Entry> RecentlyAdded(int limit) const = 0;
    /// The session log, oldest first; with `since_unix`, the sessions that ended after it.
    virtual std::vector<Session> Sessions(std::int64_t since_unix = 0) const = 0;

    virtual void SetFavourite(const std::string& id, bool favourite) = 0;
    /// Adds one play session to the entry's records and the session log. Time the log already
    /// holds for another game inside this session (a game opened over this one) is left out.
    virtual void RecordPlay(const std::string& id, std::int64_t started_unix, std::int64_t seconds) = 0;

    /// Consoles from plugin manifests (systems.h). They replace the built-in rules of the same
    /// id and add new ones. The plugin host or the app calls this; the next scan uses them.
    virtual void SetSystems(std::vector<SystemDef> systems) = 0;
    /// Adds the host folders an earlier app's profile lists as game folders (its
    /// config/qt-config.ini, read only). Fails when there is no such profile.
    virtual Status ImportFolders(const std::filesystem::path& old_profile) = 0;
};

/// An answer to a GET: status 0 when the request never got one.
struct HttpAnswer {
    int status = 0;
    std::string body;
};
using HttpFetcher = std::function<HttpAnswer(const std::string& url)>;

struct Config {
    std::filesystem::path data_dir; // the library keeps its files under data_dir / "library"
    Jobs* jobs = nullptr;
    store::Settings* settings = nullptr;
    /// Cover downloads. Empty = the platform's HTTP client; with `offline` nothing is fetched.
    HttpFetcher http;
    bool offline = false;
    /// On the first run (no library file yet) the game folders of this old profile are taken
    /// over. Empty = the folder the environment variable EMUDEN_IMPORT_PROFILE names, if any.
    std::filesystem::path import_profile;
};

std::unique_ptr<Library> CreateLibrary(const Config& config);

} // namespace den::library
