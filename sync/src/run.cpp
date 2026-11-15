// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One sync run: catalog, listing, journal, plan, then every action, each file on its own
// (a file that fails is tried again on the next run). Saves, states, settings and BIOS files
// come first; the run counts as a sync once they are done. Game uploads follow, one at a
// time, until a game starts, another run is asked for or the app ends. The journal is written
// after every action, so a run cut short loses nothing it did.

#include <algorithm>
#include <chrono>
#include <system_error>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/log.h"
#include "den/base/sha256.h"
#include "den/store/settings.h"
#include "engine_impl.h"

namespace den::sync {

namespace fs = std::filesystem;

namespace {

constexpr const char* ORDER[] = {SAVES, STATES, CONFIG, GAMES, BIOS};

std::string TextHash(const std::string& text) {
    return Sha256Hex(text.data(), text.size());
}

} // namespace

class Run {
public:
    Run(EngineImpl& engine, Provider& provider, const std::atomic<bool>* cancel)
        : engine_(engine), provider_(provider), cancel_(cancel) {}

    Status Go(const std::vector<std::string>& first_games) {
        const std::string id = provider_.Id();
        const fs::path data = engine_.config_.data_dir;
        const std::set<std::string> pc = engine_.PcSystems();
        games_ = engine_.Games();
        const std::vector<std::string> enabled = engine_.Categories(id);
        std::set<std::string> categories(enabled.begin(), enabled.end());
        sync_dir_ = engine_.SyncDir(id);
        temp_dir_ = sync_dir_ / "tmp";
        std::error_code ec;
        fs::create_directories(temp_dir_, ec);
        games_folder_ = engine_.GamesFolder();
        HashCache hashes(data / "sync" / "hashes.json");
        config_ = engine_.MakeConfigFiles();

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
        bool listed = false;
        for (const char* category : ORDER) {
            if (!categories.contains(category)) {
                continue;
            }
            Result<std::vector<RemoteFile>> files = provider_.List(std::string(category) + "/");
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
        remote_ = &remote;

        Journal journal(sync_dir_ / "journal.json");
        journal_ = &journal;
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
            return false;
        });
        std::stable_partition(actions.begin(), actions.end(), [&](const Action& action) {
            const std::string game = GameOf(action.path);
            return !game.empty() && std::find(first_games.begin(), first_games.end(), game) != first_games.end();
        });
        {
            std::lock_guard lock(engine_.mutex_);
            engine_.state_.total = static_cast<int>(actions.size());
            engine_.state_.games_total = static_cast<int>(uploads.size());
        }
        engine_.Touch();
        const std::string running = engine_.RunningGame();
        for (const Action& action : actions) {
            if (Cancelled()) {
                break;
            }
            const std::string game = GameOf(action.path);
            if (!running.empty() && game == running && action.op != Op::Forget) {
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
        {
            const SyncState state = engine_.State();
            log::Info("sync", "{} files: {} up, {} down, {} waiting, {} errors; {} games to upload", actions.size(),
                      state.uploaded, state.downloaded, state.waiting, state.errors.size(), uploads.size());
        }
        UploadGames(uploads);
        fs::remove_all(temp_dir_, ec);
        return listed ? Status() : Status::Fail("The provider could not be reached.");
    }

private:
    bool Cancelled() const {
        return cancel_ != nullptr && cancel_->load();
    }

    // Atomically, after each transfer, and at least every second for the quick ones.
    void SaveJournal(bool now) {
        const auto time = std::chrono::steady_clock::now();
        if (now || time - saved_ >= std::chrono::seconds(1)) {
            journal_->Save();
            saved_ = time;
        }
    }

    // The regular part is done: that is the last sync, and the games' marks.
    void Synced(const std::string& id) {
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

    // One game at a time; the provider resumes a cut upload where it stopped.
    void UploadGames(const std::vector<Action>& uploads) {
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
                Upload(item->second, std::string());
                SaveJournal(true);
            }
            Change([](SyncState& s) { ++s.games_done; });
        }
        engine_.games_pass_.store(false);
        log::Info("sync", "games: {} of {} looked at{}", engine_.State().games_done, uploads.size(),
                  engine_.Stopping() ? ", the rest goes on later" : "");
    }

    void Error(const std::string& message) {
        log::Warn("sync", "{}", message);
        Change([&](SyncState& s) {
            if (s.errors.size() < 20) {
                s.errors.push_back(message);
            }
        });
    }

    template <typename Fn>
    void Change(Fn&& fn) {
        {
            std::lock_guard lock(engine_.mutex_);
            fn(engine_.state_);
        }
        engine_.Touch();
    }

    void Current(const std::string& path, std::uint64_t size) {
        Change([&](SyncState& s) {
            s.current = path;
            s.fraction = -1;
            s.size = size;
        });
    }

    void Fraction(double done, double total) {
        Change([&](SyncState& s) {
            s.fraction = total > 0 ? static_cast<float>(done / total) : done <= 1 ? static_cast<float>(done) : -1;
            if (total > 1) {
                s.size = static_cast<std::uint64_t>(total);
            }
        });
    }

    std::string GameOf(const std::string& path) const {
        if (const auto item = local_.find(path); item != local_.end()) {
            return item->second.game_id;
        }
        const auto target = TargetOf(path, games_, engine_.config_.data_dir, games_folder_);
        return target ? target->game_id : std::string();
    }

    fs::path Temp(const std::string& path) {
        return temp_dir_ / (std::to_string(++temp_) + "-" + SafeFileName(path.substr(path.rfind('/') + 1)));
    }

    const RemoteFile* RemoteOf(const std::string& path) const {
        const auto found = remote_->find(path);
        return found != remote_->end() ? &found->second : nullptr;
    }

    void Agree(const std::string& path, const std::string& hash, const std::string& rev, std::uint64_t size) {
        journal_->entries[path] = {hash, rev, size, engine_.Now()};
    }

    void Do(const Action& action) {
        const std::string& path = action.path;
        const LocalItem* local = local_.contains(path) ? &local_.at(path) : nullptr;
        const RemoteFile* remote = RemoteOf(path);
        switch (action.op) {
        case Op::Upload:
            Upload(*local, remote ? (journal_->entries.contains(path) ? journal_->entries[path].rev : remote->rev)
                                  : std::string());
            break;
        case Op::Download:
        case Op::Compare:
        case Op::Conflict: {
            const fs::path temp = Temp(path);
            const Status got = provider_.Get(path, temp, [this](double done, double total) { Fraction(done, total); });
            if (!got.ok) {
                Error(path + ": " + got.error);
                break;
            }
            if (action.op == Op::Download) {
                Apply(path, temp, *remote);
            } else {
                const std::string hash = Sha256File(temp).value_or("");
                if (local && hash == local->hash) {
                    Agree(path, hash, remote->rev, local->size);
                } else if (CategoryOf(path) == CONFIG) {
                    // Settings are merged, never a conflict copy. Changed on both sides: key by
                    // key against the version both last agreed on (what changed here wins).
                    // Met for the first time: the remote values are applied. Then what this
                    // device has besides goes up.
                    if (action.op == Op::Conflict && local) {
                        const std::string merged = MergeSettings(
                            ReadTextFile(BaseOf(path)).value_or(""), local->text, ReadTextFile(temp).value_or(""));
                        if (!merged.empty()) {
                            WriteFileAtomic(temp, merged);
                        }
                    }
                    if (Apply(path, temp, *remote)) {
                        for (LocalItem& item : config_->Export(games_)) {
                            if (item.path == path && TextHash(item.text) != hash) {
                                item.hash = TextHash(item.text);
                                item.size = item.text.size();
                                Upload(item, remote->rev);
                            }
                        }
                    }
                } else if (local) {
                    Resolve(*local, *remote, temp);
                }
            }
            std::error_code ec;
            fs::remove(temp, ec);
            break;
        }
        case Op::Adopt:
            Agree(path, local->hash, remote->rev, local->size);
            break;
        case Op::DeleteRemote:
            if (const Status deleted = provider_.Delete(path); !deleted.ok) {
                Error(path + ": " + deleted.error);
                break;
            }
            journal_->entries.erase(path);
            journal_->deleted.erase(path);
            break;
        case Op::Forget:
            journal_->entries.erase(path);
            journal_->deleted.erase(path);
            break;
        }
    }

    bool Upload(const LocalItem& item, const std::string& rev) {
        fs::path file = item.file;
        if (file.empty()) {
            file = Temp(item.path);
            WriteFileAtomic(file, item.text);
        }
        Result<std::string> put = provider_.Put(item.path, file, rev, item.modified,
                                                [this](double done, double total) { Fraction(done, total); });
        if (item.file.empty()) {
            std::error_code ec;
            fs::remove(file, ec);
        }
        if (!put && put.error() == "cancelled" && (Cancelled() || engine_.Stopping())) {
            return false; // it goes on in the next run
        }
        if (!put) {
            Error(item.path + ": " + (put.error() == "conflict" ? "changed on another device; it is sorted out "
                                                                  "on the next sync"
                                                                : put.error()));
            return false;
        }
        Agree(item.path, item.hash, put.value(), item.size);
        if (CategoryOf(item.path) == CONFIG) {
            KeepBase(item.path, item.text);
        }
        Change([](SyncState& s) { ++s.uploaded; });
        return true;
    }

    // A downloaded file takes the place of the local one (or is applied, for settings).
    // The last version of a settings file both sides agreed on, for merging.
    fs::path BaseOf(const std::string& path) const {
        return sync_dir_ / "base" / PathFromUtf8(path);
    }

    void KeepBase(const std::string& path, const std::string& text) {
        std::error_code ec;
        fs::create_directories(BaseOf(path).parent_path(), ec);
        WriteFileAtomic(BaseOf(path), text);
    }

    bool Apply(const std::string& path, const fs::path& temp, const RemoteFile& remote) {
        const std::string hash = Sha256File(temp).value_or("");
        if (CategoryOf(path) == CONFIG) {
            const std::string text = ReadTextFile(temp).value_or("");
            if (const Status applied = config_->Import(path, text, games_); !applied.ok) {
                Error(path + ": " + applied.error);
                return false;
            }
            // What both sides agree on is what this device now exports for it.
            std::string agreed = TextHash(text);
            for (const LocalItem& item : config_->Export(games_)) {
                if (item.path == path) {
                    agreed = TextHash(item.text);
                }
            }
            Agree(path, agreed, remote.rev, remote.size);
            KeepBase(path, text);
        } else {
            const auto target = local_.contains(path)
                                    ? std::optional<LocalTarget>(LocalTarget{local_.at(path).file, {}})
                                    : TargetOf(path, games_, engine_.config_.data_dir, games_folder_);
            if (!target || target->file.empty()) {
                return false; // this device does not have the game
            }
            std::error_code ec;
            fs::create_directories(target->file.parent_path(), ec);
            fs::path part = target->file;
            part += ".sync-part";
            fs::copy_file(temp, part, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                SetFileTime(part, remote.modified);
                fs::rename(part, target->file, ec);
            }
            if (ec) {
                fs::remove(part, ec);
                Error(path + ": could not be written");
                return false;
            }
            Agree(path, hash, remote.rev, remote.size);
        }
        Change([](SyncState& s) { ++s.downloaded; });
        return true;
    }

    // Both sides changed: the newer file wins, the other one is kept as a conflict copy.
    void Resolve(const LocalItem& local, const RemoteFile& remote, const fs::path& temp) {
        std::error_code ec;
        if (local.modified >= remote.modified) {
            const fs::path loser = Temp(local.path);
            fs::copy_file(temp, loser, fs::copy_options::overwrite_existing, ec);
            engine_.KeepLoser(local.path, loser, engine_.OtherDevice(), local.game_id);
            Upload(local, remote.rev);
        } else {
            const fs::path loser = Temp(local.path);
            if (local.file.empty()) {
                WriteFileAtomic(loser, local.text);
            } else {
                fs::copy_file(local.file, loser, fs::copy_options::overwrite_existing, ec);
            }
            engine_.KeepLoser(local.path, loser, engine_.config_.device, local.game_id);
            Apply(local.path, temp, remote);
        }
        Change([](SyncState& s) { ++s.conflicts; });
    }

    EngineImpl& engine_;
    Provider& provider_;
    const std::atomic<bool>* cancel_;
    GameIndex games_;
    Catalog local_;
    const std::map<std::string, RemoteFile>* remote_ = nullptr;
    Journal* journal_ = nullptr;
    std::unique_ptr<ConfigFiles> config_;
    fs::path sync_dir_, temp_dir_, games_folder_;
    int temp_ = 0;
    std::chrono::steady_clock::time_point saved_;
};

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
