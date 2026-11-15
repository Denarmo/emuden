// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A game's own sync (docs/sync.md, section 3.3): before it starts, its saves and states are
// checked against the remote (what changed only online comes down, files changed on both
// sides wait for the user's choice); when it ends, they go up at once. Neither waits for a
// full run's game uploads, and a full run's other files give way to it between two files.

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/jobs.h"
#include "den/base/log.h"
#include "den/base/sha256.h"
#include "run.h"

namespace den::sync {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view PLAY_PREFIX = "config/playtime/";

bool IsPlayFile(const std::string& path) {
    return path.starts_with(PLAY_PREFIX);
}

} // namespace

Status Run::GoGame(const GameScope& scope) {
    Setup();
    gate_ = scope.gate;
    state_ = &own_state_;
    temp_dir_ = sync_dir_ / "tmp-game";
    const fs::path data = engine_.config_.data_dir;
    const GameRef* game = games_.ById(scope.game_id);
    std::set<std::string> categories;
    for (const std::string& category : engine_.Categories(provider_.Id())) {
        if (category == SAVES || category == STATES || category == CONFIG) {
            categories.insert(category);
        }
    }
    if (game == nullptr || (!categories.contains(SAVES) && !categories.contains(STATES))) {
        return {};
    }
    HashCache hashes(data / "sync" / "hashes.json");
    CatalogRequest request;
    request.data_dir = data;
    request.games_folder = games_folder_;
    request.categories = categories;
    request.categories.erase(CONFIG);
    request.games = &games_;
    request.cancel = cancel_;
    request.only_game = scope.game_id;
    local_ = BuildCatalog(request, hashes);
    // The play time files: this device's goes up with the game's end; the others name the
    // device an online save came from.
    if (categories.contains(CONFIG)) {
        for (LocalItem& item : config_->Export(games_)) {
            if (IsPlayFile(item.path)) {
                item.size = item.text.size();
                item.hash = Sha256Hex(item.text.data(), item.text.size());
                local_[item.path] = std::move(item);
            }
        }
    }
    // Only the game's system folders where the provider lists one folder quickly; else the
    // categories.
    std::map<std::string, RemoteFile> listed;
    const bool reached = engine_.ListsFolders(provider_.Id()) ? ListFolders(*game, categories, scope.before, listed)
                                                              : List(categories, listed);
    if (!reached) {
        return Status::Fail("The provider could not be reached.");
    }
    if (scope.out != nullptr) {
        scope.out->reached = true;
    }
    const auto mine = [&](const std::string& path) { return IsPlayFile(path) || GameOf(path) == scope.game_id; };
    std::map<std::string, RemoteFile> remote;
    for (auto& [path, file] : listed) {
        if (mine(path)) {
            remote.emplace(path, std::move(file));
        }
    }
    remote_ = &remote;

    if (!engine_.LockFiles(files_, scope.gate != nullptr ? scope.gate->Flag() : cancel_, scope.seconds)) {
        return Status::Fail("The saves are busy syncing; try again in a moment.");
    }
    std::error_code ec;
    fs::create_directories(temp_dir_, ec);
    Journal journal(sync_dir_ / "journal.json");
    journal_ = &journal;
    // The plan sees only this game's entries: the others' are not "gone on both sides".
    Journal view{fs::path()};
    for (const auto& [path, entry] : journal.entries) {
        if (mine(path)) {
            view.entries[path] = entry;
        }
    }
    for (const std::string& path : journal.deleted) {
        if (mine(path)) {
            view.deleted.insert(path);
        }
    }
    PlanRequest plan{&local_, &remote, &view, categories, false};
    std::vector<Action> actions = MakePlan(plan);
    std::stable_partition(actions.begin(), actions.end(), [](const Action& a) { return !IsPlayFile(a.path); });
    for (const Action& action : actions) {
        if (Cancelled() || (gate_ != nullptr && gate_->Abandoned())) {
            break;
        }
        GameAction(scope, action);
    }
    SaveJournal(true);
    hashes.Save();
    const bool clean = own_state_.errors.empty() && !Cancelled();
    {
        std::lock_guard lock(engine_.mutex_);
        if (clean) {
            engine_.game_synced_[scope.game_id] = engine_.Now();
        }
    }
    files_.unlock();
    fs::remove_all(temp_dir_, ec);
    engine_.Touch();
    log::Info("sync", "{} {}: {} up, {} down{}", scope.before ? "before" : "after", scope.game_id,
              own_state_.uploaded, own_state_.downloaded,
              scope.out != nullptr && !scope.out->conflicts.empty() ? ", changed on both sides" : "");
    return own_state_.errors.empty() ? Status() : Status::Fail(own_state_.errors.front());
}

bool Run::ListFolders(const GameRef& game, const std::set<std::string>& categories, bool before,
                      std::map<std::string, RemoteFile>& remote) {
    std::vector<std::string> prefixes;
    for (const char* category : {SAVES, STATES}) {
        if (categories.contains(category)) {
            prefixes.push_back(std::string(category) + "/" + game.system + "/");
        }
    }
    if (!before && categories.contains(CONFIG)) {
        prefixes.emplace_back(PLAY_PREFIX); // this device's goes up; before, the kept ones name devices
    }
    const std::set<std::string> pc = engine_.PcSystems();
    bool listed = false;
    for (const std::string& prefix : prefixes) {
        Result<std::vector<RemoteFile>> files = provider_.List(prefix);
        if (!files) {
            Error("Listing " + prefix + ": " + files.error());
            continue;
        }
        listed = true;
        for (RemoteFile& file : files.value()) {
            if (!file.path.starts_with(prefix) || !IsPortablePath(file.path) || IsPcPath(file.path, pc)) {
                continue;
            }
            if (file.rev.empty()) {
                file.rev = std::to_string(file.size) + "@" + std::to_string(file.modified);
            }
            remote[file.path] = std::move(file);
        }
    }
    return listed;
}

void Run::GameAction(const GameScope& scope, const Action& action) {
    const std::string& path = action.path;
    const bool play = IsPlayFile(path);
    GameCheck* out = scope.out;
    if (!scope.before) {
        Current(path, local_.contains(path) ? local_.at(path).size : 0);
        Do(action);
        SaveJournal(true);
        engine_.NoteTouched(path);
        return;
    }
    // Before the game starts: nothing goes up, and the play time files only to name the
    // device of a choice the user has to make.
    const bool asking = out != nullptr && !out->conflicts.empty();
    if (action.op == Op::Upload || action.op == Op::DeleteRemote || (play && (!asking || action.op != Op::Download))) {
        if (action.op == Op::Upload && !play && out != nullptr) {
            ++out->waiting;
        }
        return;
    }
    const LocalItem* local = local_.contains(path) ? &local_.at(path) : nullptr;
    const RemoteFile* remote = RemoteOf(path);
    if ((action.op == Op::Compare || action.op == Op::Conflict) && local != nullptr && remote != nullptr) {
        const fs::path temp = Fetch(path);
        if (temp.empty()) {
            return;
        }
        const std::string hash = Sha256File(temp).value_or("");
        const auto entry = journal_->entries.find(path);
        const JournalEntry* agreed = entry != journal_->entries.end() ? &entry->second : nullptr;
        switch (AfterFetch(local->hash, hash, agreed)) {
        case Op::Adopt:
            Agree(path, hash, remote->rev, local->size);
            break;
        case Op::Upload:
            // Only its rev moved online: this device's change goes up when the game ends.
            Agree(path, agreed->hash, remote->rev, agreed->size);
            if (out != nullptr) {
                ++out->waiting;
            }
            break;
        default: {
            const fs::path kept = sync_dir_ / "pending" / temp.filename();
            std::error_code ec;
            fs::create_directories(kept.parent_path(), ec);
            fs::rename(temp, kept, ec);
            if (ec) {
                Error(path + ": could not be kept");
                break;
            }
            PendingConflict pending{path, kept, hash, *remote, local->file, local->hash,
                                    agreed ? agreed->hash : std::string(), agreed ? agreed->rev : std::string()};
            {
                std::lock_guard lock(engine_.mutex_);
                engine_.pending_conflicts_[scope.game_id].push_back(std::move(pending));
            }
            if (out != nullptr) {
                out->conflicts.push_back(path);
                out->online_time = std::max(out->online_time, remote->modified);
                out->local_time = std::max(out->local_time, local->modified);
            }
            break;
        }
        }
        std::error_code ec;
        fs::remove(temp, ec);
    } else {
        const int before = own_state_.downloaded;
        Do(action);
        if (out != nullptr && !play && own_state_.downloaded > before) {
            ++out->downloaded;
        }
    }
    SaveJournal(true);
    engine_.NoteTouched(path);
}

bool EngineImpl::LockFiles(std::unique_lock<std::timed_mutex>& lock, const std::atomic<bool>* give_up,
                           double seconds) {
    lock = std::unique_lock(files_mutex_, std::defer_lock);
    ++files_wanted_;
    const auto start = std::chrono::steady_clock::now();
    bool got = false;
    while (!(got = lock.try_lock_for(std::chrono::milliseconds(50)))) {
        const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if ((give_up != nullptr && give_up->load()) || stop_.load() || (seconds >= 0 && waited >= seconds)) {
            break;
        }
    }
    --files_wanted_;
    return got;
}

bool EngineImpl::SyncsSaves() const {
    const std::string id = Active();
    if (id.empty()) {
        return false;
    }
    const std::vector<std::string> on = Categories(id);
    return std::find(on.begin(), on.end(), SAVES) != on.end() || std::find(on.begin(), on.end(), STATES) != on.end();
}

GameCheck EngineImpl::CheckGame(const std::string& game_id, CheckGate& gate, double seconds) {
    GameCheck out;
    const std::string id = Active();
    if (id.empty() || stop_.load() || !SyncsSaves()) {
        return out;
    }
    {
        std::lock_guard lock(mutex_);
        for (const PendingConflict& old : pending_conflicts_[game_id]) {
            std::error_code ec;
            fs::remove(old.fetched, ec);
        }
        pending_conflicts_.erase(game_id);
    }
    std::unique_ptr<Provider> provider = MakeProvider(id, gate.Flag());
    if (!provider) {
        out.error = "The sync plugin is not installed.";
        return out;
    }
    Status status;
    try {
        status = Run(*this, *provider, gate.Flag()).GoGame({game_id, true, &gate, seconds, &out});
    } catch (const std::exception& e) {
        status = Status::Fail(e.what());
    }
    if (!status.ok) {
        out.error = status.error;
    }
    if (!out.conflicts.empty()) {
        const GameIndex games = Games();
        if (const GameRef* game = games.ById(game_id)) {
            out.online_device = LastPlayedBy(config_.data_dir / "sync" / "playtime", DeviceId(),
                                             game->system + "/" + game->stem);
        }
        if (out.online_device.empty()) {
            const std::vector<std::string> names = OtherDeviceNames(config_.data_dir / "sync" / "playtime", DeviceId());
            out.online_device = names.size() == 1 ? names.front() : std::string();
        }
        std::lock_guard lock(mutex_);
        for (PendingConflict& pending : pending_conflicts_[game_id]) {
            pending.from = out.online_device;
        }
    }
    if (gate.Abandoned() && out.conflicts.empty()) {
        log::Info("sync", "check of {} given up; the game starts", game_id);
    }
    return out;
}

Status EngineImpl::ResolveGame(const std::string& game_id, bool use_online) {
    std::vector<PendingConflict> list;
    {
        std::lock_guard lock(mutex_);
        if (const auto found = pending_conflicts_.find(game_id); found != pending_conflicts_.end()) {
            list = std::move(found->second);
            pending_conflicts_.erase(found);
        }
    }
    const std::string id = Active();
    if (list.empty() || id.empty()) {
        return {};
    }
    std::unique_lock<std::timed_mutex> files;
    if (!LockFiles(files, &stop_)) {
        return Status::Fail("The app is closing.");
    }
    Journal journal(SyncDir(id) / "journal.json");
    Status status;
    for (const PendingConflict& p : list) {
        std::error_code ec;
        const auto entry = journal.entries.find(p.path);
        const bool same_journal = entry == journal.entries.end()
                                      ? p.journal_hash.empty() && p.journal_rev.empty()
                                      : entry->second.hash == p.journal_hash && entry->second.rev == p.journal_rev;
        if (!same_journal || Sha256File(p.local).value_or("") != p.local_hash) {
            fs::remove(p.fetched, ec); // a run sorted it out meanwhile
            continue;
        }
        if (use_online) {
            fs::path mine = p.fetched;
            mine += ".mine";
            fs::copy_file(p.local, mine, fs::copy_options::overwrite_existing, ec);
            fs::path part = p.local;
            part += ".sync-part";
            fs::copy_file(p.fetched, part, fs::copy_options::overwrite_existing, ec);
            if (!ec) {
                SetFileTime(part, p.remote.modified);
                fs::rename(part, p.local, ec);
            }
            if (ec) {
                fs::remove(part, ec);
                fs::remove(mine, ec);
                status = Status::Fail(p.path + " could not be replaced.");
                continue;
            }
            KeepLoser(p.path, mine, config_.device, game_id);
            fs::remove(p.fetched, ec);
        } else {
            KeepLoser(p.path, p.fetched, p.from.empty() ? std::string("another device") : p.from, game_id);
        }
        // Both take the online copy as agreed on: this device's then counts as changed only
        // here, and goes up when the game ends.
        journal.entries[p.path] = {p.fetched_hash, p.remote.rev, p.remote.size, Now()};
        NoteTouched(p.path);
    }
    journal.Save();
    Touch();
    log::Info("sync", "{}: {} chosen for {} files", game_id, use_online ? "the online save" : "this device's save",
              list.size());
    return status;
}

Status EngineImpl::SyncGameOnce(const std::string& game_id, const std::atomic<bool>* cancel) {
    const std::string id = Active();
    if (id.empty() || stop_.load() || !SyncsSaves()) {
        return {};
    }
    std::unique_ptr<Provider> provider = MakeProvider(id, cancel != nullptr ? cancel : &stop_);
    if (!provider) {
        return Status::Fail("The sync plugin is not installed.");
    }
    try {
        return Run(*this, *provider, cancel != nullptr ? cancel : &stop_).GoGame({game_id, false, nullptr, -1, nullptr});
    } catch (const std::exception& e) {
        return Status::Fail(e.what());
    }
}

void EngineImpl::SyncGame(const std::string& game_id) {
    if (Active().empty() || config_.jobs == nullptr) {
        return;
    }
    if (game_id.empty() || !SyncsSaves()) {
        Request(game_id);
        return;
    }
    std::weak_ptr<EngineImpl> self = shared_from_this();
    config_.jobs->Run(
        "Sync " + game_id,
        [self, game_id](Job&) {
            const auto engine = self.lock();
            return engine ? engine->SyncGameOnce(game_id, &engine->stop_) : Status();
        },
        [self, game_id](Job&) {
            if (const auto engine = self.lock()) {
                engine->Request(game_id); // then the rest: play time, settings
            }
        });
}

} // namespace den::sync
