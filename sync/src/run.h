// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One sync run (run.cpp), its file transfers (run_files.cpp) and a game's own run before it
// starts or after it ends (game_sync.cpp).

#pragma once

#include <chrono>
#include <functional>
#include <mutex>

#include "engine_impl.h"

namespace den::sync {

/// A run over one game's saves and states (and the play time files), not the whole library.
struct GameScope {
    std::string game_id;
    /// Before the game starts: nothing goes up (it does when the game ends), and files changed
    /// on both sides are left for the user instead of resolved.
    bool before = false;
    CheckGate* gate = nullptr; // before: stops the check, and every write after it
    double seconds = -1;       // before: how long the files may be waited for
    GameCheck* out = nullptr;
};

class Run {
public:
    Run(EngineImpl& engine, Provider& provider, const std::atomic<bool>* cancel);

    Status Go(const std::vector<std::string>& first_games);
    Status GoGame(const GameScope& scope);

private:
    // run.cpp
    bool Cancelled() const;
    void Setup();
    /// The provider's listing of `categories` (one call for Google Drive); false when nothing
    /// could be listed. Categories without a listing are taken out.
    bool List(std::set<std::string>& categories, std::map<std::string, RemoteFile>& remote);
    void SaveJournal(bool now);
    /// Lets a game's own run take the files; the actions it did are left out afterwards.
    void GiveWay();
    void Synced(const std::string& id);
    void UploadGames(const std::vector<Action>& uploads);
    /// The journal on disk gets `path`'s entry, the rest as it is there (games pass).
    void SaveEntry(const std::string& path);

    // run_files.cpp
    void Error(const std::string& message);
    template <typename Fn>
    void Change(Fn&& fn) {
        {
            std::lock_guard lock(engine_.mutex_);
            fn(*state_);
        }
        engine_.Touch();
    }
    void Current(const std::string& path, std::uint64_t size);
    void Fraction(double done, double total);
    std::string GameOf(const std::string& path) const;
    std::filesystem::path Temp(const std::string& path);
    const RemoteFile* RemoteOf(const std::string& path) const;
    void Agree(const std::string& path, const std::string& hash, const std::string& rev, std::uint64_t size);
    /// Downloads into a temporary file; empty when that failed.
    std::filesystem::path Fetch(const std::string& path);
    void Do(const Action& action);
    bool Upload(const LocalItem& item, const std::string& rev);
    std::filesystem::path BaseOf(const std::string& path) const;
    void KeepBase(const std::string& path, const std::string& text);
    /// Writes a game's file, unless the check was given up (then nothing more is written).
    bool Guarded(const std::function<void()>& write);
    bool Apply(const std::string& path, const std::filesystem::path& temp, const RemoteFile& remote);
    void Resolve(const LocalItem& local, const RemoteFile& remote, const std::filesystem::path& temp);

    // game_sync.cpp
    void GameAction(const GameScope& scope, const Action& action);

    EngineImpl& engine_;
    Provider& provider_;
    const std::atomic<bool>* cancel_;
    SyncState own_state_;             // a game's run keeps its counts apart from the full run's
    SyncState* state_ = nullptr;
    CheckGate* gate_ = nullptr;
    GameIndex games_;
    Catalog local_;
    const std::map<std::string, RemoteFile>* remote_ = nullptr;
    Journal* journal_ = nullptr;
    std::unique_ptr<ConfigFiles> config_;
    std::filesystem::path sync_dir_, temp_dir_, games_folder_;
    std::unique_lock<std::timed_mutex> files_;
    std::set<std::string> skipped_; // paths a game's own run did meanwhile
    int temp_ = 0;
    std::chrono::steady_clock::time_point saved_;
};

} // namespace den::sync
