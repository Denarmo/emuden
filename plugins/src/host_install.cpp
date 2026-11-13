// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "real_host.h"

namespace den::plugins {

Status RealHost::FetchIndex(Job& job, bool force) {
    {
        std::lock_guard lock(mutex_);
        if (!force && index_ && !index_expired_ &&
            deps_.now() - fetched_ < POLL_INTERVAL_SECONDS) {
            return {};
        }
    }
    Result<SourceLocation> location = ParseSourceUrl(deps_.source);
    if (!location) {
        return location.status();
    }
    const auto get = [&](const std::string& url) {
        pal::HttpRequest request;
        request.url = url;
        request.max_body = MAX_INDEX_SIZE;
        request.progress = [&job](std::uint64_t, std::uint64_t) {
            return !job.CancelRequested();
        };
        return deps_.http(request);
    };
    const pal::HttpResponse list = get(location->index_url);
    const pal::HttpResponse signature =
        list.ok() ? get(location->index_url + ".sig") : pal::HttpResponse{};
    if (!list.ok() || !signature.ok()) {
        const pal::HttpResponse& failed = list.ok() ? signature : list;
        return Status::Fail("The plugin source could not be reached" +
                            (failed.error.empty() ? std::string(".") : ": " + failed.error));
    }
    std::lock_guard lock(mutex_);
    Index index;
    const IndexVerdict verdict =
        VerifyIndex(list.body, signature.body, deps_.keys, last_sequence_, deps_.now(), index);
    if (!verdict.Usable()) {
        return Status::Fail(verdict.message);
    }
    std::error_code ec;
    fs::create_directories(plugins_dir_ / "cache", ec);
    WriteFileAtomic(plugins_dir_ / "cache" / "index.json", list.body);
    WriteFileAtomic(plugins_dir_ / "cache" / "index.json.sig", signature.body);
    last_sequence_ = index.sequence;
    fetched_ = deps_.now();
    SaveState();
    index_ = std::move(index);
    index_expired_ = verdict.check == IndexCheck::Expired;
    checked_.clear();
    ++revision_;
    return {};
}

Status RealHost::SyncPackages(Job& job, const std::string& only) {
    std::vector<IndexPlugin> wanted;
    std::string index_url;
    {
        std::lock_guard lock(mutex_);
        if (!index_ || index_expired_) {
            return {};
        }
        for (const IndexPlugin& offered : index_->plugins) {
            if ((!only.empty() && offered.id != only) || offered.kind != "console") {
                continue; // feature plugins are the feature host's
            }
            const auto it =
                std::find_if(manifests_.begin(), manifests_.end(),
                             [&](const Loaded& l) { return l.manifest.id == offered.id; });
            const bool newer = it == manifests_.end() || offered.version > it->manifest.version;
            if (newer && !index_->RevokedReason(offered.id, offered.version)) {
                wanted.push_back(offered);
            }
        }
        index_url = ParseSourceUrl(deps_.source)->index_url;
    }
    for (const IndexPlugin& offered : wanted) {
        const Status status = FetchPackage(job, offered, index_url);
        if (!status.ok) {
            log::Warn("plugins", "{}: {}", offered.id, status.error);
            if (!only.empty()) {
                return status;
            }
        }
    }
    return {};
}

Status RealHost::FetchPackage(Job& job, const IndexPlugin& offered, const std::string& index_url) {
    const std::string url = ResolveSourceUrl(index_url, offered.package.url);
    const fs::path temp = plugins_dir_ / "tmp";
    const fs::path package = temp / PathFromUtf8(offered.id + ".emuplugin");
    const fs::path unpacked = temp / PathFromUtf8(offered.id + ".json");
    job.SetMessage("Getting " + offered.name);
    Status status = DownloadPinned(deps_.http, url, offered.package.sha256,
                                   offered.package.size, package, &job);
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
    const fs::path dir = plugins_dir_ / PathFromUtf8(offered.id);
    fs::create_directories(dir, ec);
    if (!WriteFileAtomic(dir / "plugin.json", *text)) {
        return Status::Fail("The plugin could not be saved.");
    }
    std::lock_guard lock(mutex_);
    AddManifest(manifest.take(), true);
    ++revision_;
    return {};
}

Status RealHost::DoInstall(Job& job, const std::string& system) {
    job.SetMessage("Checking the plugin list");
    Status result;
    // Without the list the manifest the app already has still pins every download.
    if (const Status fetched = FetchIndex(job, false); !fetched.ok) {
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

    Manifest manifest;
    Component core;
    Download download;
    bool from_index = false;
    bool have_core = false;
    {
        std::lock_guard lock(mutex_);
        const Found found = Find(system);
        if (found.system == nullptr) {
            return Status::Fail("EmuDEN does not know the console \"" + system + "\".");
        }
        manifest = found.loaded->manifest;
        from_index = found.loaded->from_index;
        if (index_) {
            if (const auto reason = index_->RevokedReason(manifest.id, manifest.version)) {
                return Status::Fail(manifest.name + " was withdrawn: " + *reason);
            }
        }
        if (found.system->console.runner == Runner::Engine) {
            return result; // the engine comes with the app
        }
        const Component* chosen = ChosenCore(*found.loaded, *found.system);
        if (chosen == nullptr) {
            return Status::Fail(found.system->console.name +
                                " is not available on this device yet.");
        }
        core = *chosen;
        download = *core.DownloadFor(deps_.platform_key);
        if (index_) {
            if (const auto reason = index_->BlockedReason(download.sha256)) {
                return Status::Fail(core.name + " must not be used: " + *reason);
            }
        }
        const CoreRecord* record = ledger_.Find(core.id);
        if (record != nullptr && record->download_sha256 == download.sha256 && CoreGood(core)) {
            ledger_.AddOwner(core.id, manifest.id);
            ledger_.Save();
            ++revision_;
            have_core = true;
        }
    }
    if (!have_core) {
        if (const Status status = InstallCore(job, manifest, core, download, from_index);
            !status.ok) {
            return status;
        }
    }
    InstallSystemFiles(job, manifest, core.id, result);
    job.SetProgress(1.0f);
    return result;
}

Status RealHost::InstallCore(Job& job, const Manifest& manifest, const Component& core,
                             const Download& download, bool from_index) {
    job.SetMessage("Downloading " + core.name);
    const fs::path temp =
        ledger_.Directory() / "tmp" / PathFromUtf8(core.id + "-" + download.sha256.substr(0, 12));
    Status status = DownloadPinned(deps_.http, download.url, download.sha256, download.size,
                                   temp, &job, [&job](float f) { job.SetProgress(f * 0.9f); });
    if (!status.ok) {
        return status;
    }
    job.SetMessage("Installing " + core.name);
    const std::string file_name = FileNameOf(download);
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
        return Status::Fail(status.error.empty() ? "The core could not be unpacked."
                                                 : status.error);
    }
    std::lock_guard lock(mutex_);
    CoreRecord record;
    record.file = file_name;
    record.download_sha256 = download.sha256;
    record.url = download.url;
    record.version = core.version;
    record.origin = from_index ? "official" : "bundled";
    record.owners = {manifest.id};
    status = ledger_.Record(core.id, std::move(record));
    if (!status.ok) {
        return status;
    }
    if (!ledger_.Save()) {
        return Status::Fail("The list of installed cores could not be saved.");
    }
    checked_.erase(core.id);
    ++revision_;
    return {};
}

void RealHost::InstallSystemFiles(Job& job, const Manifest& manifest, const std::string& core_id,
                                  Status& result) {
    std::vector<Component> wanted;
    {
        std::lock_guard lock(mutex_);
        for (const Component* files : manifest.SystemFilesFor(core_id, deps_.platform_key)) {
            if (!SystemFilesGood(manifest, *files)) {
                wanted.push_back(*files);
            }
        }
    }
    for (const Component& files : wanted) {
        const Download download = *files.DownloadFor(deps_.platform_key);
        job.SetMessage("Downloading " + files.name);
        const fs::path temp = ledger_.Directory() / "tmp" /
                              PathFromUtf8(files.id + "-" + download.sha256.substr(0, 12));
        Status status;
        {
            std::lock_guard lock(mutex_);
            if (const auto reason = index_ ? index_->BlockedReason(download.sha256) : std::nullopt) {
                status = Status::Fail("it must not be used: " + *reason);
            }
        }
        if (status.ok) {
            status = DownloadPinned(deps_.http, download.url, download.sha256, download.size, temp,
                                    &job);
        }
        if (status.ok) {
            job.SetMessage("Installing " + files.name);
            status = UnpackFolder(temp, download.entry, bios_dir_, MAX_SYSTEM_FILES,
                                  MAX_SYSTEM_FILES_BYTES, &job);
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
        return Status::Fail(found.system->console.name + " is part of EmuDEN.");
    }
    const Component* core = ChosenCore(*found.loaded, *found.system);
    if (core == nullptr) {
        return {};
    }
    const Status status = ledger_.Remove(core->id);
    if (status.ok) {
        ledger_.Save();
        checked_.erase(core->id);
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
