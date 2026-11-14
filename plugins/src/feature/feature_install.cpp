// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Installing a feature plugin from the signed index, updating and removing it.

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/base/zip.h"
#include "feature/host_impl.h"
#include "feature/index_source.h"

namespace den::plugins::feature {

namespace {

bool AllowedInPackage(std::string_view name) {
    for (const char* extension : {".json", ".lua", ".png", ".jpg", ".txt", ".md"}) {
        if (name.ends_with(extension)) {
            return true;
        }
    }
    return false;
}

FeatureAccess AccessOf(const IndexPlugin& offered) {
    FeatureAccess access;
    access.permissions = offered.permissions;
    access.http_hosts = offered.http_hosts;
    access.open_hosts = offered.open_hosts;
    access.browse_hosts = offered.browse_hosts;
    access.systems = offered.systems;
    return access;
}

} // namespace

Status UnpackFeaturePackage(const fs::path& archive, const fs::path& dir, Job* job) {
    const auto entries = ListZip(archive);
    if (!entries || entries->empty()) {
        return Status::Fail("The plugin's package is not an archive.");
    }
    if (entries->size() > MAX_PACKAGE_FILES) {
        return Status::Fail("The plugin's package has too many files.");
    }
    std::uint64_t total = 0;
    bool manifest = false;
    for (const ZipEntry& entry : *entries) {
        total += entry.size;
        if (entry.symlink || entry.encrypted || !IsSafeRelativePath(entry.name) ||
            entry.name.ends_with("/") || !AllowedInPackage(entry.name) ||
            !CanExtractZipEntry(entry)) {
            return Status::Fail("The plugin's package holds a file it may not: " + entry.name);
        }
        manifest = manifest || entry.name == "plugin.json";
    }
    if (total > MAX_PACKAGE_BYTES || !manifest) {
        return Status::Fail(manifest ? "The plugin's package is too large."
                                     : "The plugin's package has no plugin.json.");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    for (const ZipEntry& entry : *entries) {
        const fs::path target = dir / PathFromUtf8(entry.name);
        fs::create_directories(target.parent_path(), ec);
        const Status status = ExtractZipEntry(archive, entry, target, [job] {
            return job != nullptr && job->CancelRequested();
        });
        if (!status.ok) {
            fs::remove_all(dir, ec);
            return status;
        }
    }
    return {};
}

JobPtr RealFeatureHost::Install(const std::string& id, bool consent) {
    return config_.jobs->Run("Install " + id, [this, id, consent](Job& job) {
        const Status status = DoInstall(job, id, consent);
        Scan();
        return status;
    });
}

JobPtr RealFeatureHost::Remove(const std::string& id, bool delete_data, bool delete_hacks) {
    return config_.jobs->Run("Remove " + id, [this, id, delete_data, delete_hacks](Job&) {
        const Status status = DoRemove(id, delete_data, delete_hacks);
        Scan();
        return status;
    });
}

Status RealFeatureHost::DoInstall(Job& job, const std::string& id, bool consent) {
    std::optional<Package> present;
    bool running = false;
    {
        std::lock_guard lock(mutex_);
        if (const auto found = installed_.find(id); found != installed_.end()) {
            present = found->second;
        }
        running = running_.contains(id);
    }
    // A developer plugin is only granted: its files are already there.
    if (present && present->trust == FeaturePlugin::Trust::Developer) {
        if (!consent) {
            return Status::Fail("The plugin asks for permissions you have not accepted.");
        }
        return WriteGrant(id, present->manifest.access)
                   ? Status()
                   : Status::Fail("The plugin's permissions could not be saved.");
    }
    // An installed plugin comes from its own source only; a new one from the source that
    // offers it first.
    const std::vector<SourceView> views = Views();
    std::size_t origin = views.size();
    if (present) {
        origin = ViewOf(views, *present);
    } else if (const Offer* offer = MergeCatalogue(views).Find(id)) {
        origin = offer->source;
    } else {
        for (std::size_t i = 0; i < views.size() && origin == views.size(); ++i) {
            const bool listed = views[i].enabled && views[i].revoked.empty() && views[i].index &&
                                views[i].index->Find(id) != nullptr;
            origin = listed && (views[i].official || CheckSourceNamespace(id, views[i].ns).ok) ? i : origin;
        }
    }
    const SourceView* view = origin < views.size() ? &views[origin] : nullptr;
    const bool usable = view != nullptr && view->enabled && view->revoked.empty() && view->index;
    const IndexPlugin* offered = usable ? view->index->Find(id) : nullptr;
    const FeaturePlugin::Trust trust =
        view == nullptr || view->official ? FeaturePlugin::Trust::Official : FeaturePlugin::Trust::ThirdParty;
    if (offered == nullptr || !IsPackageKind(offered->kind)) {
        // A plugin that ships with the app and has no newer version: accepting it is enough.
        if (present && !Empty(AccessBeyond(present->manifest.access, present->granted))) {
            if (present->trust != FeaturePlugin::Trust::Official && !consent) {
                return Status::Fail("The plugin asks for permissions you have not accepted.");
            }
            WriteGrant(id, present->manifest.access);
            return {};
        }
        return present ? Status() : Status::Fail("The plugin list does not offer this plugin.");
    }
    if (view->expired) {
        return Status::Fail("The plugin list is out of date. Check for updates first.");
    }
    if (const auto reason = RevokedFor(views, origin, id, offered->version)) {
        return Status::Fail(reason->empty() ? "This plugin was withdrawn by its source." : *reason);
    }
    if (const auto reason = BlockedFor(views, origin, offered->package.sha256)) {
        return Status::Fail(reason->empty() ? "This plugin was blocked by its source." : *reason);
    }
    if (present && !present->bundled && present->manifest.version >= offered->version) {
        return {};
    }
    // What the index announces is what the user sees and accepts before any download.
    const FeatureAccess announced = AccessOf(*offered);
    const FeatureAccess granted = ReadGrant(id);
    const bool first = !present || Empty(granted);
    const bool asks = first ? trust != FeaturePlugin::Trust::Official
                            : !Empty(AccessBeyond(announced, granted));
    if (asks && !consent) {
        return Status::Fail("The plugin asks for permissions you have not accepted.");
    }

    const std::string version = offered->version.Text();
    const fs::path temp = plugins_dir_ / "tmp";
    const fs::path archive = temp / PathFromUtf8(id + "-" + version + ".emuplugin");
    const fs::path unpacked = temp / PathFromUtf8(id + "-" + version);
    std::error_code ec;
    fs::create_directories(temp, ec);
    job.SetMessage("Getting " + offered->name);
    Status status =
        DownloadPinned(deps_.http, ResolveSourceUrl(view->index_url, offered->package.url),
                       offered->package.sha256, offered->package.size, archive, &job);
    if (status.ok) {
        status = UnpackFeaturePackage(archive, unpacked, &job);
    }
    fs::remove(archive, ec);
    std::optional<Package> package;
    if (status.ok) {
        const auto text = ReadTextFile(unpacked / "plugin.json");
        Result<FeatureManifest> manifest =
            text ? ParseFeatureManifest(*text) : Status::Fail("The plugin has no manifest.");
        if (!manifest) {
            status = manifest.status();
        } else if (manifest->id != id || manifest->version != offered->version ||
                   manifest->kind != offered->kind) {
            status = Status::Fail("The plugin is not the one its source announced.");
        } else if (!Empty(AccessBeyond(manifest->access, announced))) {
            status = Status::Fail("The plugin asks for more than its source announced.");
        } else {
            status = CheckTrust(*manifest, trust, view->ns);
            package = Package{manifest.take(), {}, trust, false, {}, 0, view->address};
        }
    }
    if (!status.ok) {
        fs::remove_all(unpacked, ec);
        return status;
    }
    const fs::path home = plugins_dir_ / PathOf(id);
    const fs::path final_dir = home / PathFromUtf8(version);
    fs::create_directories(home, ec);
    // The origin goes first: a folder without it would count as the official source's.
    if (trust == FeaturePlugin::Trust::Official) {
        fs::remove(home / ORIGIN_FILE, ec);
    } else if (!WriteOrigin(home, view->address)) {
        fs::remove_all(unpacked, ec);
        return Status::Fail("The plugin could not be saved.");
    }
    fs::remove_all(final_dir, ec);
    fs::rename(unpacked, final_dir, ec);
    if (ec) {
        fs::remove_all(unpacked, ec);
        return Status::Fail("The plugin could not be saved.");
    }
    if (!WriteGrant(id, package->manifest.access)) {
        return Status::Fail("The plugin's permissions could not be saved.");
    }
    // Older versions go once nothing runs from them; a running script keeps its files until
    // the next start.
    if (!running) {
        for (const fs::path& other : ListDirectory(home)) {
            if (other != final_dir && other.filename() != ORIGIN_FILE) {
                fs::remove_all(other, ec);
            }
        }
    }
    log::Info("plugins", "installed {} {}", id, version);
    return {};
}

Status RealFeatureHost::DoRemove(const std::string& id, bool delete_data, bool delete_hacks) {
    std::optional<Package> present;
    {
        std::lock_guard lock(mutex_);
        if (const auto found = installed_.find(id); found != installed_.end()) {
            present = found->second;
        }
    }
    if (!present) {
        return Status::Fail("The plugin is not installed.");
    }
    if (std::shared_ptr<Runner> runner = TakeRunner(id)) {
        runner->Finish();
        runner.reset(); // waits for plugin.close, cancels what the script waits for
    }
    Status result;
    if (delete_hacks && deps_.services) {
        for (const HackFacts& hack : deps_.services->Hacks(id)) {
            if (const Status removed = deps_.services->RemoveHack(hack.id, id); !removed.ok) {
                result.notes.push_back(hack.name + ": " + removed.error);
            }
        }
    }
    std::error_code ec;
    fs::remove_all(plugins_dir_ / PathOf(id), ec);
    if (delete_data) {
        fs::remove_all(DataDir(id), ec);
    } else {
        fs::remove(DataDir(id) / "grant.json", ec); // installing again asks again
    }
    if (present->bundled) {
        result.notes.push_back(present->manifest.name + " comes with the app and stays.");
    }
    return result;
}

} // namespace den::plugins::feature
