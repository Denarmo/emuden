// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "real_host.h"

namespace den::plugins {

Status RealHost::FetchAll(Job& job, bool force) {
    std::vector<std::pair<std::string, std::string>> due; // address, label
    {
        std::lock_guard lock(mutex_);
        for (std::size_t i = 0; i < views_.size(); ++i) {
            if (i == 0 || (views_[i].enabled && views_[i].revoked.empty())) {
                due.emplace_back(views_[i].address, views_[i].label);
            }
        }
    }
    Status result;
    std::vector<std::string> notes;
    for (std::size_t i = 0; i < due.size(); ++i) {
        const Status status = FetchSource(job, due[i].first, force);
        if (i == 0) {
            result = status;
        } else if (!status.ok) {
            notes.push_back(due[i].second + ": " + status.error);
        }
    }
    result.notes.insert(result.notes.end(), notes.begin(), notes.end());
    return result;
}

Status RealHost::FetchSource(Job& job, const std::string& address, bool force) {
    std::string index_url;
    std::vector<std::string> keys;
    std::int64_t last_sequence = 0;
    {
        std::lock_guard lock(mutex_);
        const std::size_t i = FindView(views_, address);
        if (i == views_.size()) {
            return {};
        }
        if (!force && views_[i].index && !views_[i].expired &&
            deps_.now() - states_[i].fetched < POLL_INTERVAL_SECONDS) {
            return {};
        }
        index_url = views_[i].index_url;
        keys = states_[i].keys;
        last_sequence = states_[i].last_sequence;
    }
    const auto get = [&](const std::string& url) {
        pal::HttpRequest request;
        request.url = url;
        request.max_body = MAX_INDEX_SIZE;
        request.progress = [&job](std::uint64_t, std::uint64_t) { return !job.CancelRequested(); };
        return deps_.http(request);
    };
    const pal::HttpResponse list = get(index_url);
    const pal::HttpResponse signature = list.ok() ? get(index_url + ".sig") : pal::HttpResponse{};
    Status status;
    Index index;
    IndexVerdict verdict;
    if (!list.ok() || !signature.ok()) {
        const pal::HttpResponse& failed = list.ok() ? signature : list;
        status = Status::Fail("The plugin source could not be reached" +
                              (failed.error.empty() ? std::string(".") : ": " + failed.error));
    } else {
        verdict = VerifyIndex(list.body, signature.body, keys, last_sequence, deps_.now(), index);
        if (!verdict.Usable()) {
            status = Status::Fail(verdict.message);
        }
    }
    std::lock_guard lock(mutex_);
    const std::size_t i = FindView(views_, address);
    if (i == views_.size()) {
        return {}; // removed meanwhile
    }
    SourceState& state = states_[i];
    if (!status.ok) {
        state.error = status.error;
        SaveSourceState(i);
        ++revision_;
        return status;
    }
    std::error_code ec;
    fs::create_directories(state.dir, ec);
    WriteFileAtomic(state.dir / "index.json", list.body);
    WriteFileAtomic(state.dir / "index.json.sig", signature.body);
    state.last_sequence = index.sequence;
    state.fetched = deps_.now();
    state.error = verdict.check == IndexCheck::Expired ? verdict.message : std::string();
    views_[i].index = std::move(index);
    views_[i].expired = verdict.check == IndexCheck::Expired;
    SaveSourceState(i);
    if (i == 0) {
        UpdateRevokedKeys();
    }
    checked_.clear();
    ++revision_;
    return {};
}

Status RealHost::SyncPackages(Job& job, const std::string& only) {
    std::vector<std::pair<IndexPlugin, std::string>> wanted; // with its source's address
    {
        std::lock_guard lock(mutex_);
        const Catalogue catalogue = MergeCatalogue(views_);
        for (const Offer& offer : catalogue.offers) {
            const IndexPlugin& offered = *offer.plugin;
            if ((!only.empty() && offered.id != only) || offered.kind != "console") {
                continue; // feature plugins are the feature host's
            }
            const std::string& address = views_[offer.source].address;
            const auto it = std::find_if(manifests_.begin(), manifests_.end(),
                                         [&](const Loaded& l) { return l.manifest.id == offered.id; });
            if (it != manifests_.end() && it->origin != address) {
                continue; // a plugin is only ever updated from the source it came from
            }
            if (it == manifests_.end() || offered.version > it->manifest.version) {
                wanted.emplace_back(offered, address);
            }
        }
    }
    Status result;
    for (const auto& [offered, address] : wanted) {
        const Status status = FetchPackage(job, offered, address);
        if (!status.ok) {
            log::Warn("plugins", "{}: {}", offered.id, status.error);
            if (!only.empty()) {
                return status;
            }
        }
    }
    return result;
}

Status RealHost::FetchPackage(Job& job, const IndexPlugin& offered, const std::string& address) {
    std::string index_url;
    std::size_t source = 0;
    {
        std::lock_guard lock(mutex_);
        source = FindView(views_, address);
        if (source == views_.size()) {
            return Status::Fail("The plugin's source was removed.");
        }
        index_url = views_[source].index_url;
    }
    const std::string url = ResolveSourceUrl(index_url, offered.package.url);
    const fs::path temp = plugins_dir_ / "tmp";
    const fs::path package = temp / PathFromUtf8(offered.id + ".emuplugin");
    const fs::path unpacked = temp / PathFromUtf8(offered.id + ".json");
    job.SetMessage("Getting " + offered.name);
    Status status = DownloadPinned(deps_.http, url, offered.package.sha256, offered.package.size, package, &job);
    if (status.ok) {
        status = UnpackEntry(package, "plugin.json", unpacked, MAX_MANIFEST_SIZE, &job);
    }
    const auto text = status.ok ? ReadTextFile(unpacked) : std::nullopt;
    std::error_code ec;
    fs::remove(package, ec);
    fs::remove(unpacked, ec);
    if (!status.ok) {
        return status;
    }
    Result<Manifest> manifest = text ? ParseManifest(*text) : Status::Fail("unreadable");
    if (!manifest) {
        return manifest.status();
    }
    if (manifest->id != offered.id || manifest->version != offered.version) {
        return Status::Fail("The plugin is not the one its source announced.");
    }
    {
        std::lock_guard lock(mutex_);
        if (source > 0 && !Allowed(*manifest, source)) {
            return Status::Fail(CheckThirdPartyManifest(*manifest, views_[source].ns).error);
        }
    }
    const fs::path dir = plugins_dir_ / PathFromUtf8(offered.id);
    fs::create_directories(dir, ec);
    // The origin goes first: a folder without it would count as the official source's.
    if (source == 0) {
        fs::remove(dir / ORIGIN_FILE, ec);
    } else if (!WriteOrigin(dir, address)) {
        return Status::Fail("The plugin could not be saved.");
    }
    if (!WriteFileAtomic(dir / "plugin.json", *text)) {
        return Status::Fail("The plugin could not be saved.");
    }
    std::lock_guard lock(mutex_);
    AddManifest(manifest.take(), true, address);
    ++revision_;
    return {};
}

Status RealHost::DoInstall(Job& job, const std::string& system, const std::string& core_id) {
    job.SetMessage("Checking the plugin list");
    Status result;
    // Without the list the manifest the app already has still pins every download.
    if (const Status fetched = FetchAll(job, false); !fetched.ok) {
        result.notes.push_back(fetched.error);
    }
    std::string plugin_id;
    {
        std::lock_guard lock(mutex_);
        const Found found = Find(system);
        if (found.system == nullptr) {
            return Status::Fail("EmuDEN does not know the console \"" + system + "\".");
        }
        plugin_id = found.loaded->manifest.id;
    }
    if (const Status synced = SyncPackages(job, plugin_id); !synced.ok) {
        return synced;
    }

    Loaded loaded;
    Component core;
    Download download;
    bool have_core = false;
    bool engine_app = false;
    std::vector<std::pair<Component, Download>> companions; // cores chosen for listed games
    {
        std::lock_guard lock(mutex_);
        const Found found = Find(system);
        if (found.system == nullptr) {
            return Status::Fail("EmuDEN does not know the console \"" + system + "\".");
        }
        loaded = *found.loaded;
        const std::size_t origin = OriginOf(loaded);
        const Manifest& manifest = loaded.manifest;
        if (const auto reason = RevokedFor(views_, origin, manifest.id, manifest.version)) {
            return Status::Fail(manifest.name + " was withdrawn: " + *reason);
        }
        if (found.system->console.runner == Runner::Engine) {
            return result; // the engine comes with the app
        }
        const Component* chosen = ChosenCore(*found.loaded, *found.system);
        if (!core_id.empty()) {
            chosen = nullptr;
            for (const Component* option : manifest.CoresFor(*found.system, deps_.platform_key)) {
                if (option->id == core_id) {
                    chosen = option;
                }
            }
        }
        if (chosen == nullptr) {
            return Status::Fail(found.system->console.name + " is not available on this device yet.");
        }
        core = *chosen;
        engine_app = core.type == TYPE_ENGINE_APP;
    }
    if (engine_app) {
        return InstallEngineApp(job, loaded, core, std::move(result));
    }
    {
        std::lock_guard lock(mutex_);
        const Found found = Find(system);
        if (found.system == nullptr) {
            return Status::Fail("EmuDEN does not know the console \"" + system + "\".");
        }
        const std::size_t origin = OriginOf(loaded);
        const Manifest& manifest = loaded.manifest;
        download = *core.DownloadFor(deps_.platform_key);
        if (const auto reason = BlockedFor(views_, origin, download.sha256)) {
            return Status::Fail(core.name + " must not be used: " + *reason);
        }
        for (const std::string& id : found.system->cores) {
            const Component* other = manifest.FindComponent(id);
            const Download* other_download = other != nullptr ? other->DownloadFor(deps_.platform_key) : nullptr;
            if (other != nullptr && other->type == TYPE_CORE && other->id != core.id && !other->games.Empty() &&
                other_download != nullptr && !CoreGood(*found.loaded, *other) &&
                !BlockedFor(views_, origin, other_download->sha256)) {
                companions.emplace_back(*other, *other_download);
            }
        }
        const std::string key = LedgerKey(loaded, core);
        const CoreRecord* record = ledger_.Find(key);
        if (record != nullptr && download.file.empty() && record->download_sha256 == download.sha256 &&
            CoreGood(loaded, core)) {
            ledger_.AddOwner(key, manifest.id);
            ledger_.Save();
            ++revision_;
            have_core = true;
        }
    }
    if (!have_core) {
        if (const Status status = InstallCore(job, loaded, core, download); !status.ok) {
            return status;
        }
    }
    InstallSystemFiles(job, loaded, core.id, result);
    // A core some games are played with comes along; the console plays without it.
    for (const auto& [other, other_download] : companions) {
        if (const Status status = InstallCore(job, loaded, other, other_download); !status.ok) {
            result.notes.push_back(other.name + " could not be installed: " + status.error);
            continue;
        }
        InstallSystemFiles(job, loaded, other.id, result);
    }
    job.SetProgress(1.0f);
    return result;
}

Status RealHost::InstallCore(Job& job, const Loaded& loaded, const Component& core, const Download& download) {
    const std::string file_name = FileNameOf(download);
    std::string key;
    std::string origin;
    {
        std::lock_guard lock(mutex_);
        const std::size_t source = OriginOf(loaded);
        key = LedgerKey(loaded, core);
        origin = loaded.developer ? DEVELOPER_ORIGIN
                 : loaded.local      ? LOCAL_ORIGIN
                 : source > 0        ? loaded.origin
                                     : (loaded.from_index ? "official" : "bundled");
        // Core files share one folder: an added source's core never takes another's file.
        for (const auto& [other_key, record] : ledger_.Records()) {
            if (other_key != key && EqualsNoCase(record.file, file_name) && (source > 0 || loaded.developer || loaded.local)) {
                return Status::Fail(core.name + " would replace the core file " + file_name + " of another plugin.");
            }
        }
    }
    job.SetMessage("Downloading " + core.name);
    const fs::path temp = ledger_.Directory() / "tmp" / PathFromUtf8(core.id + "-" + download.sha256.substr(0, 12));
    std::string download_sha256;
    Status status = FetchDownload(deps_.http, download, temp, &job, [&job](float f) { job.SetProgress(f * 0.9f); },
                                  &download_sha256);
    if (!status.ok) {
        return status;
    }
    job.SetMessage("Installing " + core.name);
    const fs::path library = ledger_.Directory() / PathFromUtf8(file_name);
    std::error_code ec;
    if (download.archive == "zip") {
        status = UnpackEntry(temp, download.entry, library, 0, &job);
    } else {
        fs::rename(temp, library, ec);
        status = ec ? Status::Fail("The core could not be put in place.") : Status{};
    }
    fs::remove(temp, ec);
    if (!status.ok) {
        return Status::Fail(status.error.empty() ? "The core could not be unpacked." : status.error);
    }
    std::lock_guard lock(mutex_);
    // The official core took the file of an added source's core: that one is gone.
    std::vector<std::string> replaced;
    for (const auto& [other_key, record] : ledger_.Records()) {
        if (other_key != key && EqualsNoCase(record.file, file_name) && record.origin != "official" &&
            record.origin != "bundled") {
            replaced.push_back(other_key);
        }
    }
    CoreRecord record;
    record.file = file_name;
    record.download_sha256 = download_sha256;
    record.url = download.file.empty() ? download.url : download.file;
    record.version = core.version;
    record.origin = origin;
    record.owners = {loaded.manifest.id};
    status = ledger_.Record(key, std::move(record));
    if (!status.ok) {
        return status;
    }
    for (const std::string& other_key : replaced) {
        ledger_.Forget(other_key);
        checked_.erase(other_key);
    }
    if (!ledger_.Save()) {
        return Status::Fail("The list of installed cores could not be saved.");
    }
    checked_.erase(key);
    ++revision_;
    return {};
}

void RealHost::InstallSystemFiles(Job& job, const Loaded& loaded, const std::string& core_id, Status& result) {
    const Manifest& manifest = loaded.manifest;
    std::vector<Component> wanted;
    std::size_t origin = 0;
    {
        std::lock_guard lock(mutex_);
        origin = OriginOf(loaded);
        for (const Component* files : manifest.SystemFilesFor(core_id, deps_.platform_key)) {
            if (!SystemFilesGood(manifest, *files)) {
                wanted.push_back(*files);
            }
        }
    }
    for (const Component& files : wanted) {
        const Download download = *files.DownloadFor(deps_.platform_key);
        job.SetMessage("Downloading " + files.name);
        const fs::path temp =
            ledger_.Directory() / "tmp" / PathFromUtf8(files.id + "-" + download.sha256.substr(0, 12));
        Status status;
        {
            std::lock_guard lock(mutex_);
            if (const auto reason = BlockedFor(views_, origin, download.sha256)) {
                status = Status::Fail("it must not be used: " + *reason);
            } else if ((origin > 0 || loaded.developer || loaded.local) && !system_files_.contains(SystemFilesKey(manifest, files)) &&
                       BiosPresent(bios_dir_, {download.entry, false})) {
                status = Status::Fail("it would replace files already in the BIOS folder");
            }
        }
        if (status.ok) {
            status = FetchDownload(deps_.http, download, temp, &job);
        }
        if (status.ok) {
            job.SetMessage("Installing " + files.name);
            status = UnpackFolder(temp, download.entry, bios_dir_, MAX_SYSTEM_FILES, MAX_SYSTEM_FILES_BYTES, &job);
        }
        std::error_code ec;
        fs::remove(temp, ec);
        if (!status.ok) {
            result.notes.push_back(files.name + " could not be installed: " + status.error);
            continue;
        }
        std::lock_guard lock(mutex_);
        system_files_[SystemFilesKey(manifest, files)] = download.sha256;
        SaveState();
        ++revision_;
    }
}

Status RealHost::DoRemove(const std::string& system) {
    std::lock_guard lock(mutex_);
    const Found found = Find(system);
    if (found.system == nullptr) {
        return Status::Fail("EmuDEN does not know the console \"" + system + "\".");
    }
    if (found.system->console.runner == Runner::Engine) {
        // Nothing of it but its plugin is on the device: removing the console removes that.
        const Loaded& loaded = *found.loaded;
        const fs::path dir = (loaded.local ? local_dir_ : plugins_dir_) / PathFromUtf8(loaded.manifest.id);
        std::error_code ec;
        if (loaded.developer || !fs::is_directory(dir, ec)) {
            return Status::Fail(found.system->console.name + " is part of EmuDEN.");
        }
        fs::remove_all(dir, ec);
        if (ec) {
            return Status::Fail("The plugin could not be removed.");
        }
        manifests_.clear(); // a built-in copy of it, if any, is used again
        LoadManifests();
        checked_.clear();
        ++revision_;
        return {};
    }
    const Component* core = ChosenCore(*found.loaded, *found.system);
    if (core == nullptr) {
        return {};
    }
    if (core->type == TYPE_ENGINE_APP) {
        return RemoveEngineApp(*found.loaded, *core);
    }
    const std::string key = LedgerKey(*found.loaded, *core);
    const Status status = ledger_.Remove(key);
    if (status.ok) {
        ledger_.Save();
        checked_.erase(key);
        ++revision_;
    }
    return status;
}

std::unique_ptr<Host> CreateHostWith(const Config& config, HostDeps deps) {
    return std::make_unique<RealHost>(config, std::move(deps));
}

std::unique_ptr<Host> CreateHost(const Config& config) {
    return CreateHostWith(config, {});
}

} // namespace den::plugins
