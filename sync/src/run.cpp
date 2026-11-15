// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One sync run: catalog, listing, journal, plan, then every action, each file on its own
// (a file that fails is tried again on the next run). Saves, states, settings and BIOS files
// come first; the run counts as a sync once they are done. Game uploads follow, one at a
// time, until a game starts, another run is asked for or the app ends. The journal is written
// after every action, so a run cut short loses nothing it did. A game's own run (before it
// starts, after it ends) takes the files between two actions; the run leaves out what it did.

#include <algorithm>
#include <optional>
#include <system_error>
#include <thread>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/store/settings.h"
#include "run.h"

namespace den::sync {

namespace fs = std::filesystem;

namespace {

constexpr const char* ORDER[] = {SAVES, STATES, CONFIG, GAMES, BIOS};
constexpr auto GIVE_WAY_LIMIT = std::chrono::seconds(30); // a game's own run is quick

} // namespace

Run::Run(EngineImpl& engine, Provider& provider, const std::atomic<bool>* cancel)
    : engine_(engine), provider_(provider), cancel_(cancel), state_(&engine.state_) {}

bool Run::Cancelled() const {
    return cancel_ != nullptr && cancel_->load();
}

void Run::Setup() {
    games_ = engine_.Games();
    sync_dir_ = engine_.SyncDir(provider_.Id());
    temp_dir_ = sync_dir_ / "tmp";
    games_folder_ = engine_.GamesFolder();
    config_ = engine_.MakeConfigFiles();
}

bool Run::List(std::set<std::string>& categories, std::map<std::string, RemoteFile>& remote) {
    const std::set<std::string> pc = engine_.PcSystems();
    bool listed = false;
    // Google Drive lists everything for every call: once is enough there.
    std::optional<Result<std::vector<RemoteFile>>> all;
    if (provider_.Id() == GDRIVE_PROVIDER) {
        Change([](SyncState& s) { s.current = "Looking for changes"; });
        all = provider_.List("");
    }
    for (const char* category : ORDER) {
        if (!categories.contains(category)) {
            continue;
        }
        Result<std::vector<RemoteFile>> files = all ? *all : provider_.List(std::string(category) + "/");
        if (!files) {
            Error(std::string("Listing ") + category + ": " + files.error());
            categories.erase(category); // nothing of it is planned without its listing
            continue;
        }
        listed = true;
        for (RemoteFile& file : files.value()) {
            if (CategoryOf(file.path) != category || !IsPortablePath(file.path) || IsPcPath(file.path, pc)) {
                continue;
            }
            if (file.rev.empty()) {
                file.rev = std::to_string(file.size) + "@" + std::to_string(file.modified);
            }
            remote[file.path] = std::move(file);
        }
    }
    Change([](SyncState& s) { s.current.clear(); });
    return listed;
}

Status Run::Go(const std::vector<std::string>& first_games) {
    const std::string id = provider_.Id();
    const fs::path data = engine_.config_.data_dir;
    Setup();
    {
        std::lock_guard lock(engine_.mutex_);
        engine_.touched_.clear();
    }
    const std::vector<std::string> enabled = engine_.Categories(id);
    std::set<std::string> categories(enabled.begin(), enabled.end());
    std::error_code ec;
    fs::create_directories(temp_dir_, ec);
    HashCache hashes(data / "sync" / "hashes.json");

    CatalogRequest request;
    request.data_dir = data;
    request.games_folder = games_folder_;
    request.categories = categories;
    request.upload_games = engine_.UploadGames();
    request.games = &games_;
    request.config = config_.get();
    request.cancel = cancel_;
    local_ = BuildCatalog(request, hashes);

    std::map<std::string, RemoteFile> remote;
    const bool listed = List(categories, remote);
    remote_ = &remote;

    // From here the journal and the files are this run's, but for a game's own run.
    files_ = std::unique_lock(engine_.files_mutex_);
    Journal journal(sync_dir_ / "journal.json");
    journal_ = &journal;
    if (categories.contains(CONFIG)) {
        AddClearedGameLayers(local_, journal, games_);
    }
    PlanRequest plan;
    plan.local = &local_;
    plan.remote = &remote;
    plan.journal = &journal;
    plan.categories = categories;
    plan.upload_games = request.upload_games;
    std::vector<Action> actions = MakePlan(plan);
    // Game uploads go in a pass of their own; adopting or forgetting a game is quick.
    std::vector<Action> uploads;
    std::erase_if(actions, [&](const Action& action) {
        if (action.op == Op::Upload && CategoryOf(action.path) == GAMES) {
            uploads.push_back(action);
            return true;
        }
        // A save or state of a game this device does not have is not fetched at all.
        if (action.op == Op::Download && CategoryOf(action.path) != CONFIG && !local_.contains(action.path)) {
            const auto target = TargetOf(action.path, games_, data, games_folder_);
            return !target || target->file.empty();
        }
        return false;
    });
    std::stable_partition(actions.begin(), actions.end(), [&](const Action& action) {
        const std::string game = GameOf(action.path);
        return !game.empty() && std::find(first_games.begin(), first_games.end(), game) != first_games.end();
    });
    {
        std::lock_guard lock(engine_.mutex_);
        skipped_ = engine_.touched_; // a game's own run between the catalog and the lock
        state_->total = static_cast<int>(actions.size());
        state_->games_total = static_cast<int>(uploads.size());
    }
    engine_.Touch();
    const std::string running = engine_.RunningGame();
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (Cancelled()) {
            break;
        }
        if (engine_.files_wanted_.load() > 0) {
            GiveWay();
        }
        const Action& action = actions[i];
        const std::string game = GameOf(action.path);
        if (skipped_.contains(action.path)) {
            // done by the game's own run
        } else if (!running.empty() && game == running && action.op != Op::Forget) {
            Change([](SyncState& s) { ++s.waiting; });
        } else {
            Current(action.path, local_.contains(action.path) ? local_.at(action.path).size : 0);
            Do(action);
            SaveJournal(action.op != Op::Adopt && action.op != Op::Forget);
        }
        Change([](SyncState& s) { ++s.done; });
    }
    SaveJournal(true);
    hashes.Save();
    if (listed && !Cancelled()) {
        Synced(id);
    }
    files_.unlock();
    {
        const SyncState state = engine_.State();
        log::Info("sync", "{} files: {} up, {} down, {} waiting, {} errors; {} games to upload", actions.size(),
                  state.uploaded, state.downloaded, state.waiting, state.errors.size(), uploads.size());
    }
    UploadGames(uploads);
    fs::remove_all(temp_dir_, ec);
    return listed ? Status() : Status::Fail("The provider could not be reached.");
}

// Atomically, after each transfer, and at least every second for the quick ones.
void Run::SaveJournal(bool now) {
    const auto time = std::chrono::steady_clock::now();
    if (now || time - saved_ >= std::chrono::seconds(1)) {
        journal_->Save();
        saved_ = time;
    }
}

void Run::GiveWay() {
    SaveJournal(true);
    files_.unlock();
    const auto until = std::chrono::steady_clock::now() + GIVE_WAY_LIMIT;
    while (engine_.files_wanted_.load() > 0 && std::chrono::steady_clock::now() < until && !Cancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    files_.lock();
    *journal_ = Journal(sync_dir_ / "journal.json");
    std::lock_guard lock(engine_.mutex_);
    skipped_ = engine_.touched_;
}

// The regular part is done: that is the last sync, and the games' marks.
void Run::Synced(const std::string& id) {
    std::map<std::string, std::int64_t> synced;
    for (const auto& [path, entry] : journal_->entries) {
        const std::string game = GameOf(path);
        if (!game.empty() && (CategoryOf(path) == SAVES || CategoryOf(path) == STATES)) {
            synced[game] = std::max(synced[game], entry.synced);
        }
    }
    const std::int64_t now = engine_.Now();
    {
        std::lock_guard lock(engine_.mutex_);
        engine_.game_synced_ = std::move(synced);
        engine_.state_.last_sync = now;
    }
    if (engine_.config_.settings != nullptr) {
        engine_.config_.settings->SetNumber(engine_.Key("last", id), static_cast<double>(now));
    }
    engine_.Touch();
}

void Run::SaveEntry(const std::string& path) {
    std::lock_guard files(engine_.files_mutex_);
    Journal fresh(sync_dir_ / "journal.json");
    if (const auto found = journal_->entries.find(path); found != journal_->entries.end()) {
        fresh.entries[path] = found->second;
        fresh.Save();
    }
}

// One game at a time; the provider resumes a cut upload where it stopped. Without the files
// lock: a game's own run never waits for this pass.
void Run::UploadGames(const std::vector<Action>& uploads) {
    if (uploads.empty() || Cancelled() || engine_.Stopping()) {
        return;
    }
    engine_.games_pass_.store(true);
    Change([](SyncState& s) { s.games_pass = true; });
    for (const Action& action : uploads) {
        if (Cancelled() || engine_.Stopping() || !engine_.RunningGame().empty()) {
            break;
        }
        if (const auto item = local_.find(action.path); item != local_.end()) {
            Current(action.path, item->second.size);
            if (Upload(item->second, std::string())) {
                SaveEntry(action.path);
            }
        }
        Change([](SyncState& s) { ++s.games_done; });
    }
    engine_.games_pass_.store(false);
    log::Info("sync", "games: {} of {} looked at{}", engine_.State().games_done, uploads.size(),
              engine_.Stopping() ? ", the rest goes on later" : "");
}

Status EngineImpl::RunOnce(const std::vector<std::string>& first_games, const std::atomic<bool>* cancel) {
    std::lock_guard run(run_mutex_);
    const std::string id = Active();
    if (id.empty() || stop_.load()) {
        return {};
    }
    run_cancel_.store(stop_.load());
    std::unique_ptr<Provider> provider = MakeProvider(id, &run_cancel_);
    if (!provider) {
        return Status::Fail("The sync plugin is not installed.");
    }
    {
        std::lock_guard lock(mutex_);
        const std::int64_t last = state_.last_sync;
        state_ = {};
        state_.running = true;
        state_.last_sync = last;
        state_.conflicts = 0;
    }
    Touch();
    Status status;
    try {
        status = Run(*this, *provider, cancel != nullptr ? cancel : &stop_).Go(first_games);
    } catch (const std::exception& e) {
        status = Status::Fail(e.what());
    }
    games_pass_.store(false);
    {
        std::lock_guard lock(mutex_);
        state_.running = false;
        state_.games_pass = false;
        state_.current.clear();
        state_.fraction = -1;
        state_.size = 0;
        if (status.ok) {
            state_.last_sync = Now();
        } else if (state_.errors.empty()) {
            state_.errors.push_back(status.error);
        }
    }
    if (status.ok && config_.settings != nullptr) {
        config_.settings->SetNumber(Key("last", id), static_cast<double>(Now()));
    }
    Touch();
    return status;
}

} // namespace den::sync
