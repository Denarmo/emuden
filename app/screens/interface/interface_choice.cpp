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
#include "den/app/quick_layouts.h"
#include "object_build.h"

namespace den::app {

namespace {

constexpr const char* CHOSEN_KEY = "ui.home.interface";

struct Cache {
    std::string key;
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
            if (plugins::OffersHomeInterface(plugin)) {
                out.push_back(std::move(plugin));
            }
        }
    }
    return out;
}

// The looks of an installed plugin; empty when it has none this app can draw.
std::vector<InterfaceSpec> LooksOf(const plugins::FeaturePlugin& plugin) {
    if (!plugin.installed || !plugins::OffersHomeInterface(plugin)) {
        return {};
    }
    const auto text = ReadTextFile(plugin.dir / "plugin.json");
    Result<std::vector<InterfaceSpec>> looks =
        text ? ParseInterfaces(*text, plugin.dir, MouldNames()) : Status::Fail("no manifest");
    if (!looks) {
        log::Warn("interface", "{}: {}", plugin.id, looks.error());
        return {};
    }
    return looks.take();
}

// "<plugin>/<look>", or a plugin's id alone for its first look.
std::shared_ptr<const InterfaceSpec> Load(const std::string& key) {
    const std::size_t slash = key.find('/');
    const std::string id = key.substr(0, slash);
    const std::string look = slash == std::string::npos ? std::string() : key.substr(slash + 1);
    plugins::FeaturePlugin plugin;
    if (!FindFeaturePlugin(id, plugin)) {
        return nullptr;
    }
    std::vector<InterfaceSpec> looks = LooksOf(plugin);
    const auto it = std::find_if(looks.begin(), looks.end(),
                                 [&](const InterfaceSpec& spec) { return look.empty() || spec.look_id == look; });
    return it == looks.end() ? nullptr : std::make_shared<const InterfaceSpec>(std::move(*it));
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
    struct Entry {
        std::string key, plugin;
        bool install = false;
    };
    std::vector<Entry> entries = {{"", "", false}};
    std::vector<std::string> names = {"EmuDEN (standard)"};
    for (const plugins::FeaturePlugin& plugin : found) {
        const bool install = !plugin.installed || plugin.update_available;
        const std::vector<InterfaceSpec> looks = LooksOf(plugin);
        if (looks.empty()) {
            // Not downloaded yet: its first look once it is.
            entries.push_back({plugin.id, plugin.id, true});
            names.push_back(plugin.name + (plugin.installed ? " (update)" : " (download)"));
            continue;
        }
        for (const InterfaceSpec& look : looks) {
            entries.push_back({look.Key(), plugin.id, install});
            names.push_back(looks.size() > 1 ? plugin.name + ": " + look.name : plugin.name);
        }
    }
    ui::RowSpec choice = ChoiceRow(
        CHOSEN_KEY, "Home interface", names,
        [entries] {
            const std::string chosen = Svc().settings->GetString(CHOSEN_KEY);
            auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.key == chosen; });
            if (it == entries.end()) {
                // A plugin's id alone: its first look.
                const std::string plugin = chosen.substr(0, chosen.find('/'));
                it = std::find_if(entries.begin() + 1, entries.end(), [&](const Entry& e) { return e.plugin == plugin; });
            }
            return it == entries.end() ? 0 : static_cast<int>(it - entries.begin());
        },
        [entries](int index) {
            const Entry& entry = entries[static_cast<std::size_t>(index)];
            Svc().settings->SetString(CHOSEN_KEY, entry.key);
            // An interface asks for no permission: there is nothing to accept.
            if (entry.install) {
                InstallFeaturePlugin(entry.plugin, true);
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
    std::vector<ui::RowSpec> rows = {std::move(choice), QuickLayoutRow(), std::move(animation), std::move(sound)};
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
    const std::string key = Svc().settings ? Svc().settings->GetString(CHOSEN_KEY) : std::string();
    const std::uint64_t revision = host ? host->Revision() : 0;
    if (key != cache.key || revision != cache.revision) {
        cache.key = key;
        cache.revision = revision;
        cache.spec = key.empty() || !host ? nullptr : Load(key);
    }
    return cache.spec;
}

void UseInterface(const std::string& key) {
    Svc().settings->SetString(CHOSEN_KEY, key);
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
