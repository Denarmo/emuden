// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Which feature plugins are on the device (bundled with the app, installed from the index,
// the developer folder) and which ones the index offers.

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/platform/pal.h"
#include "den/platform/webview.h"
#include "den/plugins/signature.h"
#include "den/store/settings.h"
#include "feature/host_impl.h"
#include "feature/index_source.h"

namespace den::plugins::feature {

namespace {

std::uint64_t FolderBytes(const fs::path& dir) {
    std::uint64_t bytes = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            bytes += it->file_size(ec);
        }
    }
    return bytes;
}

// The manifest in `dir`, when it is a feature plugin this source may publish.
std::optional<Package> ReadPackage(const fs::path& dir, FeaturePlugin::Trust trust,
                                   const std::string& source_owner) {
    const auto text = ReadTextFile(dir / "plugin.json");
    if (!text || !IsFeatureManifest(*text)) {
        return std::nullopt;
    }
    Result<FeatureManifest> manifest = ParseFeatureManifest(*text);
    const Status trusted = manifest ? CheckTrust(*manifest, trust, source_owner) : Status();
    if (!manifest || !trusted.ok) {
        log::Warn("plugins", "{}: {}", PathToUtf8(dir.filename()),
                  manifest ? trusted.error : manifest.error());
        return std::nullopt;
    }
    Package package;
    package.manifest = manifest.take();
    package.dir = dir;
    package.trust = trust;
    return package;
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

FeaturePlugin::Trust TrustOf(const SourceView& view) {
    return view.official ? FeaturePlugin::Trust::Official : FeaturePlugin::Trust::ThirdParty;
}

// The official source by the name its list gives itself, an added one by its address.
std::string SourceName(const SourceView& view) {
    if (!view.official) {
        return view.label;
    }
    return view.index && !view.index->source_name.empty() ? view.index->source_name : std::string("EmuDEN");
}

std::string TrustName(const Package& package, const std::vector<SourceView>& views, std::size_t origin) {
    if (package.trust == FeaturePlugin::Trust::Developer) {
        return "Developer folder";
    }
    return package.bundled || origin >= views.size() ? "EmuDEN" : SourceName(views[origin]);
}

} // namespace

fs::path RealFeatureHost::PathOf(const std::string& id) {
    return PathFromUtf8(id);
}

RealFeatureHost::RealFeatureHost(const FeatureConfig& config, FeatureDeps deps)
    : config_(config), deps_(std::move(deps)), plugins_dir_(config.data_dir / "plugins"),
      dev_dir_(config.data_dir / "plugins-dev"), data_root_(config.data_dir / "plugin-data") {
    if (deps_.defaults) {
        if (!deps_.http) {
            deps_.http = &pal::HttpGet;
        }
        if (deps_.bundled_dir.empty()) {
            deps_.bundled_dir = pal::AssetsDir() / "plugins";
        }
        if (deps_.platform.empty()) {
            deps_.platform = pal::GetCaps().os;
        }
        if (!deps_.services) {
            deps_.services = CreateLibraryServices(config.library);
        }
        if (!deps_.web_available) {
            deps_.web_available = [] { return pal::WebView::Available(); };
        }
        if (deps_.downloads_dir.empty()) {
            // "plugins.downloads_folder": where the user's browser saves, when not the usual one.
            const std::string chosen = config.settings != nullptr
                                           ? config.settings->GetString("plugins.downloads_folder")
                                           : std::string();
            deps_.downloads_dir = chosen.empty() ? pal::DownloadsDir() : PathFromUtf8(chosen);
        }
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
    fs::create_directories(data_root_, ec);
    Scan();
}

std::vector<SourceView> RealFeatureHost::Views() const {
    if (deps_.index) {
        SourceView view;
        view.address = deps_.index_url;
        view.label = deps_.source_owner;
        view.ns = ToLower(deps_.source_owner);
        view.official = deps_.source_trust == FeaturePlugin::Trust::Official;
        view.index = deps_.index();
        view.index_url = deps_.index_url;
        return {std::move(view)};
    }
    if (const auto* source = dynamic_cast<const IndexSource*>(config_.plugins)) {
        return source->SourceViews();
    }
    return {};
}

std::size_t RealFeatureHost::ViewOf(const std::vector<SourceView>& views, const Package& package) {
    if (package.bundled || package.trust == FeaturePlugin::Trust::Developer || package.origin.empty()) {
        return 0;
    }
    return FindView(views, package.origin);
}

FeatureAccess RealFeatureHost::ReadGrant(const std::string& id) const {
    const auto text = ReadTextFile(DataDir(id) / "grant.json");
    const auto root = text ? ParseJson(*text) : std::nullopt;
    return root ? AccessFromJson(*root) : FeatureAccess();
}

bool RealFeatureHost::WriteGrant(const std::string& id, const FeatureAccess& access) const {
    std::error_code ec;
    fs::create_directories(DataDir(id), ec);
    return WriteFileAtomic(DataDir(id) / "grant.json", WriteJson(AccessToJson(access)));
}

// The version of each plugin that runs: the developer folder's, else the newest installed
// one, else the one that ships with the app.
void RealFeatureHost::Scan() {
    std::map<std::string, Package> found;
    const auto take = [&](std::optional<Package> package, const fs::path& folder) {
        if (!package) {
            return;
        }
        const std::string id = package->manifest.id;
        if (PathToUtf8(folder.filename()) != id) {
            log::Warn("plugins", "{}: the folder is not named after the plugin", id);
            return;
        }
        const auto existing = found.find(id);
        if (existing != found.end() && package->trust != FeaturePlugin::Trust::Developer &&
            package->manifest.version < existing->second.manifest.version) {
            return;
        }
        found[id] = std::move(*package);
    };
    for (const fs::path& dir : ListDirectory(deps_.bundled_dir)) {
        std::optional<Package> package = ReadPackage(dir, FeaturePlugin::Trust::Official, {});
        if (package) {
            package->bundled = true;
        }
        take(std::move(package), dir);
    }
    const std::vector<SourceView> views = Views();
    for (const fs::path& dir : ListDirectory(plugins_dir_)) {
        // Without a source.json a plugin came from the first source, the official one.
        const std::string named = ReadOrigin(dir);
        const std::size_t origin = named.empty() ? 0 : FindView(views, named);
        FeaturePlugin::Trust trust = deps_.source_trust;
        std::string ns = deps_.source_owner;
        if (origin < views.size()) {
            trust = TrustOf(views[origin]);
            ns = views[origin].ns;
        } else if (!named.empty()) {
            continue; // its source was removed
        }
        for (const fs::path& version : ListDirectory(dir)) {
            std::optional<Package> package = ReadPackage(version, trust, ns);
            // A folder is a version only under its own name; an unfinished install is not.
            if (package && PathToUtf8(version.filename()) == package->manifest.version.Text()) {
                package->origin = origin < views.size() ? views[origin].address : std::string();
                take(std::move(package), dir);
            }
        }
    }
    if (config_.settings != nullptr && config_.settings->GetBool("plugins.developer", false)) {
        for (const fs::path& dir : ListDirectory(dev_dir_)) {
            take(ReadPackage(dir, FeaturePlugin::Trust::Developer, {}), dir);
        }
    }
    for (auto& [id, package] : found) {
        package.granted = ReadGrant(id);
        package.data_bytes = FolderBytes(DataDir(id));
    }
    std::lock_guard lock(mutex_);
    installed_ = std::move(found);
    ++revision_;
}

std::vector<FeaturePlugin> RealFeatureHost::Plugins() const {
    const std::vector<SourceView> views = Views();
    const Catalogue catalogue = MergeCatalogue(views);
    std::vector<FeaturePlugin> out;
    std::lock_guard lock(mutex_);
    for (const auto& [id, package] : installed_) {
        const FeatureManifest& manifest = package.manifest;
        const std::size_t origin = ViewOf(views, package);
        FeaturePlugin plugin;
        plugin.id = id;
        plugin.name = manifest.name;
        plugin.version = manifest.version.Text();
        plugin.publisher = manifest.publisher;
        plugin.summary = manifest.summary;
        plugin.kind = manifest.kind;
        plugin.dir = package.dir;
        plugin.source_name = TrustName(package, views, origin);
        plugin.trust = package.trust;
        plugin.installed = true;
        plugin.permissions = PermissionLines(manifest.access, manifest.sync_categories);
        plugin.sync_categories = manifest.sync_categories;
        plugin.data_bytes = package.data_bytes;
        std::error_code ec;
        if (!manifest.icon.empty() && fs::is_regular_file(package.dir / PathFromUtf8(manifest.icon), ec)) {
            plugin.icon = package.dir / PathFromUtf8(manifest.icon);
        }
        // An official plugin is granted what it declares; any other waits for the user.
        FeatureAccess beyond = AccessBeyond(manifest.access, package.granted);
        if (package.trust != FeaturePlugin::Trust::Official && !Empty(beyond)) {
            plugin.needs_consent = true;
            plugin.new_permissions = PermissionLines(beyond, manifest.sync_categories);
        }
        // Only the plugin's own source updates it.
        const IndexPlugin* offered =
            origin < views.size() && views[origin].Offers() ? views[origin].index->Find(id) : nullptr;
        if (offered != nullptr && offered->kind == manifest.kind &&
            package.trust != FeaturePlugin::Trust::Developer &&
            offered->version > manifest.version &&
            !RevokedFor(views, origin, id, offered->version)) {
            plugin.update_available = true;
            const FeatureAccess granted =
                Empty(package.granted) ? manifest.access : package.granted;
            beyond = AccessBeyond(AccessOf(*offered), granted);
            if (!Empty(beyond)) {
                plugin.needs_consent = true;
                plugin.new_permissions = PermissionLines(beyond);
            }
        }
        if (const auto reason = RevokedFor(views, origin, id, manifest.version)) {
            plugin.error = reason->empty() ? "This plugin was withdrawn by its source." : *reason;
        }
        if (const auto running = running_.find(id); running != running_.end()) {
            plugin.running = !running->second.runner->Stopped();
            if (const std::string error = running->second.runner->Error(); !error.empty()) {
                plugin.error = error;
            }
        }
        out.push_back(std::move(plugin));
    }
    for (const Offer& offer : catalogue.offers) {
        const IndexPlugin& offered = *offer.plugin;
        const SourceView& view = views[offer.source];
        if (!IsPackageKind(offered.kind) || installed_.contains(offered.id)) {
            continue;
        }
        FeaturePlugin plugin;
        plugin.id = offered.id;
        plugin.name = offered.name;
        plugin.version = offered.version.Text();
        plugin.publisher = offered.publisher;
        plugin.summary = offered.summary;
        plugin.kind = offered.kind;
        plugin.source_name = SourceName(view);
        plugin.trust = TrustOf(view);
        plugin.permissions = PermissionLines(AccessOf(offered));
        if (plugin.trust != FeaturePlugin::Trust::Official) {
            plugin.needs_consent = true;
            plugin.new_permissions = plugin.permissions;
        }
        out.push_back(std::move(plugin));
    }
    return out;
}

} // namespace den::plugins::feature
