// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Feature plugins: packages of Lua scripts from the signed plugin index that add a page to
// the app (docs/feature-plugins.md). The host lists, installs and runs them; a script only
// reaches what its manifest declares and the user granted.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/plugins/feature_ui.h"
#include "den/plugins/host.h"

namespace den::library {
class Library;
}

namespace den::plugins {

struct PermissionLine {
    std::string permission, text;
};

struct FeaturePlugin {
    std::string id, name, version, publisher, summary, source_name;
    std::filesystem::path icon; // empty until installed or fetched
    enum class Trust { Official, ThirdParty, Developer } trust = Trust::Official;
    bool installed = false, update_available = false, running = false;
    bool needs_consent = false; // install or update asks for permissions not yet granted
    std::vector<PermissionLine> permissions, new_permissions;
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
    virtual Status Open(const std::string& id) = 0; // starts the script, runs plugin.open
    virtual void Close(const std::string& id) = 0;  // the page left the stage
    virtual void Reload(const std::string& id) = 0; // developer plugins
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
};

struct FeatureConfig {
    std::filesystem::path data_dir;
    Jobs* jobs = nullptr;
    store::Settings* settings = nullptr;
    Host* plugins = nullptr;
    library::Library* library = nullptr;
    std::string app_version;
};

std::unique_ptr<FeatureHost> CreateFeatureHost(const FeatureConfig& config);

} // namespace den::plugins
