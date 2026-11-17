// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Engine apps are set up with what the player gave EmuDEN (ECP configure, SPEC 6.6): before
// each game, and outside a session once one is installed and whenever its BIOS files, game
// folders or option values change, so the app opened on its own has no first-run setup.

#include "den/app/engine_setup.h"

#include <algorithm>
#include <map>
#include <set>
#include <system_error>

#include "den/app/bios_import.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/platform/apps.h"
#include "den/platform/engine_apps.h"
#include "den/platform/pal.h"
#include "den/library/library.h"
#include "den/store/settings.h"

namespace den::app {

namespace fs = std::filesystem;

namespace {

constexpr double CHECK_SECONDS = 2.0;
// Option values carry no revision: they are looked at again this often.
constexpr double RECHECK_SECONDS = 60.0;

bool Within(const fs::path& path, const fs::path& folder) {
    auto p = path.begin();
    for (auto f = folder.begin(); f != folder.end(); ++f, ++p) {
        if (p == path.end() || *p != *f) {
            return false;
        }
    }
    return true;
}

bool Configurable(const plugins::CoreInfo& core) {
    const auto& caps = core.engine.capabilities;
    return core.runner == plugins::Runner::Engine && core.installed && !core.engine.package.empty() &&
           std::find(caps.begin(), caps.end(), "configure") != caps.end();
}

// What was last sent to each package outside a session (not synced: it is this device's).
fs::path LedgerPath() {
    return Svc().data_dir / "engines" / "configured.json";
}

std::string Describe(const engine::EngineSetup& setup) {
    std::vector<std::pair<std::string, JsonValue>> settings;
    for (const auto& [key, value] : setup.settings) {
        settings.emplace_back(key, JsonString(value));
    }
    const auto paths = [](const std::vector<fs::path>& list) {
        std::vector<JsonValue> out;
        for (const fs::path& path : list) {
            out.push_back(JsonString(PathToUtf8(path)));
        }
        return JsonArray(std::move(out));
    };
    return WriteJson(JsonObject({{"bios", paths(setup.bios)},
                                 {"game_folders", paths(setup.game_folders)},
                                 {"settings", JsonObject(std::move(settings))}}));
}

void ConfigureInstalled() {
    Services& svc = Svc();
    std::map<std::string, std::string> sent;
    if (const auto text = ReadTextFile(LedgerPath())) {
        if (const auto root = ParseJson(*text)) {
            for (const auto& [package, value] : root->members) {
                sent[package] = value.string;
            }
        }
    }
    bool changed = false;
    for (const plugins::Console& console : svc.plugins->Consoles()) {
        for (const plugins::CoreInfo& core : svc.plugins->CoresFor(console.id)) {
            if (!Configurable(core)) {
                continue;
            }
            const engine::EngineSetup setup = EngineSetupFor(core);
            const std::string text = Describe(setup);
            if (setup.Empty() || sent[core.engine.package] == text) {
                continue;
            }
            if (const Status status = engine::ConfigureApp(EngineInstallFor(core), setup); !status) {
                log::Warn("engine", "{} was not set up: {}", core.engine.package, status.error);
                continue;
            }
            log::Info("engine", "{} set up with {} BIOS locations and {} game folders", core.engine.package,
                      setup.bios.size(), setup.game_folders.size());
            sent[core.engine.package] = text;
            changed = true;
        }
    }
    if (changed) {
        std::vector<std::pair<std::string, JsonValue>> members;
        for (const auto& [package, text] : sent) {
            members.emplace_back(package, JsonString(text));
        }
        std::error_code ec;
        fs::create_directories(LedgerPath().parent_path(), ec);
        WriteFileAtomic(LedgerPath(), WriteJson(JsonObject(std::move(members))));
    }
}

void Frame() {
    Services& svc = Svc();
    if (!svc.plugins || !svc.library || !svc.engine || !pal::engine_apps::Available()) {
        return;
    }
    static double next = 0;
    static std::vector<std::uint64_t> seen;
    static double recheck = 0;
    const double now = pal::NowSeconds();
    if (now < next || svc.engine->Running()) {
        return;
    }
    next = now + CHECK_SECONDS;
    std::vector<std::uint64_t> revisions = {svc.plugins->Revision(), svc.library->Revision(),
                                            static_cast<std::uint64_t>(BiosImports()), pal::apps::Revision()};
    if (revisions == seen && now < recheck) {
        return;
    }
    seen = std::move(revisions);
    recheck = now + RECHECK_SECONDS;
    ConfigureInstalled();
}

} // namespace

engine::EngineSetup EngineSetupFor(const plugins::CoreInfo& core) {
    Services& svc = Svc();
    engine::EngineSetup setup;
    std::error_code ec;
    if (const auto console = svc.plugins ? svc.plugins->FindConsole(core.system) : std::nullopt) {
        for (const plugins::BiosSet& set : console->bios) {
            if (!set.cores.empty() && std::find(set.cores.begin(), set.cores.end(), core.id) == set.cores.end()) {
                continue;
            }
            for (const plugins::BiosFile& file : set.files) {
                if (file.path.empty() || file.path.starts_with("@")) {
                    continue;
                }
                const fs::path path = core.system_dir / PathFromUtf8(file.path);
                if (fs::exists(path, ec)) {
                    setup.bios.push_back(path.has_filename() ? path : path.parent_path());
                }
            }
        }
    }
    if (svc.library) {
        std::set<fs::path> folders;
        for (const library::Entry& entry : svc.library->Entries()) {
            if (entry.system == core.system && !entry.path.empty()) {
                folders.insert(PathFromUtf8(entry.path).parent_path());
            }
        }
        // A folder inside another is covered by it.
        for (const fs::path& folder : folders) {
            if (std::none_of(setup.game_folders.begin(), setup.game_folders.end(),
                             [&](const fs::path& kept) { return Within(folder, kept); })) {
                setup.game_folders.push_back(folder);
            }
        }
    }
    setup.settings = core.option_hints;
    if (svc.settings) {
        for (const std::string& item : svc.settings->GetList("engine_options." + core.id)) {
            const std::size_t eq = item.find('=');
            if (eq == std::string::npos || eq == 0) {
                continue;
            }
            const std::string key = item.substr(0, eq);
            auto it = std::find_if(setup.settings.begin(), setup.settings.end(),
                                   [&](const auto& value) { return value.first == key; });
            if (it != setup.settings.end()) {
                it->second = item.substr(eq + 1);
            } else {
                setup.settings.emplace_back(key, item.substr(eq + 1));
            }
        }
    }
    return setup;
}

engine::Install EngineInstallFor(const plugins::CoreInfo& core) {
    engine::Install install;
    install.executable = core.engine.executable;
    install.source = "manifest";
    install.level = core.engine.level;
    install.core_id = core.id;
    install.package = core.engine.package;
    install.cert_sha256 = core.engine.cert_sha256;
    return install;
}

std::optional<plugins::CoreInfo> FoundEngineCore(const std::string& system) {
    const Services& svc = Svc();
    const auto install = svc.engine ? svc.engine->Find(system) : std::nullopt;
    if (!install || install->level < 1 || !install->package.empty()) {
        return std::nullopt;
    }
    plugins::CoreInfo core;
    core.id = PathToUtf8(install->executable.stem());
    core.name = core.id;
    core.system = system;
    core.runner = plugins::Runner::Engine;
    core.engine.level = install->level;
    core.engine.executable = install->executable;
    core.library = install->executable;
    core.installed = true;
    return core;
}

DEN_REGISTER_FRAME(Frame);

} // namespace den::app
