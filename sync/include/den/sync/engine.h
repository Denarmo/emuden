// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sync engine (docs/sync.md, section 3): which provider is active, what syncs, when a
// run starts, its status, the conflicts it kept, and games downloaded on request. The app
// creates one (app/screens/sync) and hands its link to the feature host.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/library/library.h"
#include "den/sync/provider.h"

namespace den::store {
class Settings;
}
namespace den::system {
class SettingsStore;
}
namespace den::plugins {
class Host;
class FeatureHost;
class SyncLink;
} // namespace den::plugins

namespace den::sync {

struct ProviderInfo {
    std::string id, name;
    std::vector<std::string> categories; // what the plugin offers
};

/// What the sync page and den.sync.status() show.
struct SyncState {
    bool running = false;
    std::int64_t last_sync = 0; // the end of the last run that reached the provider
    int done = 0, total = 0;    // files of the current (or last) run
    int uploaded = 0, downloaded = 0;
    int conflicts = 0;
    int waiting = 0;            // files of a running game, left for when it ends
    std::string current;        // the file being moved
    float fraction = -1;        // of it, negative when unknown
    std::vector<std::string> errors;
};

/// A file that lost a conflict, kept under <data>/sync/conflicts.
struct Conflict {
    std::string path;           // portable name
    std::filesystem::path file; // the kept copy
    std::string from;           // the device it came from, or "another device"
    std::int64_t time = 0;
    std::string game_id;
};

/// A game the provider has and this device may download.
struct RemoteGame {
    std::string system, name;
    std::vector<std::string> paths; // the index file first, then its members
    std::uint64_t size = 0;
    bool present = false;           // this device has it already
};

/// What the game's details page says about its saves.
struct GameSync {
    bool active = false;     // a provider syncs saves or states on this device
    std::int64_t synced = 0; // the last time one of its files was synced, 0 = never
    bool conflict = false;
    bool waiting = false;    // it runs: its files sync when the session ends
};

struct EngineConfig {
    std::filesystem::path data_dir;
    Jobs* jobs = nullptr;
    store::Settings* settings = nullptr;     // the engine's own settings (sync.*) and config/settings.json
    system::SettingsStore* layers = nullptr; // console and game settings; may be null
    library::Library* library = nullptr;
    plugins::Host* consoles = nullptr;       // which consoles are PC ones; may be null
    plugins::FeatureHost* features = nullptr;
    std::string device;                      // this device's name, for conflict copies
    std::function<std::int64_t()> now;       // Unix seconds; default: the clock
    /// Tests: the library's games, and the providers, instead of `library` and `features`.
    std::function<std::vector<library::Entry>()> entries;
    std::vector<ProviderInfo> providers;
    /// Tests: makes the provider instead of the plugin or the folder store.
    std::function<std::unique_ptr<Provider>(const std::string& id)> provider_factory;
};

class Engine {
public:
    virtual ~Engine() = default;

    virtual std::vector<ProviderInfo> Providers() const = 0;
    virtual std::string Active() const = 0;          // "" when sync is off
    virtual void SetActive(const std::string& id) = 0;
    /// The categories on for the provider on this device (default: what it offers, without bios).
    virtual std::vector<std::string> Categories(const std::string& provider) const = 0;
    virtual void SetCategory(const std::string& provider, const std::string& category, bool on) = 0;
    virtual bool UploadGames() const = 0;
    virtual void SetUploadGames(bool on) = 0;
    virtual std::filesystem::path GamesFolder() const = 0;
    virtual void SetGamesFolder(const std::filesystem::path& folder) = 0;
    virtual int IntervalMinutes() const = 0; // 0: no timer
    virtual void SetIntervalMinutes(int minutes) = 0;
    /// The folder store of a provider, empty when it uses its own remote.
    virtual std::filesystem::path Folder(const std::string& provider) const = 0;
    virtual Status CheckFolder(const std::filesystem::path& folder) const = 0;
    virtual Status UseFolder(const std::string& provider, const std::filesystem::path& folder) = 0;

    // Triggers. From any thread; a run that is asked for while one goes on follows it.
    virtual void SyncNow() = 0;
    /// A game session ended: that game's files go first.
    virtual void SyncGame(const std::string& game_id) = 0;
    /// Main thread, every frame: the timer. `running_game` is the library id of the game
    /// that runs now ("" for none): its files are never replaced while it does.
    virtual void Pump(const std::string& running_game) = 0;
    /// The user deleted a synced file in EmuDEN: the remote copy goes on the next run.
    virtual void NoteDeleted(const std::filesystem::path& local_file) = 0;

    virtual SyncState State() const = 0;
    virtual std::vector<Conflict> Conflicts() const = 0;
    /// "Use this one instead": the kept copy replaces the current file, which is kept instead.
    virtual Status UseConflict(std::size_t index) = 0;
    virtual void DismissConflict(std::size_t index) = 0;
    virtual GameSync GameState(const std::string& game_id) const = 0;

    /// Lists the active provider's games (a job); RemoteGames() has them when it is done.
    virtual JobPtr RefreshRemoteGames() = 0;
    virtual std::vector<RemoteGame> RemoteGames() const = 0;
    virtual JobPtr StartDownload(const RemoteGame& game) = 0;
    /// Blocking: fetches a game's files from `provider` into the games folder and adds that
    /// folder to the library.
    virtual Status DownloadGame(const std::string& provider, const std::vector<std::string>& paths,
                                const std::atomic<bool>* cancel) = 0;

    /// Blocking: one run now (tests; the app's runs go through the triggers).
    virtual Status RunOnce(const std::vector<std::string>& first_games, const std::atomic<bool>* cancel) = 0;

    /// Grows whenever the state, the conflicts or the settings changed.
    virtual std::uint64_t Revision() const = 0;
    /// What den.sync of the providers reaches.
    virtual std::shared_ptr<plugins::SyncLink> Link() = 0;
    /// The app ends: provider calls that wait give up, no run starts.
    virtual void Stop() = 0;
};

std::shared_ptr<Engine> CreateEngine(EngineConfig config);

} // namespace den::sync
