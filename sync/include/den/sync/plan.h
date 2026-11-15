// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pieces of one sync run (docs/sync.md, section 3): the catalog of local files, the
// journal of what both sides last agreed on, and the plan that compares them with the
// provider's listing.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "den/base/result.h"
#include "den/sync/names.h"
#include "den/sync/provider.h"

namespace den::store {
class Settings;
}
namespace den::system {
class SettingsStore;
}

namespace den::sync {

/// A local file under its portable name. Config files are made from the settings (`text`).
struct LocalItem {
    std::string path;
    std::filesystem::path file; // empty for a config file
    std::string text;           // a config file's content
    std::uint64_t size = 0;
    std::int64_t modified = 0; // Unix seconds
    std::string hash;          // SHA-256, lower-case hex
    std::string game_id;       // the game a save, state or game config belongs to
};
using Catalog = std::map<std::string, LocalItem>;

/// The settings as config files (`config/...`) and their application. The real one is over
/// the app's settings (config_files.cpp); tests use another.
class ConfigFiles {
public:
    virtual ~ConfigFiles() = default;
    virtual std::vector<LocalItem> Export(const GameIndex& games) = 0;
    /// Applies a downloaded config file. Device-only settings stay as they are.
    virtual Status Import(const std::string& path, const std::string& text, const GameIndex& games) = 0;
};

/// Settings that stay on their device: display, paths, input devices, the plugins, the sync
/// settings themselves, and whatever a key declares device_only.
bool IsDeviceOnlySetting(const std::string& key);

/// The app's settings as config/settings.json, config/settings-overrides.json (the console
/// layer) and config/games/<system>/<rom stem>.json (a game's layer). Nothing of a `pc`
/// system is in them. `layers` may be null (no console or game files).
std::unique_ptr<ConfigFiles> CreateSettingsFiles(store::Settings* settings, system::SettingsStore* layers,
                                                 std::filesystem::path settings_file, std::set<std::string> pc);

/// SHA-256 of files, computed again only when their size or time changed since the last run.
class HashCache {
public:
    explicit HashCache(std::filesystem::path file);
    std::string Hash(const std::filesystem::path& file, std::uint64_t size, std::int64_t modified);
    void Save();

private:
    struct Known {
        std::uint64_t size = 0;
        std::int64_t modified = 0;
        std::string hash;
    };
    std::filesystem::path file_;
    std::map<std::string, Known> known_;
    bool changed_ = false;
};

struct CatalogRequest {
    std::filesystem::path data_dir;
    std::filesystem::path games_folder;
    std::set<std::string> categories;
    bool upload_games = false; // games: only then are local games listed
    const GameIndex* games = nullptr;
    ConfigFiles* config = nullptr;
    const std::atomic<bool>* cancel = nullptr;
};

/// The local files of the request's categories.
Catalog BuildCatalog(const CatalogRequest& request, HashCache& hashes);

/// A file's time as Unix seconds.
std::int64_t FileTime(const std::filesystem::path& file);
void SetFileTime(const std::filesystem::path& file, std::int64_t unix_seconds);

struct JournalEntry {
    std::string hash; // the local content both sides agreed on
    std::string rev;  // the remote version at that time
    std::uint64_t size = 0;
    std::int64_t synced = 0; // when, Unix seconds
};

/// <data>/sync/<provider>/journal.json, and the deletes the user made in EmuDEN.
class Journal {
public:
    explicit Journal(std::filesystem::path file);
    bool Save() const;
    std::map<std::string, JournalEntry> entries;
    std::set<std::string> deleted; // portable names to delete remotely

private:
    std::filesystem::path file_;
};

enum class Op {
    Upload,       // local changed or new
    Download,     // remote changed or new
    Compare,      // both new: equal content is agreed on, else a conflict
    Conflict,     // both changed since the journal
    Adopt,        // both there and taken as equal without a transfer (games of the same size)
    DeleteRemote, // the user deleted it here
    Forget,       // gone on both sides
};

struct Action {
    Op op = Op::Upload;
    std::string path;
};

struct PlanRequest {
    const Catalog* local = nullptr;
    const std::map<std::string, RemoteFile>* remote = nullptr;
    const Journal* journal = nullptr;
    std::set<std::string> categories;
    bool upload_games = false;
};

/// What to do for every path that local, remote or the journal knows. Nothing is deleted
/// locally; a remote delete only for a path in the journal's `deleted`. Games are never
/// downloaded by a plan, and uploaded only with `upload_games`.
std::vector<Action> MakePlan(const PlanRequest& request);

} // namespace den::sync
