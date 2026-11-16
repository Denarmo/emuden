// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The conflict copies, the state the pages show, and games: the remote list and downloads
// into the games folder, which joins the library.

#include <algorithm>
#include <cstdio>
#include <system_error>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/library/library.h"
#include "engine_impl.h"

namespace den::sync {

namespace fs = std::filesystem;

namespace {

bool IsIndex(const std::string& name) {
    const std::string lower = ToLower(name);
    return lower.ends_with(".cue") || lower.ends_with(".m3u") || lower.ends_with(".gdi") || lower.ends_with(".ccd");
}

// yyyymmdd-hhmmss in UTC, from the days since 1970 (H. Hinnant's civil_from_days).
std::string Stamp(std::int64_t unix_seconds) {
    const std::int64_t days = unix_seconds >= 0 ? unix_seconds / 86400 : (unix_seconds - 86399) / 86400;
    const std::int64_t seconds = unix_seconds - days * 86400;
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    char text[32];
    std::snprintf(text, sizeof(text), "%04d%02d%02d-%02d%02d%02d", static_cast<int>(year), static_cast<int>(month),
                  static_cast<int>(day), static_cast<int>(seconds / 3600), static_cast<int>(seconds / 60 % 60),
                  static_cast<int>(seconds % 60));
    return text;
}

} // namespace

void EngineImpl::LoadConflicts() {
    const auto text = ReadTextFile(config_.data_dir / "sync" / "conflicts.json");
    const auto root = text ? ParseJson(*text) : std::nullopt;
    if (!root || root->kind != JsonValue::Kind::Array) {
        return;
    }
    for (const JsonValue& item : root->items) {
        Conflict conflict;
        conflict.path = item.String("path");
        conflict.file = PathFromUtf8(item.String("file"));
        conflict.from = item.String("from");
        conflict.game_id = item.String("game");
        if (const JsonValue* time = item.Find("time")) {
            conflict.time = static_cast<std::int64_t>(time->number);
        }
        if (IsRegularFile(conflict.file)) {
            conflicts_.push_back(std::move(conflict));
        }
    }
}

void EngineImpl::SaveConflicts() const {
    JsonValue list = JsonArray();
    for (const Conflict& conflict : conflicts_) {
        list.items.push_back(JsonObject({{"path", JsonString(conflict.path)},
                                         {"file", JsonString(PathToUtf8(conflict.file))},
                                         {"from", JsonString(conflict.from)},
                                         {"game", JsonString(conflict.game_id)},
                                         {"time", JsonNumber(static_cast<double>(conflict.time))}}));
    }
    std::error_code ec;
    fs::create_directories(config_.data_dir / "sync", ec);
    WriteFileAtomic(config_.data_dir / "sync" / "conflicts.json", WriteJson(list));
}

// The loser keeps the remote file's name with ".conflict-<device>-<yyyymmdd-hhmmss>".
void EngineImpl::KeepLoser(const std::string& path, const fs::path& copy, const std::string& from,
                           const std::string& game_id) {
    const std::int64_t now = Now();
    const std::string device = SafeFileName(from.empty() ? "device" : from);
    fs::path kept = config_.data_dir / "sync" / "conflicts" / PathFromUtf8(path);
    kept += ".conflict-" + device + "-" + Stamp(now);
    std::error_code ec;
    fs::create_directories(kept.parent_path(), ec);
    fs::rename(copy, kept, ec);
    if (ec) {
        fs::copy_file(copy, kept, fs::copy_options::overwrite_existing, ec);
        fs::remove(copy, ec);
    }
    std::lock_guard lock(mutex_);
    conflicts_.push_back({path, kept, from, now, game_id});
    SaveConflicts();
    ++revision_;
}

std::vector<Conflict> EngineImpl::Conflicts() const {
    std::lock_guard lock(mutex_);
    return conflicts_;
}

Status EngineImpl::UseConflict(std::size_t index) {
    Conflict conflict;
    {
        std::lock_guard lock(mutex_);
        if (index >= conflicts_.size()) {
            return Status::Fail("That conflict is gone.");
        }
        conflict = conflicts_[index];
        if (!conflict.game_id.empty() && conflict.game_id == running_game_) {
            return Status::Fail("Close the game first.");
        }
    }
    const GameIndex games = Games();
    std::unique_lock<std::timed_mutex> held;
    if (!LockFiles(held, &stop_)) {
        return Status::Fail("The app is closing.");
    }
    const auto target = TargetOf(conflict.path, games, config_.data_dir, GamesFolder());
    if (!target) {
        return Status::Fail("This device does not have that game any more.");
    }
    std::error_code ec;
    const fs::path swap = config_.data_dir / "sync" / "conflicts" / "swap.tmp";
    if (CategoryOf(conflict.path) == CONFIG) {
        // On a job: the settings stores lock for every call, as they do for the main thread.
        auto files = MakeConfigFiles();
        for (const LocalItem& item : files->Export(games)) {
            if (item.path == conflict.path) {
                WriteFileAtomic(swap, item.text);
            }
        }
        const Status applied = files->Import(conflict.path, ReadTextFile(conflict.file).value_or(""), games);
        if (!applied.ok) {
            return applied;
        }
    } else {
        fs::create_directories(target->file.parent_path(), ec);
        if (fs::is_regular_file(target->file, ec)) {
            fs::copy_file(target->file, swap, fs::copy_options::overwrite_existing, ec);
        }
        fs::path part = target->file;
        part += ".sync-part";
        fs::copy_file(conflict.file, part, fs::copy_options::overwrite_existing, ec);
        fs::rename(part, target->file, ec);
        if (ec) {
            return Status::Fail("The file could not be replaced.");
        }
        SetFileTime(target->file, Now()); // newer than the remote one: it goes up next
    }
    fs::remove(conflict.file, ec);
    {
        std::lock_guard lock(mutex_);
        // By its file: the list may have changed while the run lock was awaited.
        std::erase_if(conflicts_, [&](const Conflict& c) { return c.file == conflict.file; });
        SaveConflicts();
    }
    if (fs::is_regular_file(swap, ec)) {
        KeepLoser(conflict.path, swap, config_.device, conflict.game_id);
    }
    Touch();
    return {};
}

void EngineImpl::DismissConflict(std::size_t index) {
    std::lock_guard lock(mutex_);
    if (index < conflicts_.size()) {
        std::error_code ec;
        fs::remove(conflicts_[index].file, ec);
        conflicts_.erase(conflicts_.begin() + static_cast<std::ptrdiff_t>(index));
        SaveConflicts();
        ++revision_;
    }
}

SyncState EngineImpl::State() const {
    std::lock_guard lock(mutex_);
    SyncState state = state_;
    state.conflicts = static_cast<int>(conflicts_.size());
    return state;
}

GameSync EngineImpl::GameState(const std::string& game_id) const {
    GameSync out;
    const std::string active = Active();
    if (active.empty()) {
        return out;
    }
    const std::vector<std::string> categories = Categories(active);
    out.active = std::find(categories.begin(), categories.end(), SAVES) != categories.end() ||
                 std::find(categories.begin(), categories.end(), STATES) != categories.end();
    std::lock_guard lock(mutex_);
    if (const auto found = game_synced_.find(game_id); found != game_synced_.end()) {
        out.synced = found->second;
    }
    out.waiting = !game_id.empty() && game_id == running_game_;
    out.conflict = std::any_of(conflicts_.begin(), conflicts_.end(),
                               [&](const Conflict& c) { return c.game_id == game_id; });
    return out;
}

JobPtr EngineImpl::RefreshRemoteGames() {
    std::weak_ptr<EngineImpl> self = shared_from_this();
    return config_.jobs->Run("Remote games", [self](Job& job) {
        const auto engine = self.lock();
        if (!engine) {
            return Status();
        }
        const std::string id = engine->Active();
        if (id.empty()) {
            return Status::Fail("Choose a sync provider first.");
        }
        std::unique_ptr<Provider> provider = engine->MakeProvider(id, &engine->stop_);
        if (!provider) {
            return Status::Fail("The sync plugin is not installed.");
        }
        // Listing changes nothing, so it does not wait for a sync run, which can take long
        // while games go up.
        Result<std::vector<RemoteFile>> files = provider->List(std::string(GAMES) + "/");
        if (!files) {
            return files.status();
        }
        const std::set<std::string> pc = engine->PcSystems();
        const GameIndex games = engine->Games();
        const fs::path folder = engine->GamesFolder();
        std::map<std::string, std::vector<RemoteFile>> by_system;
        for (RemoteFile& file : files.value()) {
            const std::string system = SystemOf(file.path);
            if (CategoryOf(file.path) == GAMES && IsPortablePath(file.path) && !IsPcPath(file.path, pc)) {
                by_system[system].push_back(std::move(file));
            }
        }
        std::vector<RemoteGame> out;
        for (auto& [system, list] : by_system) {
            std::set<std::string> claimed;
            const auto add = [&](const RemoteFile& lead, std::vector<const RemoteFile*> members) {
                RemoteGame game;
                game.system = system;
                const std::string name = lead.path.substr(lead.path.rfind('/') + 1);
                game.name = PathToUtf8(PathFromUtf8(name).stem());
                game.paths.push_back(lead.path);
                game.size = lead.size;
                for (const RemoteFile* member : members) {
                    game.paths.push_back(member->path);
                    game.size += member->size;
                }
                game.present = games.Find(system, game.name) != nullptr ||
                               IsRegularFile(folder / PathFromUtf8(lead.path.substr(lead.path.find('/') + 1)));
                out.push_back(std::move(game));
            };
            // An index (cue, m3u) takes the files whose names start with its own.
            for (const RemoteFile& file : list) {
                const std::string name = file.path.substr(file.path.rfind('/') + 1);
                if (!IsIndex(name)) {
                    continue;
                }
                const std::string stem = ToLower(PathToUtf8(PathFromUtf8(name).stem()));
                std::vector<const RemoteFile*> members;
                for (const RemoteFile& other : list) {
                    const std::string other_name = ToLower(other.path.substr(other.path.rfind('/') + 1));
                    if (&other != &file && !IsIndex(other_name) && other_name.starts_with(stem)) {
                        members.push_back(&other);
                        claimed.insert(other.path);
                    }
                }
                claimed.insert(file.path);
                add(file, members);
            }
            for (const RemoteFile& file : list) {
                if (!claimed.contains(file.path)) {
                    add(file, {});
                }
            }
        }
        std::sort(out.begin(), out.end(), [](const RemoteGame& a, const RemoteGame& b) {
            return a.system != b.system ? a.system < b.system : ToLower(a.name) < ToLower(b.name);
        });
        {
            std::lock_guard lock(engine->mutex_);
            engine->remote_games_ = std::move(out);
        }
        engine->Touch();
        (void)job;
        return Status();
    });
}

std::vector<RemoteGame> EngineImpl::RemoteGames() const {
    std::lock_guard lock(mutex_);
    return remote_games_;
}

JobPtr EngineImpl::StartDownload(const RemoteGame& game) {
    std::weak_ptr<EngineImpl> self = shared_from_this();
    return config_.jobs->Run("Download " + game.name, [self, game](Job&) {
        const auto engine = self.lock();
        return engine ? engine->DownloadGame(engine->Active(), game.paths, &engine->stop_) : Status();
    });
}

Status EngineImpl::DownloadGame(const std::string& provider_id, const std::vector<std::string>& paths,
                                const std::atomic<bool>* cancel) {
    const std::set<std::string> pc = PcSystems();
    const fs::path folder = GamesFolder();
    std::set<std::string> systems;
    for (const std::string& path : paths) {
        if (CategoryOf(path) != GAMES || !IsPortablePath(path) || SystemOf(path).empty()) {
            return Status::Fail(path + " is not a game's file");
        }
        if (IsPcPath(path, pc)) {
            return Status::Fail("Games of PC consoles never sync.");
        }
        systems.insert(SystemOf(path));
    }
    std::unique_ptr<Provider> provider = MakeProvider(provider_id, cancel != nullptr ? cancel : &stop_);
    if (!provider) {
        return Status::Fail("The sync plugin is not installed.");
    }
    YieldGames();
    std::lock_guard run(run_mutex_);
    // A game from the library is never uploaded back: the journal knows it from here on.
    for (const std::string& path : paths) {
        // games/<system>/<file> lands as <games folder>/<system>/<file>: never in a folder
        // the user's own games are in.
        const fs::path target = folder / PathFromUtf8(path.substr(path.find('/') + 1));
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        fs::path part = target;
        part += ".sync-part";
        {
            std::lock_guard lock(mutex_);
            state_.current = path;
            state_.fraction = -1;
        }
        Touch();
        const Status got = provider->Get(path, part, [this](double done, double total) {
            std::lock_guard lock(mutex_);
            state_.fraction = total > 0 ? static_cast<float>(done / total) : done <= 1 ? static_cast<float>(done) : -1;
            ++revision_;
        });
        if (got.ok) {
            fs::rename(part, target, ec);
        }
        if (got.ok && !ec) {
            const std::uint64_t size = fs::file_size(target, ec);
            std::lock_guard files(files_mutex_);
            Journal journal(SyncDir(provider_id) / "journal.json");
            journal.entries[path] = {GameHash(size), std::string(), size, Now()};
            journal.Save();
        }
        if (!got.ok || ec) {
            fs::remove(part, ec);
            std::lock_guard lock(mutex_);
            state_.current.clear();
            return Status::Fail(got.ok ? "The game could not be written." : got.error);
        }
    }
    {
        std::lock_guard lock(mutex_);
        state_.current.clear();
    }
    Touch();
    // Each console's folder joins the library with its console set, then a scan finds them.
    if (config_.library != nullptr && config_.jobs != nullptr) {
        library::Library* library = config_.library;
        config_.jobs->PostToMain([library, folder, systems] {
            const std::vector<library::Folder> known = library->Folders();
            for (const std::string& system : systems) {
                const std::string path = PathToUtf8(folder / PathFromUtf8(system));
                const bool present = std::any_of(known.begin(), known.end(),
                                                 [&](const library::Folder& f) { return f.path == path; });
                if (!present) {
                    if (const Status added = library->AddFolder(path, system); !added) {
                        log::Warn("sync", "{} did not join the library: {}", path, added.error);
                    }
                }
            }
            library->Scan();
        });
    }
    return {};
}

} // namespace den::sync
