// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Feature plugins: packages of Lua scripts from the signed plugin index that add a page to
// the app (docs/feature-plugins.md). The host lists, installs and runs them; a script only
// reaches what its manifest declares and the user granted.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/json.h"
#include "den/base/result.h"
#include "den/plugins/feature_ui.h"
#include "den/plugins/host.h"

namespace den::library {
class Library;
}

namespace den::plugins {

class AchievementServices;
class SyncLink;
class TuneServices;

/// Package kinds the feature host installs: "feature" (a script) and "interface" (data only:
/// a look for the Home screen, docs/interface-plugins.md in emuden-plugins).
inline constexpr const char* KIND_FEATURE = "feature";
inline constexpr const char* KIND_INTERFACE = "interface";

struct PermissionLine {
    std::string permission, text;
};

struct FeaturePlugin {
    std::string id, name, version, publisher, summary, source_name;
    std::string kind = KIND_FEATURE; // or KIND_INTERFACE: data for the Home screen, nothing to open
    std::filesystem::path icon; // empty until installed or fetched
    std::filesystem::path dir;  // installed: the package's folder
    enum class Trust { Official, ThirdParty, Developer } trust = Trust::Official;
    bool installed = false, update_available = false, running = false;
    bool needs_consent = false; // install or update asks for permissions not yet granted
    std::vector<PermissionLine> permissions, new_permissions;
    std::vector<std::string> sync_categories; // a sync provider: what it can keep in step
    std::string sync_listing;                 // and how it lists (feature.sync.listing)
    std::uint64_t data_bytes = 0;
    std::string error; // last script failure, user-facing
};

class FeatureHost {
public:
    virtual ~FeatureHost() = default;
    virtual std::vector<FeaturePlugin> Plugins() const = 0; // installed first, then available
    virtual std::uint64_t Revision() const = 0;             // list changed
    /// Fails without consent when the plugin needs it.
    virtual JobPtr Install(const std::string& id, bool consent) = 0;
    virtual JobPtr Remove(const std::string& id, bool delete_data, bool delete_hacks) = 0;
    /// Starts the script, runs plugin.open. Fails for an interface, which has none.
    virtual Status Open(const std::string& id) = 0;
    virtual void Close(const std::string& id) = 0;  // the page left the stage
    virtual void Reload(const std::string& id) = 0; // developer plugins
    /// Reads the plugins on the device again: those of the developer folder come and go.
    virtual void Rescan() {}
    /// The plugin's page stack, top last; `revision` moves on every change.
    virtual std::vector<PageModel> Pages(const std::string& id, std::uint64_t& revision) const = 0;
    virtual void Send(const std::string& id, UiEvent event) = 0; // never blocks
    virtual void Back(const std::string& id) = 0;                // pops a pushed page
    virtual std::vector<Ask> TakeAsks(const std::string& id) = 0;
    virtual void Answer(std::uint64_t ask_id, bool accepted, const std::string& value = {}) = 0;
    /// A local file for an element's image, or empty while it loads (the revision moves when
    /// ready).
    virtual std::filesystem::path ImagePath(const std::string& id, const std::string& image) = 0;
    /// A file opened with the app: the plugin id that takes it, or empty.
    virtual std::string RouteFile(const std::filesystem::path& file) = 0;
    /// Main thread, every frame: applies what the workers produced.
    virtual void Pump() = 0;

    // Sync providers (docs/sync.md). From any thread.
    /// What den.sync of every provider reaches. Set once, before a provider runs.
    virtual void SetSyncLink(std::shared_ptr<SyncLink> link) {
        (void)link;
    }
    /// Runs plugin.<path...>(args...) of an installed plugin, starting its script without its
    /// page when needed, and hands the results to `done` (on the script's thread). With
    /// `progress` the function gets a progress function (done, total) as its last argument.
    virtual void Invoke(const std::string& id, std::vector<std::string> path, std::vector<JsonValue> args,
                        std::function<void(Result<std::vector<JsonValue>>)> done,
                        std::function<void(double, double)> progress = {}) {
        (void)id;
        (void)path;
        (void)args;
        (void)progress;
        done(Status::Fail("this app runs no plugins"));
    }
    /// Lends a file to a plugin until ReturnFile: the text its script names it by
    /// ("sync:<token>"), which den.http.request and den.files.read take.
    virtual std::string LendFile(const std::string& id, const std::filesystem::path& file) {
        (void)id;
        (void)file;
        return {};
    }
    virtual void ReturnFile(const std::string& token) {
        (void)token;
    }
};

struct FeatureConfig {
    std::filesystem::path data_dir;
    Jobs* jobs = nullptr;
    store::Settings* settings = nullptr;
    Host* plugins = nullptr;
    library::Library* library = nullptr;
    std::string app_version;
    /// What den.device.info, den.game and den.steam reach; null: those calls fail.
    std::shared_ptr<TuneServices> tune;
    /// What den.achievements reaches; null: those calls fail.
    std::shared_ptr<AchievementServices> achievements;
};

std::unique_ptr<FeatureHost> CreateFeatureHost(const FeatureConfig& config);

} // namespace den::plugins
