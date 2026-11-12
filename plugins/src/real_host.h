// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The plugin host: consoles from the manifests, cores from pinned downloads. The queries are
// here; fetching and installing are in host_install.cpp.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <system_error>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/plugins/index.h"
#include "den/plugins/installer.h"
#include "den/plugins/manifest.h"
#include "den/plugins/signature.h"
#include "den/store/settings.h"
#include "json_util.h"

namespace den::plugins {

namespace fs = std::filesystem;

inline constexpr std::int64_t POLL_INTERVAL_SECONDS = 6 * 3600;

struct Loaded {
    Manifest manifest;
    bool from_index = false; // its package came from the signed index, not from the app
};

inline std::string FileNameOf(const Download& download) {
    const std::string& path = download.archive == "zip" ? download.entry : download.url;
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline bool BiosPresent(const fs::path& dir, const BiosFile& file) {
    std::error_code ec;
    const fs::path path = dir / PathFromUtf8(file.path);
    if (file.path.ends_with("/")) {
        return fs::is_directory(path, ec) && !fs::is_empty(path, ec);
    }
    return fs::is_regular_file(path, ec);
}

inline std::vector<std::string> MissingBios(const fs::path& dir, const Console& console) {
    std::vector<std::string> missing;
    for (const BiosSet& set : console.bios) {
        bool any = false;
        std::vector<std::string> required;
        for (const BiosFile& file : set.files) {
            if (file.path.starts_with("@")) {
                continue; // answered by the engine
            }
            const bool present = BiosPresent(dir, file);
            any = any || present;
            if (!present && !file.optional) {
                required.push_back(file.path);
            }
        }
        if (set.any_one) {
            if (!any && !required.empty()) {
                missing.push_back(set.label.empty() ? required.front() : set.label);
            }
        } else {
            missing.insert(missing.end(), required.begin(), required.end());
        }
    }
    return missing;
}

class RealHost final : public Host {
public:
    RealHost(const Config& config, HostDeps deps)
        : config_(config), deps_(std::move(deps)), plugins_dir_(config.data_dir / "plugins"),
          bios_dir_(config.data_dir / "bios"), ledger_(config.data_dir / "cores") {
        if (!deps_.http) {
            deps_.http = &pal::HttpGet;
        }
        if (deps_.source.empty()) {
            deps_.source = OFFICIAL_SOURCE;
        }
        if (deps_.keys.empty()) {
            deps_.keys = OfficialKeys();
        }
        if (deps_.platform_key.empty()) {
            deps_.platform_key = pal::GetCaps().platform_key;
        }
        if (!deps_.now) {
            deps_.now = [] {
                return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                     std::chrono::system_clock::now().time_since_epoch())
                                                     .count());
            };
        }
        std::error_code ec;
        fs::create_directories(plugins_dir_, ec);
        fs::create_directories(bios_dir_, ec);
        fs::create_directories(ledger_.Directory(), ec);
        LoadManifests();
        LoadState();
        LoadCachedIndex();
    }

    std::vector<Console> Consoles() const override {
        std::lock_guard lock(mutex_);
        std::vector<Console> out;
        for (const Loaded& loaded : manifests_) {
            for (const SystemDef& system : loaded.manifest.systems) {
                if (std::none_of(out.begin(), out.end(),
                                 [&](const Console& c) { return c.id == system.console.id; })) {
                    out.push_back(Describe(loaded, system));
                }
            }
        }
        std::stable_sort(out.begin(), out.end(),
                         [](const Console& a, const Console& b) { return a.order < b.order; });
        return out;
    }

    std::optional<Console> FindConsole(const std::string& system) const override {
        std::lock_guard lock(mutex_);
        const Found found = Find(system);
        return found.system ? std::optional<Console>(Describe(*found.loaded, *found.system))
                            : std::nullopt;
    }

    std::optional<CoreInfo> CoreFor(const std::string& system) const override {
        std::lock_guard lock(mutex_);
        const Found found = Find(system);
        const Component* core = found.system ? ChosenCore(*found.loaded, *found.system) : nullptr;
        return core ? std::optional<CoreInfo>(Describe(*found.system, *core)) : std::nullopt;
    }

    std::vector<CoreInfo> CoresFor(const std::string& system) const override {
        std::lock_guard lock(mutex_);
        std::vector<CoreInfo> out;
        if (const Found found = Find(system); found.system != nullptr) {
            for (const Component* core :
                 found.loaded->manifest.CoresFor(*found.system, deps_.platform_key)) {
                out.push_back(Describe(*found.system, *core));
            }
        }
        return out;
    }

    void SelectCore(const std::string& system, const std::string& core_id) override {
        if (config_.settings != nullptr) {
            config_.settings->SetString("plugins.core." + system, core_id);
            ++revision_;
        }
    }

    std::vector<SettingSpec> SettingsFor(const std::string& system) const override {
        std::lock_guard lock(mutex_);
        std::vector<SettingSpec> out;
        if (const Found found = Find(system); found.system != nullptr) {
            for (const ManifestSetting& setting : found.loaded->manifest.settings) {
                if (setting.systems.empty() ||
                    std::find(setting.systems.begin(), setting.systems.end(), system) !=
                        setting.systems.end()) {
                    out.push_back(setting.spec);
                }
            }
        }
        return out;
    }

    bool Installed(const std::string& system) const override {
        std::lock_guard lock(mutex_);
        const Found found = Find(system);
        if (found.system == nullptr) {
            return false;
        }
        if (found.system->console.runner == Runner::Engine) {
            return true;
        }
        const Component* core = ChosenCore(*found.loaded, *found.system);
        return core != nullptr && CoreGood(*core);
    }

    Status VerifyCore(const fs::path& library) const override {
        std::lock_guard lock(mutex_);
        return ledger_.VerifyForLoad(library, Blocked());
    }

    JobPtr Install(const std::string& system) override {
        return config_.jobs->Run("Install " + system,
                                 [this, system](Job& job) { return DoInstall(job, system); });
    }

    JobPtr Remove(const std::string& system) override {
        return config_.jobs->Run("Remove " + system,
                                 [this, system](Job&) { return DoRemove(system); });
    }

    JobPtr Refresh() override {
        return config_.jobs->Run("Check for plugin updates", [this](Job& job) {
            job.SetMessage("Checking the plugin list");
            Status status = FetchIndex(job, true);
            if (status.ok) {
                status = SyncPackages(job, {});
            }
            return status;
        });
    }

    std::uint64_t Revision() const override {
        return revision_.load();
    }

private:
    struct Found {
        const Loaded* loaded = nullptr;
        const SystemDef* system = nullptr;
    };

    struct Checked {
        std::uintmax_t size = 0;
        fs::file_time_type time;
        bool good = false;
    };

    Found Find(const std::string& system) const {
        for (const Loaded& loaded : manifests_) {
            for (const SystemDef& def : loaded.manifest.systems) {
                if (def.console.id == system) {
                    return {&loaded, &def};
                }
            }
        }
        return {};
    }

    const Component* ChosenCore(const Loaded& loaded, const SystemDef& system) const {
        const auto cores = loaded.manifest.CoresFor(system, deps_.platform_key);
        if (cores.empty()) {
            return nullptr;
        }
        if (config_.settings != nullptr) {
            const std::string chosen =
                config_.settings->GetString("plugins.core." + system.console.id);
            for (const Component* core : cores) {
                if (core->id == chosen) {
                    return core;
                }
            }
        }
        return cores.front();
    }

    std::function<std::string(std::string_view)> Blocked() const {
        return [this](std::string_view sha256) {
            return index_ ? index_->BlockedReason(sha256).value_or(std::string()) : std::string();
        };
    }

    // The core is in the ledger and its file is the one that was installed. The file is
    // hashed once and again whenever its size or time changes.
    bool CoreGood(const Component& core) const {
        const CoreRecord* record = ledger_.Find(core.id);
        if (record == nullptr) {
            return false;
        }
        const fs::path file = ledger_.Directory() / PathFromUtf8(record->file);
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(file, ec);
        if (ec) {
            return false;
        }
        const fs::file_time_type time = fs::last_write_time(file, ec);
        Checked& checked = checked_[core.id];
        if (checked.size != size || checked.time != time) {
            checked = {size, time, ledger_.VerifyForLoad(file, Blocked()).ok};
        }
        return checked.good;
    }

    Console Describe(const Loaded& loaded, const SystemDef& system) const {
        Console console = system.console;
        if (console.runner == Runner::Engine) {
            console.installed = true;
        } else {
            const Component* core = ChosenCore(loaded, system);
            console.available = core != nullptr;
            console.installed = core != nullptr && CoreGood(*core);
            if (console.installed) {
                const CoreRecord* record = ledger_.Find(core->id);
                const Download* download = core->DownloadFor(deps_.platform_key);
                console.update_available = record != nullptr && download != nullptr &&
                                           record->download_sha256 != download->sha256;
            }
        }
        if (const IndexPlugin* offered = index_ ? index_->Find(loaded.manifest.id) : nullptr) {
            console.update_available =
                console.update_available ||
                (console.installed && offered->version > loaded.manifest.version);
        }
        return console;
    }

    CoreInfo Describe(const SystemDef& system, const Component& core) const {
        CoreInfo info;
        info.id = core.id;
        info.name = core.name;
        info.system = system.console.id;
        info.system_dir = bios_dir_;
        info.needs_gl = core.opengl;
        info.option_hints = core.forced_core_options;
        info.missing_bios = MissingBios(bios_dir_, system.console);
        info.installed = CoreGood(core);
        if (info.installed) {
            info.library = ledger_.Directory() / PathFromUtf8(ledger_.Find(core.id)->file);
        }
        return info;
    }

    void AddManifest(Manifest manifest, bool from_index) {
        const auto it = std::find_if(manifests_.begin(), manifests_.end(), [&](const Loaded& l) {
            return l.manifest.id == manifest.id;
        });
        if (it == manifests_.end()) {
            manifests_.push_back({std::move(manifest), from_index});
        } else if (manifest.version >= it->manifest.version) {
            *it = {std::move(manifest), from_index};
        }
    }

    void LoadManifests() {
        if (deps_.bundled) {
            std::size_t count = 0;
            const BundledManifest* bundled = BundledManifests(count);
            for (std::size_t i = 0; i < count; ++i) {
                Result<Manifest> manifest = ParseManifest(std::string_view(
                    reinterpret_cast<const char*>(bundled[i].data), bundled[i].size));
                if (manifest) {
                    AddManifest(manifest.take(), false);
                } else {
                    log::Warn("plugins", "bundled {}: {}", bundled[i].id, manifest.error());
                }
            }
        }
        for (const fs::path& dir : ListDirectory(plugins_dir_)) {
            const auto text = ReadTextFile(dir / "plugin.json");
            if (!text) {
                continue;
            }
            Result<Manifest> manifest = ParseManifest(*text);
            if (manifest && manifest->id == PathToUtf8(dir.filename())) {
                AddManifest(manifest.take(), true);
            } else {
                log::Warn("plugins", "{}: {}", PathToUtf8(dir.filename()),
                          manifest ? "the folder is not named after the plugin" : manifest.error());
            }
        }
    }

    void LoadState() {
        const auto text = ReadTextFile(plugins_dir_ / "state.json");
        if (const auto root = text ? ParseJson(*text) : std::nullopt) {
            last_sequence_ = static_cast<std::int64_t>(json::Number(*root, "last_sequence", 0));
            fetched_ = static_cast<std::int64_t>(json::Number(*root, "fetched", 0));
        }
    }

    void SaveState() const {
        WriteFileAtomic(plugins_dir_ / "state.json",
                        WriteJson(JsonObject({
                            {"last_sequence", JsonNumber(static_cast<double>(last_sequence_))},
                            {"fetched", JsonNumber(static_cast<double>(fetched_))},
                        })));
    }

    // The cached index is checked like a downloaded one before it is believed.
    void LoadCachedIndex() {
        const auto bytes = ReadTextFile(plugins_dir_ / "cache" / "index.json");
        const auto signature = ReadTextFile(plugins_dir_ / "cache" / "index.json.sig");
        if (!bytes || !signature) {
            return;
        }
        Index index;
        const IndexVerdict verdict =
            VerifyIndex(*bytes, *signature, deps_.keys, last_sequence_, deps_.now(), index);
        if (verdict.Usable()) {
            index_ = std::move(index);
            index_expired_ = verdict.check == IndexCheck::Expired;
        }
    }

    Status FetchIndex(Job& job, bool force);

    // Brings the manifests the index offers in a newer version (or that the app does not
    // have at all) from their packages. `only` limits it to one plugin, where a failure counts.
    Status SyncPackages(Job& job, const std::string& only);

    Status FetchPackage(Job& job, const IndexPlugin& offered, const std::string& index_url);

    Status DoInstall(Job& job, const std::string& system);

    Status DoRemove(const std::string& system);

    Config config_;
    HostDeps deps_;
    fs::path plugins_dir_;
    fs::path bios_dir_;

    mutable std::mutex mutex_;
    std::vector<Loaded> manifests_;
    CoreLedger ledger_;
    std::optional<Index> index_;
    bool index_expired_ = false;
    std::int64_t last_sequence_ = 0;
    std::int64_t fetched_ = 0;
    mutable std::map<std::string, Checked, std::less<>> checked_;
    std::atomic<std::uint64_t> revision_{1};
};

} // namespace den::plugins
