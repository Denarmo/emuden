// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The engine: settings and triggers (engine.cpp), one run (run.cpp), conflicts and games
// (engine_files.cpp), den.sync (engine_link.cpp).

#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <set>

#include "den/sync/engine.h"
#include "den/sync/names.h"
#include "den/sync/plan.h"

namespace den::sync {

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
    }

    // For the run and the link.
    std::set<std::string> PcSystems() const;
    GameIndex Games() const;
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
    /// Keeps a conflict's losing copy: `copy` is moved under <data>/sync/conflicts.
    void KeepLoser(const std::string& path, const std::filesystem::path& copy, const std::string& from,
                   const std::string& game_id);

private:
    void Request(const std::string& first_game);
    void StartRun();
    void LoadConflicts();
    void SaveConflicts() const; // needs mutex_
    std::string Key(const std::string& name, const std::string& provider) const;

    EngineConfig config_;
    std::atomic<std::uint64_t> revision_{1};
    std::atomic<bool> stop_{false};
    std::mutex run_mutex_; // one run (or game download) at a time

    mutable std::mutex mutex_; // everything below
    SyncState state_;
    std::vector<Conflict> conflicts_;
    std::vector<RemoteGame> remote_games_;
    std::map<std::string, std::int64_t> game_synced_; // game id -> last synced, of the active provider
    std::string running_game_;
    bool job_running_ = false, pending_ = false;
    std::vector<std::string> pending_games_;
    JobPtr job_;
    double last_timer_ = 0;
    std::shared_ptr<plugins::SyncLink> link_;

    friend class Run;
};

} // namespace den::sync
