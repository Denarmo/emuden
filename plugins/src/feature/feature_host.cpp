// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Running feature plugins: starting a script, passing events and answers to it, dropping
// it some time after its page was left.

#include <algorithm>
#include <system_error>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/log.h"
#include "den/store/settings.h"
#include "feature/host_impl.h"

namespace den::plugins::feature {

namespace {

constexpr std::uint64_t MAX_IMAGE = 8ull << 20;
constexpr std::uint64_t MAX_ROUTED_FILE = 64ull << 20;

} // namespace

RealFeatureHost::~RealFeatureHost() {
    std::map<std::string, Running> running;
    {
        std::lock_guard lock(mutex_);
        running.swap(running_);
    }
    running.clear(); // each runner cancels its waits and joins its thread
}

std::shared_ptr<Runner> RealFeatureHost::RunnerOf(const std::string& id) const {
    std::lock_guard lock(mutex_);
    const auto found = running_.find(id);
    return found != running_.end() ? found->second.runner : nullptr;
}

std::shared_ptr<Runner> RealFeatureHost::TakeRunner(const std::string& id) {
    std::lock_guard lock(mutex_);
    const auto found = running_.find(id);
    if (found == running_.end()) {
        return nullptr;
    }
    std::shared_ptr<Runner> runner = std::move(found->second.runner);
    running_.erase(found);
    ++revision_;
    return runner;
}

void RealFeatureHost::Retire(std::shared_ptr<Runner> runner) {
    if (!runner) {
        return;
    }
    runner->Finish();
    if (config_.jobs != nullptr) {
        // Joining the script's thread may take as long as its plugin.close.
        config_.jobs->Run("Close plugin", [runner = std::move(runner)](Job&) mutable {
            runner.reset();
            return Status();
        });
    }
}

Result<std::shared_ptr<Runner>> RealFeatureHost::Start(const std::string& id) {
    const auto found = installed_.find(id);
    if (found == installed_.end()) {
        return Status::Fail("The plugin is not installed.");
    }
    Package& package = found->second;
    const std::vector<SourceView> views = Views();
    if (const auto reason = RevokedFor(views, ViewOf(views, package), id, package.manifest.version)) {
        return Status::Fail(reason->empty() ? "This plugin was withdrawn by its source." : *reason);
    }
    if (!Empty(AccessBeyond(package.manifest.access, package.granted))) {
        if (package.trust != FeaturePlugin::Trust::Official) {
            return Status::Fail("The plugin asks for permissions you have not accepted.");
        }
        package.granted = package.manifest.access;
        WriteGrant(id, package.granted);
    }
    if (const auto running = running_.find(id); running != running_.end()) {
        if (!running->second.runner->Stopped()) {
            return running->second.runner;
        }
        Retire(std::move(running->second.runner)); // failed three times: start afresh
        running_.erase(running);
    }
    RunnerConfig config;
    config.manifest = package.manifest;
    config.package_dir = package.dir;
    config.data_dir = DataDir(id);
    if (package.manifest.access.Has(PERMISSION_DOWNLOADS_WATCH)) {
        config.downloads_dir = deps_.downloads_dir;
    }
    if (package.manifest.access.Has(PERMISSION_BROWSE_ANY)) {
        config.bookmarks_file = config_.data_dir / "browser" / "bookmarks.json";
    }
    config.app_version = config_.app_version;
    config.language = [settings = config_.settings] {
        return settings != nullptr ? settings->GetString("ui.language", "en") : std::string("en");
    };
    config.platform = deps_.platform;
    config.developer = package.trust == FeaturePlugin::Trust::Developer;
    config.web_available = deps_.web_available;
    config.http = deps_.http;
    config.services = deps_.services;
    config.tune = deps_.tune ? deps_.tune : config_.tune;
    config.achievements = deps_.achievements ? deps_.achievements : config_.achievements;
    if (package.manifest.access.Has(PERMISSION_GAME_FILES)) {
        config.mods = deps_.mods ? deps_.mods : config_.mods;
        config.mods_dir = ModsDir(id);
    }
    config.now = deps_.now;
    config.instruction_limit = deps_.instruction_limit;
    if (package.manifest.access.Has(PERMISSION_SYNC)) {
        config.sync = sync_link_;
    }
    config.lent = [this, id](const std::string& token) -> std::optional<fs::path> {
        std::lock_guard lock(mutex_);
        const auto found = lent_.find(token);
        if (found == lent_.end() || found->second.first != id) {
            return std::nullopt;
        }
        return found->second.second;
    };
    Running entry;
    entry.runner = std::make_shared<Runner>(std::move(config));
    const std::shared_ptr<Runner> runner = entry.runner;
    running_[id] = std::move(entry);
    ++revision_;
    return runner;
}

Status RealFeatureHost::Open(const std::string& id) {
    std::lock_guard lock(mutex_);
    if (const auto found = installed_.find(id);
        found != installed_.end() && found->second.manifest.kind != KIND_FEATURE) {
        return Status::Fail("An interface is chosen under Settings, Interface: it has nothing to open.");
    }
    Result<std::shared_ptr<Runner>> runner = Start(id);
    if (!runner) {
        return runner.status();
    }
    Running& running = running_[id];
    // A plugin that already shows something (a routed file, a page left a moment ago) keeps it.
    if (!running.open && !(*runner)->HasPages()) {
        (*runner)->Call({"open"}, {JsonObject()});
    } else if (!running.open) {
        (*runner)->Call({"resume"}, {});
    }
    running.open = true;
    return {};
}

void RealFeatureHost::Close(const std::string& id) {
    std::lock_guard lock(mutex_);
    if (const auto found = running_.find(id); found != running_.end()) {
        found->second.open = false;
        found->second.closed_at = std::chrono::steady_clock::now();
    }
}

void RealFeatureHost::Reload(const std::string& id) {
    bool was_open = false;
    {
        std::lock_guard lock(mutex_);
        if (const auto found = running_.find(id); found != running_.end()) {
            was_open = found->second.open;
        }
    }
    if (std::shared_ptr<Runner> runner = TakeRunner(id)) {
        runner.reset(); // a reload does not wait for plugin.close
    }
    Scan();
    if (was_open) {
        if (const Status opened = Open(id); !opened) {
            log::Warn("plugins", "{} did not open again after a reload: {}", id, opened.error);
        }
    }
}

std::vector<PageModel> RealFeatureHost::Pages(const std::string& id,
                                              std::uint64_t& revision) const {
    revision = 0;
    const std::shared_ptr<Runner> runner = RunnerOf(id);
    return runner ? runner->Pages(revision) : std::vector<PageModel>();
}

void RealFeatureHost::Settle(const std::string& id) const {
    if (const std::shared_ptr<Runner> runner = RunnerOf(id)) {
        runner->Settle();
    }
}

void RealFeatureHost::Send(const std::string& id, UiEvent event) {
    if (const std::shared_ptr<Runner> runner = RunnerOf(id); runner && !event.action.empty()) {
        JsonValue arg;
        if (!event.arg_json.empty()) {
            if (auto parsed = ParseJson(event.arg_json)) {
                arg = std::move(*parsed);
            }
        }
        runner->Call({"actions", event.action}, {std::move(arg), JsonString(event.value)});
    }
}

void RealFeatureHost::Back(const std::string& id) {
    if (const std::shared_ptr<Runner> runner = RunnerOf(id)) {
        runner->Back();
    }
}

std::vector<Ask> RealFeatureHost::TakeAsks(const std::string& id) {
    const std::shared_ptr<Runner> runner = RunnerOf(id);
    return runner ? runner->TakeAsks() : std::vector<Ask>();
}

void RealFeatureHost::Answer(std::uint64_t ask_id, bool accepted, const std::string& value) {
    std::vector<std::shared_ptr<Runner>> runners;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [id, running] : running_) {
            runners.push_back(running.runner);
        }
    }
    for (const std::shared_ptr<Runner>& runner : runners) {
        if (runner->Answer(ask_id, accepted, value)) {
            return;
        }
    }
}

fs::path RealFeatureHost::ImagePath(const std::string& id, const std::string& image) {
    Package package;
    std::shared_ptr<Runner> runner;
    {
        std::lock_guard lock(mutex_);
        const auto found = installed_.find(id);
        if (found == installed_.end()) {
            return {};
        }
        package = found->second;
        if (const auto running = running_.find(id); running != running_.end()) {
            runner = running->second.runner;
        }
    }
    std::error_code ec;
    if (image.starts_with("plugin:")) {
        const std::string name = image.substr(7);
        if (!IsSafeRelativePath(name)) {
            return {};
        }
        for (const fs::path& root : {package.dir, DataDir(id)}) {
            const fs::path file = root / PathFromUtf8(name);
            if (fs::is_regular_file(file, ec)) {
                return file;
            }
        }
        return {};
    }
    if (image.starts_with("game:")) {
        const auto game = deps_.services ? deps_.services->Game(image.substr(5)) : std::nullopt;
        return game && package.manifest.access.SeesSystem(game->system)
                   ? PathFromUtf8(game->cover)
                   : fs::path();
    }
    // An https image of a declared host: fetched once through the plugin's own limits.
    if (!runner || !package.manifest.access.Has(PERMISSION_HTTP) ||
        !runner->Http().Allowed(image).ok || config_.jobs == nullptr) {
        return {};
    }
    const fs::path file =
        DataDir(id) / "cache" / "images" / (Sha256Hex(image.data(), image.size()) + ".img");
    if (fs::is_regular_file(file, ec)) {
        return file;
    }
    const std::string key = id + "\n" + image;
    {
        std::lock_guard lock(mutex_);
        if (!fetching_images_.insert(key).second) {
            return {};
        }
    }
    config_.jobs->Run("Plugin image", [this, runner, image, file, key](Job&) {
        std::error_code error;
        fs::create_directories(file.parent_path(), error);
        const Status status = runner->Http().Download(image, file, MAX_IMAGE, {});
        if (status.ok) {
            runner->Touch();
        }
        // A failed image is not asked for again while the plugin runs: the key stays.
        if (status.ok) {
            std::lock_guard lock(mutex_);
            fetching_images_.erase(key);
        }
        return Status();
    });
    return {};
}

std::string RealFeatureHost::RouteFile(const fs::path& file) {
    const std::string extension = ExtensionOf(file);
    std::error_code ec;
    if (extension.empty() || !fs::is_regular_file(file, ec) ||
        fs::file_size(file, ec) > MAX_ROUTED_FILE) {
        return {};
    }
    std::lock_guard lock(mutex_);
    for (const auto& [id, package] : installed_) {
        const FeatureManifest& manifest = package.manifest;
        if (!manifest.access.Has(PERMISSION_FILES_IMPORT) ||
            std::find(manifest.file_types.begin(), manifest.file_types.end(), extension) ==
                manifest.file_types.end()) {
            continue;
        }
        Result<std::shared_ptr<Runner>> runner = Start(id);
        if (!runner) {
            log::Warn("plugins", "{} cannot take {}: {}", id, PathToUtf8(file.filename()),
                      runner.error());
            continue;
        }
        const Result<std::string> copied =
            CopyIntoInbox(DataDir(id), file, manifest.file_types, MAX_ROUTED_FILE);
        if (!copied) {
            continue;
        }
        if (!(*runner)->HasPages()) {
            (*runner)->Call({"open"}, {JsonObject()});
        }
        (*runner)->Call({"file"}, {JsonString(copied.value())});
        Running& running = running_[id];
        running.open = true; // the screen shows it next; Open then keeps its pages
        return id;
    }
    return {};
}

void RealFeatureHost::Pump() {
    // The console host fetched a new index or changed the sources: versions, revocations,
    // updates and the plugins of a removed source may differ.
    if (config_.plugins != nullptr && config_.plugins->Revision() != host_revision_) {
        host_revision_ = config_.plugins->Revision();
        Scan();
    }
    std::vector<SourceView> views;
    {
        std::lock_guard lock(mutex_);
        if (!running_.empty()) {
            views = Views();
        }
    }
    std::vector<std::shared_ptr<Runner>> retired;
    std::string signature;
    {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto it = running_.begin(); it != running_.end();) {
            const auto package = installed_.find(it->first);
            const bool revoked =
                package != installed_.end() &&
                RevokedFor(views, ViewOf(views, package->second), it->first, package->second.manifest.version)
                    .has_value();
            if (it->second.runner->Invoking() > 0) {
                it->second.closed_at = now; // the sync engine uses it: it stays a while after
            }
            const bool idle = !it->second.open &&
                              now - it->second.closed_at >= std::chrono::seconds(deps_.idle_seconds);
            if (revoked || idle || package == installed_.end()) {
                retired.push_back(std::move(it->second.runner));
                it = running_.erase(it);
                continue;
            }
            signature += it->first + (it->second.runner->Stopped() ? "!" : ":") +
                         it->second.runner->Error() + "\n";
            ++it;
        }
        if (signature != state_signature_) {
            state_signature_ = std::move(signature);
            ++revision_;
        }
    }
    for (std::shared_ptr<Runner>& runner : retired) {
        Retire(std::move(runner));
    }
}

void RealFeatureHost::SetSyncLink(std::shared_ptr<SyncLink> link) {
    std::lock_guard lock(mutex_);
    sync_link_ = std::move(link);
}

void RealFeatureHost::Invoke(const std::string& id, std::vector<std::string> path, std::vector<JsonValue> args,
                             std::function<void(Result<std::vector<JsonValue>>)> done,
                             std::function<void(double, double)> progress) {
    std::shared_ptr<Runner> runner;
    {
        std::lock_guard lock(mutex_);
        const auto found = installed_.find(id);
        if (found == installed_.end() || found->second.manifest.kind != KIND_FEATURE) {
            done(Status::Fail("The plugin is not installed."));
            return;
        }
        Result<std::shared_ptr<Runner>> started = Start(id);
        if (!started) {
            done(started.status());
            return;
        }
        runner = started.take();
        running_[id].closed_at = std::chrono::steady_clock::now();
    }
    runner->Invoke(std::move(path), std::move(args), std::move(done), std::move(progress));
}

std::string RealFeatureHost::LendFile(const std::string& id, const fs::path& file) {
    std::lock_guard lock(mutex_);
    const std::string token =
        "sync:" + std::to_string(next_lend_++) + "-" +
        Sha256Hex(PathToUtf8(file).data(), PathToUtf8(file).size()).substr(0, 12);
    lent_[token] = {id, file};
    return token;
}

void RealFeatureHost::ReturnFile(const std::string& token) {
    std::lock_guard lock(mutex_);
    lent_.erase(token);
}

} // namespace den::plugins::feature

namespace den::plugins {

std::unique_ptr<FeatureHost> CreateFeatureHostWith(const FeatureConfig& config, FeatureDeps deps) {
    return std::make_unique<feature::RealFeatureHost>(config, std::move(deps));
}

std::unique_ptr<FeatureHost> CreateFeatureHost(const FeatureConfig& config) {
    return CreateFeatureHostWith(config, FeatureDeps());
}

} // namespace den::plugins
