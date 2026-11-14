// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Engine apps as cores: what is installed and installing them. On the desktop the pinned
// download is unpacked under <data>/engines/<id>/ and its executable hashed, like a core.
// On Android the pinned APK goes to the system installer (the user confirms it), or the
// store page is opened; the installed package counts only when it is signed with the
// certificate the manifest pins.

#include "den/base/sha256.h"
#include "den/platform/apps.h"
#include "den/platform/engine_apps.h"
#include "real_host.h"

namespace den::plugins {

namespace {

constexpr std::size_t MAX_ENGINE_FILES = 20000;
constexpr std::uint64_t MAX_ENGINE_BYTES = 4ull << 30;
// How long an install waits for the user to finish in the system installer or the store.
constexpr double INSTALL_WAIT_SECONDS = 600;

} // namespace

fs::path RealHost::EnginesDir() const {
    return config_.data_dir / "engines";
}

fs::path RealHost::EngineDir(const std::string& key) const {
    return EnginesDir() / PathFromUtf8(key);
}

void RealHost::LoadEngines() const {
    if (engines_loaded_) {
        return;
    }
    engines_loaded_ = true;
    const auto text = ReadTextFile(EnginesDir() / "engines.json");
    const auto root = text ? ParseJson(*text) : std::nullopt;
    if (!root) {
        return;
    }
    for (const auto& [id, value] : root->members) {
        EngineRecord record;
        record.executable = value.String("executable");
        record.executable_sha256 = value.String("executable_sha256");
        record.download_sha256 = value.String("download_sha256");
        record.version = value.String("version");
        record.origin = value.String("origin");
        if (IsSafeRelativePath(id) && IsSafeRelativePath(record.executable) && IsSha256Hex(record.executable_sha256)) {
            engines_[id] = std::move(record);
        }
    }
}

bool RealHost::SaveEngines() const {
    std::vector<std::pair<std::string, JsonValue>> members;
    for (const auto& [id, record] : engines_) {
        members.emplace_back(id, JsonObject({{"executable", JsonString(record.executable)},
                                             {"executable_sha256", JsonString(record.executable_sha256)},
                                             {"download_sha256", JsonString(record.download_sha256)},
                                             {"version", JsonString(record.version)},
                                             {"origin", JsonString(record.origin)}}));
    }
    std::error_code ec;
    fs::create_directories(EnginesDir(), ec);
    return WriteFileAtomic(EnginesDir() / "engines.json", WriteJson(JsonObject(std::move(members))));
}

pal::engine_apps::Installed RealHost::QueryApp(const std::string& package, bool fresh) const {
    const std::uint64_t revision = pal::apps::Revision();
    auto it = apps_.find(package);
    if (fresh || it == apps_.end() || it->second.first != revision) {
        it = apps_.insert_or_assign(package, std::make_pair(revision, pal::engine_apps::Query(package))).first;
    }
    return it->second.second;
}

bool RealHost::AppMatches(const Component& core, const pal::engine_apps::Installed& app) {
    const EngineAppSpec& spec = core.engine_app;
    return app.present && app.plays && app.cert_sha256 == spec.cert_sha256 && app.version_code >= spec.min_version_code;
}

bool RealHost::EngineAppGood(const Loaded& loaded, const Component& core) const {
    if (IsAndroidKey(deps_.platform_key)) {
        return AppMatches(core, QueryApp(core.engine_app.package, false));
    }
    LoadEngines();
    const std::string key = LedgerKey(loaded, core);
    const auto it = engines_.find(key);
    if (it == engines_.end()) {
        return false;
    }
    const std::size_t origin = OriginOf(loaded);
    if (BlockedFor(views_, origin, it->second.download_sha256) ||
        BlockedFor(views_, origin, it->second.executable_sha256)) {
        return false;
    }
    const fs::path file = EngineDir(key) / PathFromUtf8(it->second.executable);
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec) {
        return false;
    }
    const fs::file_time_type time = fs::last_write_time(file, ec);
    Checked& checked = checked_["engine:" + key];
    if (checked.size != size || checked.time != time) {
        const auto sha = Sha256File(file);
        checked = {size, time, sha && *sha == it->second.executable_sha256};
    }
    return checked.good;
}

bool RealHost::EngineAppUpdate(const Loaded& loaded, const Component& core) const {
    if (IsAndroidKey(deps_.platform_key)) {
        return false; // the system's installer and store update apps
    }
    LoadEngines();
    const auto it = engines_.find(LedgerKey(loaded, core));
    const Download* download = core.DownloadFor(deps_.platform_key);
    return it != engines_.end() && download != nullptr && it->second.download_sha256 != download->sha256;
}

CoreInfo RealHost::DescribeEngineApp(const Loaded& loaded, const SystemDef& system, const Component& core) const {
    CoreInfo info;
    info.id = core.id;
    info.name = core.name;
    info.system = system.console.id;
    info.system_dir = bios_dir_;
    info.runner = Runner::Engine;
    const EngineAppSpec& spec = core.engine_app;
    info.engine.level = spec.level;
    info.engine.capabilities = spec.capabilities;
    info.engine.needs = spec.needs;
    info.engine.package = spec.package;
    info.engine.cert_sha256 = spec.cert_sha256;
    info.engine.store_url = spec.store_url;
    const bool android = IsAndroidKey(deps_.platform_key);
    info.engine.downloadable = android ? spec.apk.has_value() : core.DownloadFor(deps_.platform_key) != nullptr;
    info.installed = EngineAppGood(loaded, core);
    if (info.installed && !android) {
        const std::string key = LedgerKey(loaded, core);
        info.library = EngineDir(key) / PathFromUtf8(engines_.at(key).executable);
        info.engine.executable = info.library;
    }
    // The widescreen row works through the engine's standard setting.
    info.widescreen_options = {{"video.widescreen_patches", "true"}};
    return info;
}

Status RealHost::InstallEngineApp(Job& job, const Loaded& loaded, const Component& core, Status result) {
    if (IsAndroidKey(deps_.platform_key)) {
        return InstallAndroidApp(job, core, std::move(result));
    }
    const Download* found = core.DownloadFor(deps_.platform_key);
    const std::string executable = core.ExecutableFor(deps_.platform_key);
    if (found == nullptr || executable.empty()) {
        return Status::Fail(core.name + " is not available on this device.");
    }
    const Download download = *found;
    std::string key;
    {
        std::lock_guard lock(mutex_);
        key = LedgerKey(loaded, core);
        if (const auto reason = BlockedFor(views_, OriginOf(loaded), download.sha256)) {
            return Status::Fail(core.name + " must not be used: " + *reason);
        }
    }
    job.SetMessage("Downloading " + core.name);
    const fs::path temp = EnginesDir() / "tmp" / PathFromUtf8(core.id + "-" + download.sha256.substr(0, 12));
    const fs::path dir = EngineDir(key);
    std::error_code ec;
    fs::create_directories(temp.parent_path(), ec);
    Status status = DownloadPinned(deps_.http, download.url, download.sha256, download.size, temp, &job,
                                   [&job](float f) { job.SetProgress(f * 0.8f); });
    if (!status.ok) {
        return status;
    }
    job.SetMessage("Installing " + core.name);
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (download.archive == "zip" && download.entry.ends_with("/")) {
        status = UnpackFolder(temp, download.entry, dir, MAX_ENGINE_FILES, MAX_ENGINE_BYTES, &job);
    } else if (download.archive == "zip") {
        status = UnpackEntry(temp, download.entry, dir / PathFromUtf8(download.entry), 0, &job);
    } else {
        fs::create_directories((dir / PathFromUtf8(executable)).parent_path(), ec);
        fs::rename(temp, dir / PathFromUtf8(executable), ec);
        status = ec ? Status::Fail("The engine could not be put in place.") : Status{};
    }
    fs::remove(temp, ec);
    const fs::path file = dir / PathFromUtf8(executable);
    if (status.ok && !IsRegularFile(file)) {
        status = Status::Fail("The download does not hold " + executable + ".");
    }
    if (!status.ok) {
        fs::remove_all(dir, ec);
        return status;
    }
    fs::permissions(file, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    const auto sha = Sha256File(file);
    if (!sha) {
        return Status::Fail("The engine could not be read back.");
    }
    std::lock_guard lock(mutex_);
    LoadEngines();
    engines_[key] = {executable, *sha, download.sha256, core.version, loaded.origin};
    if (!SaveEngines()) {
        return Status::Fail("The list of installed engines could not be saved.");
    }
    checked_.erase("engine:" + key);
    ++revision_;
    job.SetProgress(1.0f);
    return result;
}

Status RealHost::InstallAndroidApp(Job& job, const Component& core, Status result) {
    const EngineAppSpec& spec = core.engine_app;
    const auto query = [this, &spec] {
        std::lock_guard lock(mutex_);
        return QueryApp(spec.package, true);
    };
    const pal::engine_apps::Installed before = query();
    if (before.present && before.cert_sha256 != spec.cert_sha256) {
        return Status::Fail("Another app named " + spec.package + " is installed, signed by someone else. "
                            "Uninstall it first.");
    }
    if (AppMatches(core, before)) {
        return result;
    }
    if (spec.apk) {
        const Download download = *spec.apk;
        job.SetMessage("Downloading " + core.name);
        const fs::path apk = EnginesDir() / "tmp" / PathFromUtf8(core.id + ".apk");
        std::error_code ec;
        fs::create_directories(apk.parent_path(), ec);
        if (Status status = DownloadPinned(deps_.http, download.url, download.sha256, download.size, apk, &job,
                                           [&job](float f) { job.SetProgress(f * 0.8f); });
            !status.ok) {
            return status;
        }
        job.SetMessage("Confirm the install of " + core.name);
        if (Status status = pal::engine_apps::Install(apk); !status.ok) {
            return status;
        }
    } else if (!spec.store_url.empty()) {
        job.SetMessage("Install " + core.name + " from the store, then come back");
        if (Status status = pal::engine_apps::OpenStore(spec.store_url); !status.ok) {
            return status;
        }
    } else {
        return Status::Fail(core.name + " is not available on this device.");
    }
    // The user answers the installer (or the store); the package appears when it is done.
    const double until = pal::NowSeconds() + INSTALL_WAIT_SECONDS;
    while (pal::NowSeconds() < until && !job.CancelRequested()) {
        const pal::engine_apps::Installed now = query();
        if (now.present && now.cert_sha256 != spec.cert_sha256) {
            return Status::Fail(core.name + " was installed with another signature than the plugin names, so "
                                "EmuDEN will not start it.");
        }
        if (AppMatches(core, now)) {
            std::error_code ec;
            fs::remove(EnginesDir() / "tmp" / PathFromUtf8(core.id + ".apk"), ec);
            ++revision_;
            job.SetProgress(1.0f);
            return result;
        }
        pal::SleepMs(500);
    }
    return Status::Fail(core.name + " was not installed.");
}

// Called with the lock held.
Status RealHost::RemoveEngineApp(const Loaded& loaded, const Component& core) {
    if (IsAndroidKey(deps_.platform_key)) {
        return Status::Fail(core.name + " is an app of its own: uninstall it in Android's settings.");
    }
    LoadEngines();
    const std::string key = LedgerKey(loaded, core);
    std::error_code ec;
    fs::remove_all(EngineDir(key), ec);
    if (ec) {
        return Status::Fail(core.name + " could not be removed (it may be running).");
    }
    engines_.erase(key);
    SaveEngines();
    checked_.erase("engine:" + key);
    ++revision_;
    return {};
}

} // namespace den::plugins
