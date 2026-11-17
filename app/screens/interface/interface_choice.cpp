// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Choosing the Home interface: the Settings section, the cached look of the chosen plugin,
// and the "home" route, which builds the chosen interface's page or the standard Home.

#include <algorithm>
#include <format>

#include "den/app/feature_api.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/app/boot.h"
#include "den/app/interfaces.h"
#include "object_build.h"

namespace den::app {

namespace {

constexpr const char* CHOSEN_KEY = "ui.home.interface";

struct Cache {
    std::string id;
    std::uint64_t revision = ~0ull;
    std::shared_ptr<const InterfaceSpec> spec;
    const InterfaceSpec* shown = nullptr; // what Home was last built with
};

Cache& GetCache() {
    static Cache cache;
    return cache;
}

std::vector<plugins::FeaturePlugin> InterfacePlugins() {
    std::vector<plugins::FeaturePlugin> out;
    if (plugins::FeatureHost* host = FeatureHostSvc()) {
        for (plugins::FeaturePlugin& plugin : host->Plugins()) {
            if (plugin.kind == plugins::KIND_INTERFACE) {
                out.push_back(std::move(plugin));
            }
        }
    }
    return out;
}

std::shared_ptr<const InterfaceSpec> Load(const std::string& id) {
    plugins::FeaturePlugin plugin;
    if (!FindFeaturePlugin(id, plugin) || !plugin.installed || plugin.kind != plugins::KIND_INTERFACE) {
        return nullptr;
    }
    const auto text = ReadTextFile(plugin.dir / "plugin.json");
    Result<InterfaceSpec> spec = text ? ParseInterface(*text, plugin.dir, MouldNames()) : Status::Fail("no manifest");
    if (!spec) {
        log::Warn("interface", "{}: {}", id, spec.error());
        return nullptr;
    }
    return std::make_shared<const InterfaceSpec>(spec.take());
}

// Home follows the choice: rebuilt when the chosen plugin arrives, changes or goes.
void FollowChoice() {
    ui::Page* top = Svc().stage->Top();
    const std::shared_ptr<const InterfaceSpec> spec = ChosenInterface();
    Cache& cache = GetCache();
    if (top != nullptr && top->Route() == "home" && cache.shown != spec.get()) {
        Svc().stage->Replace("home");
    }
}

std::vector<ui::RowSpec> InterfaceRows() {
    const std::vector<plugins::FeaturePlugin> found = InterfacePlugins();
    std::vector<std::string> ids = {""};
    std::vector<std::string> names = {"EmuDEN (standard)"};
    for (const plugins::FeaturePlugin& plugin : found) {
        ids.push_back(plugin.id);
        names.push_back(plugin.installed ? plugin.name : plugin.name + " (download)");
    }
    ui::RowSpec choice = ChoiceRow(
        CHOSEN_KEY, "Home interface", names,
        [ids] {
            const std::string chosen = Svc().settings->GetString(CHOSEN_KEY);
            const auto it = std::find(ids.begin(), ids.end(), chosen);
            return it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
        },
        [ids, found](int index) {
            const std::string& id = ids[static_cast<std::size_t>(index)];
            Svc().settings->SetString(CHOSEN_KEY, id);
            const auto plugin = std::find_if(found.begin(), found.end(),
                                             [&](const plugins::FeaturePlugin& p) { return p.id == id; });
            // An interface asks for no permission: there is nothing to accept.
            if (plugin != found.end() && (!plugin->installed || plugin->update_available)) {
                InstallFeaturePlugin(id, true);
            }
        });
    choice.description = "How the first screen shows your games. Interfaces come from plugin sources, like consoles";
    ui::RowSpec animation = ToggleRow(
        std::string(LAUNCH_ANIMATION_KEY), "Launch animation",
        [] { return Svc().settings->GetBool(LAUNCH_ANIMATION_KEY, true); },
        [](bool on) { Svc().settings->SetBool(LAUNCH_ANIMATION_KEY, on); });
    animation.description = "The game's cartridge or disc drops into its slot, then the EmuDEN logo shows while "
                            "the game starts";
    ui::RowSpec sound = ToggleRow(
        std::string(LAUNCH_SOUND_KEY), "Launch sound",
        [] { return Svc().settings->GetBool(LAUNCH_SOUND_KEY, true); },
        [](bool on) { Svc().settings->SetBool(LAUNCH_SOUND_KEY, on); });
    sound.description = "A short chime with the EmuDEN logo";
    std::vector<ui::RowSpec> rows = {std::move(choice), std::move(animation), std::move(sound)};
    for (const plugins::FeaturePlugin& plugin : found) {
        ui::RowSpec row = ValueRow(
            "interface.about." + plugin.id, plugin.name,
            [id = plugin.id, installed = plugin.installed, version = plugin.version] {
                return JobText(FeaturePluginJob(id), installed ? "Version " + version : "Not installed");
            });
        row.description = std::format("{}  ·  {}  ·  {}", plugin.publisher, plugin.summary, plugin.source_name);
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace

std::shared_ptr<const InterfaceSpec> ChosenInterface() {
    Cache& cache = GetCache();
    plugins::FeatureHost* host = FeatureHostSvc();
    const std::string id = Svc().settings ? Svc().settings->GetString(CHOSEN_KEY) : std::string();
    const std::uint64_t revision = host ? host->Revision() : 0;
    if (id != cache.id || revision != cache.revision) {
        cache.id = id;
        cache.revision = revision;
        cache.spec = id.empty() || !host ? nullptr : Load(id);
    }
    return cache.spec;
}

void UseInterface(const std::string& id) {
    Svc().settings->SetString(CHOSEN_KEY, id);
    ui::Stage& stage = *Svc().stage;
    stage.PopToRoot();
    stage.Replace("home");
}

std::unique_ptr<ui::Page> MakeChosenHome() {
    std::shared_ptr<const InterfaceSpec> spec = ChosenInterface();
    GetCache().shown = spec.get();
    if (!spec) {
        return nullptr;
    }
    return MakeCarouselHome(std::move(spec)); // every layout (KnownLayout) shares the page
}

DEN_REGISTER_FRAME(FollowChoice);
DEN_REGISTER_SETTINGS(11, "Interface", InterfaceRows);

} // namespace den::app
