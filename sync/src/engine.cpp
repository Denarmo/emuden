// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The engine's settings (all device-only, under "sync."), the providers and the triggers.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <system_error>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/platform/pal.h"
#include "den/plugins/feature_host.h"
#include "den/plugins/host.h"
#include "den/store/settings.h"
#include "engine_impl.h"

namespace den::sync {

namespace fs = std::filesystem;

namespace {

constexpr int DEFAULT_INTERVAL_MINUTES = 15;

bool Inside(const fs::path& path, const fs::path& folder) {
    const fs::path relative = path.lexically_normal().lexically_relative(folder.lexically_normal());
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

EngineImpl::EngineImpl(EngineConfig config) : config_(std::move(config)) {
    if (!config_.now) {
        config_.now = [] {
            return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                 std::chrono::system_clock::now().time_since_epoch())
                                                 .count());
        };
    }
    LoadConflicts();
    last_timer_ = pal::NowSeconds();
    if (config_.settings != nullptr) {
        state_.last_sync = static_cast<std::int64_t>(config_.settings->GetNumber(Key("last", Active())));
    }
}

std::string EngineImpl::Key(const std::string& name, const std::string& provider) const {
    return "sync." + name + (provider.empty() ? "" : "." + provider);
}

std::vector<ProviderInfo> EngineImpl::Providers() const {
    std::vector<ProviderInfo> out = config_.providers;
    if (config_.features == nullptr) {
        return out;
    }
    for (const plugins::FeaturePlugin& plugin : config_.features->Plugins()) {
        if (plugin.installed && !plugin.sync_categories.empty()) {
            out.push_back({plugin.id, plugin.name, plugin.sync_categories});
        }
    }
    return out;
}

std::string EngineImpl::Active() const {
    return config_.settings ? config_.settings->GetString("sync.active") : std::string();
}

void EngineImpl::SetActive(const std::string& id) {
    if (config_.settings == nullptr) {
        return;
    }
    if (id.empty()) {
        config_.settings->Remove("sync.active");
    } else {
        config_.settings->SetString("sync.active", id);
    }
    {
        std::lock_guard lock(mutex_);
        state_ = {};
        state_.last_sync = static_cast<std::int64_t>(config_.settings->GetNumber(Key("last", id)));
        remote_games_.clear();
        game_synced_.clear();
    }
    Touch();
    if (!id.empty()) {
        SyncNow();
    }
}

std::vector<std::string> EngineImpl::Categories(const std::string& provider) const {
    std::vector<std::string> offered;
    for (const ProviderInfo& info : Providers()) {
        if (info.id == provider) {
            offered = info.categories;
        }
    }
    if (config_.settings == nullptr) {
        return {};
    }
    const std::string key = Key("categories", provider);
    if (!config_.settings->Stored(key)) {
        std::erase(offered, std::string(BIOS)); // only when the user turns it on
        return offered;
    }
    std::vector<std::string> on = config_.settings->GetList(key);
    std::erase_if(on, [&](const std::string& category) {
        return std::find(offered.begin(), offered.end(), category) == offered.end();
    });
    return on;
}

void EngineImpl::SetCategory(const std::string& provider, const std::string& category, bool on) {
    std::vector<std::string> now = Categories(provider);
    std::erase(now, category);
    if (on) {
        now.push_back(category);
    }
    config_.settings->SetList(Key("categories", provider), now);
    Touch();
}

bool EngineImpl::UploadGames() const {
    return config_.settings && config_.settings->GetBool("sync.upload_games", false);
}

void EngineImpl::SetUploadGames(bool on) {
    config_.settings->SetBool("sync.upload_games", on);
    Touch();
}

fs::path EngineImpl::GamesFolder() const {
    const std::string chosen = config_.settings ? config_.settings->GetString("sync.games_folder") : std::string();
    return chosen.empty() ? config_.data_dir / "games" : PathFromUtf8(chosen);
}

void EngineImpl::SetGamesFolder(const fs::path& folder) {
    config_.settings->SetString("sync.games_folder", PathToUtf8(folder));
    Touch();
}

int EngineImpl::IntervalMinutes() const {
    return config_.settings
               ? static_cast<int>(config_.settings->GetNumber("sync.interval_minutes", DEFAULT_INTERVAL_MINUTES))
               : DEFAULT_INTERVAL_MINUTES;
}

void EngineImpl::SetIntervalMinutes(int minutes) {
    config_.settings->SetNumber("sync.interval_minutes", std::max(0, minutes));
    Touch();
}

fs::path EngineImpl::Folder(const std::string& provider) const {
    const std::string folder = config_.settings ? config_.settings->GetString(Key("folder", provider)) : std::string();
    return folder.empty() ? fs::path() : PathFromUtf8(folder);
}

Status EngineImpl::CheckFolder(const fs::path& folder) const {
    if (!folder.is_absolute() || folder.relative_path().empty() || !folder.has_parent_path() ||
        folder.parent_path() == folder) {
        return Status::Fail("Choose a folder, not a whole drive.");
    }
    const fs::path data = config_.data_dir;
    if (Inside(folder, data) || Inside(data, folder) || folder.lexically_normal() == data.lexically_normal()) {
        return Status::Fail("The sync folder cannot be inside EmuDEN's own data.");
    }
    if (config_.library != nullptr) {
        for (const library::Folder& games : config_.library->Folders()) {
            const fs::path path = PathFromUtf8(games.path);
            if (Inside(folder, path) || Inside(path, folder) || folder.lexically_normal() == path.lexically_normal()) {
                return Status::Fail("The sync folder cannot be one of your game folders.");
            }
        }
    }
    return {};
}

Status EngineImpl::UseFolder(const std::string& provider, const fs::path& folder) {
    if (const Status fine = CheckFolder(folder); !fine.ok) {
        return fine;
    }
    config_.settings->SetString(Key("folder", provider), PathToUtf8(folder));
    Touch();
    return {};
}

fs::path EngineImpl::Home() const {
    const fs::path downloads = pal::DownloadsDir();
    return downloads.empty() ? config_.data_dir.parent_path() : downloads.parent_path();
}

fs::path EngineImpl::SyncDir(const std::string& provider) const {
    return config_.data_dir / "sync" / PathFromUtf8(SafeFileName(provider));
}

std::set<std::string> EngineImpl::PcSystems() const {
    std::set<std::string> pc{"steam"};
    if (config_.consoles != nullptr) {
        for (const plugins::Console& console : config_.consoles->Consoles()) {
            if (console.group == "pc") {
                pc.insert(console.id);
            }
        }
    }
    return pc;
}

GameIndex EngineImpl::Games() const {
    if (config_.entries) {
        return GameIndex(config_.entries(), PcSystems());
    }
    return config_.library ? GameIndex(config_.library->Entries(), PcSystems()) : GameIndex({}, PcSystems());
}

std::unique_ptr<Provider> EngineImpl::MakeProvider(const std::string& id, const std::atomic<bool>* cancel) const {
    if (config_.provider_factory) {
        return config_.provider_factory(id);
    }
    // A provider runs only once its plugin is installed and the user allowed it, even when
    // its folder or settings are already there.
    if (config_.features != nullptr) {
        const auto plugins = config_.features->Plugins();
        const auto found = std::find_if(plugins.begin(), plugins.end(),
                                        [&](const plugins::FeaturePlugin& p) { return p.id == id; });
        if (found == plugins.end() || !found->installed || found->needs_consent) {
            return nullptr;
        }
    }
    if (const fs::path folder = Folder(id); !folder.empty()) {
        return CreateFolderStore(id, folder);
    }
    if (config_.features == nullptr) {
        return nullptr;
    }
    return CreatePluginProvider(config_.features, id, PcSystems(), cancel);
}

std::unique_ptr<ConfigFiles> EngineImpl::MakeConfigFiles() {
    PlaySource play;
    if (config_.own_play) {
        play.own = config_.own_play;
        play.set_others = config_.set_other_play;
    } else if (library::Library* library = config_.library) {
        play.own = [library] { return library->OwnPlay(); };
        play.set_others = [library](const std::map<std::string, library::PlayTotals>& others) {
            library->SetOtherPlay(others);
        };
    }
    return CombineConfigFiles(
        CreateSettingsFiles(config_.settings, config_.layers, config_.data_dir / "settings.json", PcSystems()),
        CreatePlayFiles(std::move(play), config_.data_dir / "sync" / "playtime", DeviceId(), config_.device));
}

std::string EngineImpl::DeviceId() {
    if (!config_.device_id.empty()) {
        return config_.device_id;
    }
    if (config_.settings == nullptr) {
        return {};
    }
    std::lock_guard lock(mutex_);
    std::string id = config_.settings->GetString("sync.device_id");
    if (id.empty()) {
        std::random_device random;
        std::mt19937_64 bits((static_cast<std::uint64_t>(random()) << 32) ^ random() ^
                             static_cast<std::uint64_t>(pal::NowSeconds() * 1000));
        char text[17];
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(bits()));
        id = text;
        config_.settings->SetString("sync.device_id", id);
    }
    return id;
}

std::string EngineImpl::OtherDevice() {
    const std::vector<std::string> names = OtherDeviceNames(config_.data_dir / "sync" / "playtime", DeviceId());
    return names.size() == 1 ? names.front() : std::string("another device");
}

std::string EngineImpl::RunningGame() const {
    std::lock_guard lock(mutex_);
    return running_game_;
}

void EngineImpl::SyncNow() {
    Request({});
}

void EngineImpl::Request(const std::string& first_game, bool timer) {
    if (Active().empty() || config_.jobs == nullptr) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (!first_game.empty()) {
        pending_games_.push_back(first_game);
    }
    pending_ = true;
    // Games still uploading give way: the next run syncs the rest first, then goes on with them.
    if (games_pass_.load() && !timer) {
        run_cancel_.store(true);
    }
    if (!job_running_) {
        StartRun();
    }
}

// Needs mutex_.
void EngineImpl::StartRun() {
    pending_ = false;
    job_running_ = true;
    std::vector<std::string> first = std::exchange(pending_games_, {});
    std::weak_ptr<EngineImpl> self = shared_from_this();
    job_ = config_.jobs->Run(
        "Sync",
        [self, first](Job&) {
            const auto engine = self.lock();
            if (!engine) {
                return Status();
            }
            return engine->RunOnce(first, &engine->stop_);
        },
        [self](Job&) {
            const auto engine = self.lock();
            if (!engine) {
                return;
            }
            std::lock_guard lock(engine->mutex_);
            engine->job_running_ = false;
            if (engine->pending_) {
                engine->StartRun();
            }
        });
}

void EngineImpl::Pump(const std::string& running_game) {
    std::string ended;
    {
        std::lock_guard lock(mutex_);
        if (running_game_ != running_game) {
            if (!running_game_.empty() && running_game.empty()) {
                ended = running_game_;
            }
            running_game_ = running_game;
            if (!running_game.empty() && games_pass_.load()) {
                run_cancel_.store(true); // no game uploads while the user plays
            }
        }
    }
    if (!ended.empty()) {
        SyncGame(ended); // its saves go up now, then the rest
    }
    const int minutes = IntervalMinutes();
    const double now = pal::NowSeconds();
    bool timer = false;
    {
        std::lock_guard lock(mutex_);
        if (minutes > 0 && now - last_timer_ >= minutes * 60.0) {
            last_timer_ = now;
            timer = true;
        }
    }
    if (timer) {
        Request({}, true);
    }
}

void EngineImpl::NoteDeleted(const fs::path& local_file) {
    const std::string active = Active();
    if (active.empty()) {
        return;
    }
    const GameIndex games = Games();
    std::unique_lock<std::timed_mutex> files;
    if (!LockFiles(files, &stop_)) {
        return;
    }
    Journal journal(SyncDir(active) / "journal.json");
    for (const auto& [path, entry] : journal.entries) {
        const auto target = TargetOf(path, games, config_.data_dir, GamesFolder());
        if (target && !target->file.empty() && target->file == local_file) {
            journal.deleted.insert(path);
            journal.Save();
            return;
        }
    }
}

std::shared_ptr<Engine> CreateEngine(EngineConfig config) {
    return std::make_shared<EngineImpl>(std::move(config));
}

} // namespace den::sync
