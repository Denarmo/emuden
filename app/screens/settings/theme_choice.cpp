// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The theme in use ("ui.theme"): one of the app's, by name, or one of an interface plugin's
// "themes", by "<plugin>/<theme>". Plugin themes are kept for the app's lifetime, so the
// pointer in Svc().theme stays valid when a plugin goes; the app then falls back to Light.

#include <algorithm>
#include <map>
#include <memory>

#include "den/app/app.h"
#include "den/app/feature_api.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "theme_choice.h"
#include "theme_spec.h"

namespace den::app {

namespace {

constexpr const char* THEME_KEY = "ui.theme";

struct State {
    std::map<std::string, std::unique_ptr<ui::Theme>> kept; // by key, never freed
    std::string key;                                         // the setting last applied
    std::uint64_t revision = ~0ull;
    const ui::Theme* applied = nullptr;
};

State& GetState() {
    static State state;
    return state;
}

bool PluginKey(const std::string& key) {
    return key.find('.') != std::string::npos; // plugin ids are dotted, the app's names are not
}

std::vector<plugins::FeaturePlugin> ThemePlugins() {
    std::vector<plugins::FeaturePlugin> out;
    if (plugins::FeatureHost* host = FeatureHostSvc()) {
        for (plugins::FeaturePlugin& plugin : host->Plugins()) {
            if (plugins::OffersThemes(plugin)) {
                out.push_back(std::move(plugin));
            }
        }
    }
    return out;
}

std::vector<ui::Theme> ThemesOf(const plugins::FeaturePlugin& plugin) {
    if (!plugin.installed || !plugins::OffersThemes(plugin)) {
        return {};
    }
    const auto text = ReadTextFile(plugin.dir / "plugin.json");
    Result<std::vector<ui::Theme>> themes = text ? ParseThemes(*text) : Status::Fail("no manifest");
    if (!themes) {
        log::Warn("theme", "{}: {}", plugin.id, themes.error());
        return {};
    }
    return themes.take();
}

// A kept copy of the plugin theme `key` names, updated in place; null when there is none.
const ui::Theme* FindPluginTheme(const std::string& key) {
    const std::size_t slash = key.find('/');
    plugins::FeaturePlugin plugin;
    if (!FindFeaturePlugin(key.substr(0, slash), plugin)) {
        return nullptr;
    }
    std::vector<ui::Theme> themes = ThemesOf(plugin);
    const auto it = std::find_if(themes.begin(), themes.end(),
                                 [&](const ui::Theme& t) { return slash == std::string::npos || t.key == key; });
    if (it == themes.end()) {
        return nullptr;
    }
    std::unique_ptr<ui::Theme>& kept = GetState().kept[it->key];
    if (!kept) {
        kept = std::make_unique<ui::Theme>();
    }
    *kept = std::move(*it);
    return kept.get();
}

void Apply(const ui::Theme& theme) {
    State& state = GetState();
    state.applied = &theme;
    Svc().theme = &theme; // the text size is applied over it (display_rows.cpp)
    if (Svc().stage) {
        RebuildTop();
    }
}

// A plugin theme comes once the plugin host has listed it, and goes with its plugin; a value
// synced from another device applies too.
void FollowTheme() {
    Services& svc = Svc();
    plugins::FeatureHost* host = FeatureHostSvc();
    if (!svc.settings || !svc.theme) {
        return;
    }
    State& state = GetState();
    const std::string key = svc.settings->GetString(THEME_KEY, "Light");
    const std::uint64_t revision = host ? host->Revision() : 0;
    if (key == state.key && revision == state.revision) {
        return;
    }
    state.key = key;
    state.revision = revision;
    const ui::Theme& theme = ThemeForKey(key);
    const ui::Theme* current = state.applied ? state.applied : svc.theme;
    if (&theme != current || PluginKey(key)) { // a plugin's may have changed in place
        Apply(theme);
    }
}

} // namespace

const ui::Theme& ThemeForKey(const std::string& key) {
    if (PluginKey(key)) {
        const ui::Theme* theme = FeatureHostSvc() ? FindPluginTheme(key) : nullptr;
        return theme ? *theme : ui::LightTheme();
    }
    return ui::ThemeByName(key);
}

void SetTheme(const std::string& key) {
    const ui::Theme& theme = ThemeForKey(key);
    // A plugin's that is still downloading is kept: it applies once installed.
    const std::string stored = PluginKey(key) ? key : theme.key;
    Svc().settings->SetString(THEME_KEY, stored);
    GetState().key = stored;
    Apply(theme);
}

ui::RowSpec ThemeRow() {
    struct Entry {
        std::string key, plugin;
        bool install = false;
    };
    std::vector<Entry> entries;
    std::vector<std::string> names;
    for (const std::string& name : ui::ThemeNames()) {
        entries.push_back({name, "", false});
        names.push_back(name);
    }
    for (const plugins::FeaturePlugin& plugin : ThemePlugins()) {
        const bool install = !plugin.installed || plugin.update_available;
        const std::vector<ui::Theme> themes = ThemesOf(plugin);
        if (themes.empty()) {
            entries.push_back({plugin.id, plugin.id, true});
            names.push_back(plugin.name + (plugin.installed ? " (update)" : " (download)"));
            continue;
        }
        for (const ui::Theme& theme : themes) {
            entries.push_back({theme.key, plugin.id, install});
            names.push_back(theme.name);
        }
    }
    const auto plugins_from = static_cast<std::ptrdiff_t>(ui::ThemeNames().size());
    ui::RowSpec row = ChoiceRow(
        THEME_KEY, "Theme", names,
        [entries, plugins_from] {
            const std::string chosen = Svc().settings->GetString(THEME_KEY, "Light");
            auto it = std::find_if(entries.begin(), entries.end(),
                                   [&](const Entry& e) { return EqualsNoCase(e.key, chosen); });
            if (it == entries.end() && PluginKey(chosen)) {
                // A plugin's id alone: its first theme.
                const std::string plugin = chosen.substr(0, chosen.find('/'));
                it = std::find_if(entries.begin() + plugins_from, entries.end(),
                                  [&](const Entry& e) { return e.plugin == plugin; });
            }
            return it == entries.end() ? 0 : static_cast<int>(it - entries.begin());
        },
        [entries](int index) {
            const Entry& entry = entries[static_cast<std::size_t>(index)];
            SetTheme(entry.key);
            // Colours only, like a Home interface: there is nothing to accept.
            if (entry.install) {
                InstallFeaturePlugin(entry.plugin, true);
            }
        });
    row.description = "More themes come from plugin sources, like Home interfaces";
    return row;
}

DEN_REGISTER_FRAME(FollowTheme);

} // namespace den::app
