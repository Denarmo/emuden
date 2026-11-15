// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The engine: settings and triggers (engine.cpp), one run (run.cpp), conflicts and games
// (engine_files.cpp), den.sync (engine_link.cpp), a game's own sync (game_sync.cpp).

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>

#include "den/sync/engine.h"
#include "den/sync/names.h"
#include "den/sync/plan.h"

namespace den::sync {

/// Play time for the play files: this device's, and where the other devices' sum goes.
struct PlaySource {
    std::function<std::map<std::string, library::PlayTotals>()> own;
    std::function<void(const std::map<std::string, library::PlayTotals>&)> set_others;
};

/// config/playtime/<device id>.json (play_files.cpp); `dir` keeps the other devices' files.
std::unique_ptr<ConfigFiles> CreatePlayFiles(PlaySource source, std::filesystem::path dir, std::string device_id,
                                             std::string device_name);
std::unique_ptr<ConfigFiles> CombineConfigFiles(std::unique_ptr<ConfigFiles> settings,
                                                std::unique_ptr<ConfigFiles> play);
/// The names of the other devices whose play time files are in `dir`.
std::vector<std::string> OtherDeviceNames(const std::filesystem::path& dir, const std::string& own_id);
/// The other device that played `key` ("gba/Pokemon - Lazarus") last, by its play time file
/// in `dir`; "" when none did.
std::string LastPlayedBy(const std::filesystem::path& dir, const std::string& own_id, const std::string& key);

/// A file CheckGame found changed on both sides, for ResolveGame.
struct PendingConflict {
    std::string path;
    std::filesystem::path fetched; // the online copy, under <sync dir>/pending
    std::string fetched_hash;
    RemoteFile remote;
    std::filesystem::path local;
    std::string local_hash;        // as it was at the check
    std::string journal_hash, journal_rev; // the journal's entry then ("" when none)
    std::string from;              // the device the online copy came from, "" when not known
};

class EngineImpl final : public Engine, public std::enable_shared_from_this<EngineImpl> {
public:
    explicit EngineImpl(EngineConfig config);

    std::vector<ProviderInfo> Providers() const override;
    std::string Active() const override;
    void SetActive(const std::string& id) override;
    std::vector<std::string> Categories(const std::string& provider) const override;
    void SetCategory(const std::string& provider, const std::string& category, bool on) override;
    bool UploadGames() const override;
    void SetUploadGames(bool on) override;
    std::filesystem::path GamesFolder() const override;
    void SetGamesFolder(const std::filesystem::path& folder) override;
    int IntervalMinutes() const override;
    void SetIntervalMinutes(int minutes) override;
    std::filesystem::path Folder(const std::string& provider) const override;
    Status CheckFolder(const std::filesystem::path& folder) const override;
    Status UseFolder(const std::string& provider, const std::filesystem::path& folder) override;

    void SyncNow() override;
    void SyncGame(const std::string& game_id) override;
    bool SyncsSaves() const override;
    GameCheck CheckGame(const std::string& game_id, CheckGate& gate, double seconds) override;
    Status ResolveGame(const std::string& game_id, bool use_online) override;
    Status SyncGameOnce(const std::string& game_id, const std::atomic<bool>* cancel) override;
    GameTransfer GameProgress(bool before) const override;
    int GameUploadsPending() const override {
        return uploads_pending_.load();
    }
    void Pump(const std::string& running_game) override;
    void NoteDeleted(const std::filesystem::path& local_file) override;

    SyncState State() const override;
    std::vector<Conflict> Conflicts() const override;
    Status UseConflict(std::size_t index) override;
    void DismissConflict(std::size_t index) override;
    GameSync GameState(const std::string& game_id) const override;

    JobPtr RefreshRemoteGames() override;
    std::vector<RemoteGame> RemoteGames() const override;
    JobPtr StartDownload(const RemoteGame& game) override;
    Status DownloadGame(const std::string& provider, const std::vector<std::string>& paths,
                        const std::atomic<bool>* cancel) override;

    Status RunOnce(const std::vector<std::string>& first_games, const std::atomic<bool>* cancel) override;

    std::uint64_t Revision() const override {
        return revision_.load();
    }
    std::shared_ptr<plugins::SyncLink> Link() override;
    void Stop() override {
        stop_.store(true);
        run_cancel_.store(true);
    }

    // For the run and the link.
    std::set<std::string> PcSystems() const;
    GameIndex Games() const;
    /// Whether the provider lists one system folder ("saves/gba/") quickly, by itself.
    bool ListsFolders(const std::string& id) const;
    std::unique_ptr<Provider> MakeProvider(const std::string& id, const std::atomic<bool>* cancel) const;
    std::filesystem::path SyncDir(const std::string& provider) const;
    std::filesystem::path Home() const;
    std::int64_t Now() const {
        return config_.now();
    }
    void Touch() {
        ++revision_;
    }
    std::string RunningGame() const;
    /// The settings and play time files (config category).
    std::unique_ptr<ConfigFiles> MakeConfigFiles();
    std::string DeviceId();
    /// Who a conflict's remote copy came from: the one other device known, else "another device".
    std::string OtherDevice();
    /// Whether provider calls should give up: the app ends, or a game started while games upload.
    bool Stopping() const {
        return run_cancel_.load();
    }
    /// Game uploads in progress stop, so what waits for the run (a download, a swap) goes now.
    void YieldGames() {
        if (games_pass_.load()) {
            run_cancel_.store(true);
        }
    }
    /// Takes the files lock (the journal, and the saves, states and settings files) for a
    /// game's own run or a conflict swap: a full run's actions give way to it between two files.
    /// Waits at most `seconds` (negative: as long as it takes) and while `give_up` is not set.
    bool LockFiles(std::unique_lock<std::timed_mutex>& lock, const std::atomic<bool>* give_up, double seconds = -1);
    /// A game's own run did `path`: the full run that gave way to it leaves it out.
    void NoteTouched(const std::string& path) {
        std::lock_guard lock(mutex_);
        touched_.insert(path);
    }
    /// Keeps a conflict's losing copy: `copy` is moved under <data>/sync/conflicts.
    void KeepLoser(const std::string& path, const std::filesystem::path& copy, const std::string& from,
                   const std::string& game_id);

private:
    /// A run, after the one going on; one that is not the timer's stops its game uploads.
    void Request(const std::string& first_game, bool timer = false);
    void StartRun();
    void LoadConflicts();
    void SaveConflicts() const; // needs mutex_
    std::string Key(const std::string& name, const std::string& provider) const;

    EngineConfig config_;
    std::atomic<std::uint64_t> revision_{1};
    std::atomic<bool> stop_{false};
    std::atomic<bool> run_cancel_{false}; // the current run's provider calls give up
    std::atomic<bool> games_pass_{false};
    std::mutex run_mutex_; // one run (or game download) at a time
    // The journal and the files of saves, states and settings: held by a run's regular part
    // (not its game uploads), a game's own run and conflict swaps.
    std::timed_mutex files_mutex_;
    std::atomic<int> files_wanted_{0}; // a game's own run waits for files_mutex_
    std::atomic<int> uploads_pending_{0}; // SyncGame jobs not finished

    mutable std::mutex mutex_; // everything below
    SyncState state_;
    GameTransfer check_transfer_, upload_transfer_; // games' own runs, before and after
    std::vector<Conflict> conflicts_;
    std::vector<RemoteGame> remote_games_;
    std::map<std::string, std::int64_t> game_synced_; // game id -> last synced, of the active provider
    std::string running_game_;
    std::set<std::string> touched_; // by games' own runs since the full run's catalog
    std::map<std::string, std::vector<PendingConflict>> pending_conflicts_; // game id -> CheckGame's conflicts
    bool job_running_ = false, pending_ = false;
    std::vector<std::string> pending_games_;
    JobPtr job_;
    double last_timer_ = 0;
    std::shared_ptr<plugins::SyncLink> link_;

    friend class Run;
};

} // namespace den::sync
