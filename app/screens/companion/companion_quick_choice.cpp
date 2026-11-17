// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The quick settings layout the user chose ("ui.companion.quick": "" for EmuDEN's standard
// one, "<plugin>/<choice>" for a plugin's, or "<plugin>" for its first while it downloads),
// cached until the setting or the plugins change, and the Settings row that picks it.

#include <algorithm>

#include "companion_play.h"
#include "den/app/app.h"
#include "den/app/feature_api.h"
#include "den/app/pages.h"
#include "den/app/quick_layouts.h"
#include "den/base/files.h"
#include "den/base/log.h"

namespace den::app {

namespace {

constexpr const char* CHOSEN_KEY = "ui.companion.quick";

struct Cache {
    std::string key;
    std::uint64_t revision = ~0ull;
    std::shared_ptr<const QuickSpec> spec;
};

Cache& GetCache() {
    static Cache cache;
    return cache;
}

std::shared_ptr<const QuickSpec> Standard() {
    static const auto standard = [] {
        auto spec = std::make_shared<QuickSpec>();
        spec->name = "EmuDEN (standard)";
        return spec;
    }();
    return standard;
}

// The choices of an installed plugin; empty when it has none this app can draw.
std::vector<QuickSpec> ChoicesOf(const plugins::FeaturePlugin& plugin) {
    if (!plugin.installed || !plugins::OffersQuickSettings(plugin)) {
        return {};
    }
    const auto text = ReadTextFile(plugin.dir / "plugin.json");
    Result<std::vector<QuickSpec>> choices = text ? ParseQuickSettings(*text) : Status::Fail("no manifest");
    if (!choices) {
        log::Warn("quick", "{}: {}", plugin.id, choices.error());
        return {};
    }
    return choices.take();
}

std::shared_ptr<const QuickSpec> Load(const std::string& key) {
    const std::size_t slash = key.find('/');
    const std::string id = key.substr(0, slash);
    const std::string choice = slash == std::string::npos ? std::string() : key.substr(slash + 1);
    plugins::FeaturePlugin plugin;
    if (!FindFeaturePlugin(id, plugin)) {
        return nullptr;
    }
    std::vector<QuickSpec> choices = ChoicesOf(plugin);
    const auto it = std::find_if(choices.begin(), choices.end(),
                                 [&](const QuickSpec& spec) { return choice.empty() || spec.choice_id == choice; });
    return it == choices.end() ? nullptr : std::make_shared<const QuickSpec>(std::move(*it));
}

std::vector<plugins::FeaturePlugin> QuickPlugins() {
    std::vector<plugins::FeaturePlugin> out;
    if (plugins::FeatureHost* host = FeatureHostSvc()) {
        for (plugins::FeaturePlugin& plugin : host->Plugins()) {
            if (plugins::OffersQuickSettings(plugin)) {
                out.push_back(std::move(plugin));
            }
        }
    }
    return out;
}

} // namespace

std::shared_ptr<const QuickSpec> ChosenQuickLayout() {
    Cache& cache = GetCache();
    plugins::FeatureHost* host = FeatureHostSvc();
    const std::string key = Svc().settings ? Svc().settings->GetString(CHOSEN_KEY) : std::string();
    const std::uint64_t revision = host ? host->Revision() : 0;
    if (key != cache.key || revision != cache.revision || !cache.spec) {
        cache.key = key;
        cache.revision = revision;
        std::shared_ptr<const QuickSpec> spec = key.empty() || !host ? nullptr : Load(key);
        cache.spec = spec ? spec : Standard();
    }
    return cache.spec;
}

ui::RowSpec QuickLayoutRow() {
    struct Entry {
        std::string key, plugin;
        bool install = false;
    };
    std::vector<Entry> entries = {{"", "", false}};
    std::vector<std::string> names = {Standard()->name};
    for (const plugins::FeaturePlugin& plugin : QuickPlugins()) {
        const bool install = !plugin.installed || plugin.update_available;
        const std::vector<QuickSpec> choices = ChoicesOf(plugin);
        if (choices.empty()) {
            // Not downloaded yet: its first layout once it is.
            entries.push_back({plugin.id, plugin.id, true});
            names.push_back(plugin.name + (plugin.installed ? " (update)" : " (download)"));
            continue;
        }
        for (const QuickSpec& choice : choices) {
            entries.push_back({choice.Key(), plugin.id, install});
            names.push_back(choices.size() > 1 ? plugin.name + ": " + choice.name : plugin.name);
        }
    }
    ui::RowSpec row = ChoiceRow(
        CHOSEN_KEY, "Quick settings layout", names,
        [entries] {
            const std::string chosen = Svc().settings->GetString(CHOSEN_KEY);
            auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.key == chosen; });
            if (it == entries.end()) {
                // A plugin's id alone: its first layout.
                const std::string plugin = chosen.substr(0, chosen.find('/'));
                it = std::find_if(entries.begin() + 1, entries.end(), [&](const Entry& e) { return e.plugin == plugin; });
            }
            return it == entries.end() ? 0 : static_cast<int>(it - entries.begin());
        },
        [entries](int index) {
            const Entry& entry = entries[static_cast<std::size_t>(index)];
            Svc().settings->SetString(CHOSEN_KEY, entry.key);
            // Data only, like a Home interface: there is nothing to accept.
            if (entry.install) {
                InstallFeaturePlugin(entry.plugin, true);
            }
        });
    row.description = "How the second screen lays out its quick settings while a game plays";
    return row;
}

} // namespace den::app
