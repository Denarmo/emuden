// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugins: the installed feature plugins as tiles, what the sources offer, and a row per
// installed plugin to manage it. Route "plugins".

#include <format>
#include <functional>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/app/interfaces.h"
#include "den/app/feature_api.h"
#include "den/app/model_widgets.h"
#include "page_widths.h"

namespace den::app {

namespace {

using plugins::FeaturePlugin;

constexpr float ICON = 144;

std::string Badge(const FeaturePlugin& plugin) {
    if (!plugin.error.empty()) {
        return "Stopped";
    }
    if (plugin.update_available) {
        return "Update";
    }
    return plugin.trust == FeaturePlugin::Trust::Developer ? "Developer" : "";
}

// The update of an installed plugin: its consent page when it asks for more, else straight away.
void StartUpdate(const FeaturePlugin& plugin) {
    if (FeaturePluginJob(plugin.id)) {
        return;
    }
    if (plugin.needs_consent) {
        Svc().stage->Push("plugin.consent", {{"id", plugin.id}});
    } else {
        InstallFeaturePlugin(plugin.id, false);
    }
}

// Opening a plugin with an update waiting asks to update it first; "Not now" opens it as it is.
void OpenOrUpdate(const FeaturePlugin& plugin, std::function<void()> open) {
    if (!plugin.update_available || FeaturePluginJob(plugin.id)) {
        open();
        return;
    }
    Svc().stage->Confirm(TrF("Update {}?", plugin.name),
                         TrF("A newer version of {} is available. Update it before opening it?", plugin.name),
                         "Update", [plugin] { StartUpdate(plugin); }, "Not now", true, std::move(open));
}

void StartInstall(const FeaturePlugin& plugin) {
    if (FeaturePluginJob(plugin.id)) {
        return;
    }
    if (plugin.needs_consent || plugin.trust != FeaturePlugin::Trust::Official) {
        Svc().stage->Push("plugin.consent", {{"id", plugin.id}});
    } else {
        InstallFeaturePlugin(plugin.id, false);
    }
}

class PluginsScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        plugins::FeatureHost* host = FeatureHostSvc();
        width_ = svc.canvas->Width();
        revision_ = host ? host->Revision() : 0;
        selected_.clear();
        ui::Node* content = BuildRailFrame(*this, "Plugins");
        content->pad_right = svc.theme->margin;
        const float room = RailContentWidth(width_ > 0 ? width_ : 1280.0f, svc.theme->margin);
        if (!host) {
            auto* text = content->Emplace<Paragraph>(
                "Plugins add features to EmuDEN, such as installing ROM hacks on your own games. "
                "This version cannot run them yet.",
                std::min(room, 720.0f));
            text->tone = ui::Tone::Dim;
            return;
        }
        ui::Node* list = content->Add(ui::MakeList(10));
        list->id = "plugins.list";
        list->pad_bottom = 28;

        std::vector<FeaturePlugin> installed, available;
        for (FeaturePlugin& plugin : host->Plugins()) {
            (plugin.installed ? installed : available).push_back(std::move(plugin));
        }

        AddHeading(list, "Installed");
        if (installed.empty()) {
            list->Emplace<ui::Label>("Nothing is installed yet.")->tone = ui::Tone::Dim;
        } else {
            ui::Node* shelf = list->Add(ui::MakeShelf(18));
            shelf->id = "plugins.installed";
            shelf->SetPadding(10, 10);
            for (const FeaturePlugin& plugin : installed) {
                auto* tile = shelf->Emplace<ui::Tile>(plugin.name, PathToUtf8(plugin.icon), ICON, ICON);
                tile->id = "plugins.tile." + plugin.id;
                tile->badge = Badge(plugin);
                // A plugin whose files are there but that was never accepted (a developer
                // folder) is accepted first; an update waiting for consent still opens.
                const bool accept = plugin.needs_consent && !plugin.update_available;
                const bool one_look = plugins::OffersHomeInterface(plugin) && !plugins::OffersHomeInterfaces(plugin);
                if (one_look) {
                    tile->on_activate = [plugin] { OpenOrUpdate(plugin, [id = plugin.id] { UseInterface(id); }); };
                } else if (plugin.kind == plugins::KIND_INTERFACE) {
                    // Several Home looks and quick settings layouts are picked under Settings,
                    // Interface; themes under Display.
                    const bool themes_only = plugins::OffersThemes(plugin) && !plugins::OffersHomeInterface(plugin) &&
                                             !plugins::OffersQuickSettings(plugin);
                    tile->on_activate = [plugin, section = themes_only ? "Display" : "Interface"] {
                        OpenOrUpdate(plugin, [section] { Svc().stage->Push("settings", {{"section", section}}); });
                    };
                } else {
                    tile->on_activate = [plugin, accept] {
                        OpenOrUpdate(plugin, [id = plugin.id, accept] {
                            Svc().stage->Push(accept ? "plugin.consent" : "plugin.view", {{"id", id}});
                        });
                    };
                }
                tile->on_focus = [this, id = plugin.id, look = one_look] {
                    selected_ = id;
                    selected_look_ = look;
                };
            }
        }

        AddHeading(list, "Available");
        std::vector<ui::RowSpec> rows;
        for (const FeaturePlugin& plugin : available) {
            ui::RowSpec row = ValueRow(
                "plugins.get." + plugin.id, plugin.name,
                [id = plugin.id] { return JobText(FeaturePluginJob(id), "Install"); },
                [plugin] { StartInstall(plugin); });
            row.description = std::format("{}  ·  {}  ·  {}", plugin.publisher, plugin.summary, plugin.source_name);
            rows.push_back(std::move(row));
        }
        if (rows.empty()) {
            list->Emplace<ui::Label>(installed.empty() ? "Your plugin sources offer nothing yet."
                                                       : "Everything your sources offer is installed.")
                ->tone = ui::Tone::Dim;
        }
        AddRows(list, std::move(rows));

        if (!installed.empty()) {
            AddHeading(list, "Manage");
            rows.clear();
            for (const FeaturePlugin& plugin : installed) {
                ui::RowSpec row = ValueRow(
                    "plugins.manage." + plugin.id, plugin.name,
                    [id = plugin.id, update = plugin.update_available, version = plugin.version] {
                        return JobText(FeaturePluginJob(id), update ? Tr("Update available") : TrF("Version {}", version));
                    },
                    [id = plugin.id] { Svc().stage->Push("plugin.manage", {{"id", id}}); });
                row.description = std::format("{}  ·  {}", plugin.publisher, plugin.source_name);
                rows.push_back(std::move(row));
            }
            AddRows(list, std::move(rows));
        }

        AddHeading(list, "Sources");
        rows.clear();
        ui::RowSpec sources = ValueRow(
            "plugins.sources", "Plugin sources", [] { return std::format("{}", Svc().plugins->Sources().size()); },
            [] { Svc().stage->Push("settings", {{"section", "Plugins"}}); });
        sources.description = "Where plugins come from: EmuDEN's own source and the ones you added";
        rows.push_back(std::move(sources));
        AddRows(list, std::move(rows));
    }

    void Update(ui::Context&) override {
        plugins::FeatureHost* host = FeatureHostSvc();
        if ((host && host->Revision() != revision_) || Svc().canvas->Width() != width_) {
            Rebuild();
        }
    }

    std::vector<ui::Hint> Hints() const override {
        if (OnTile()) {
            return {{input::Nav::Confirm, selected_look_ ? "Use as Home" : "Open"}, {input::Nav::Option, "Manage"}, {input::Nav::Back, "Back"}};
        }
        return {{input::Nav::Confirm, "Select"}, {input::Nav::Back, "Back"}};
    }

    bool OnNav(input::Nav nav) override {
        if (nav == input::Nav::Option && OnTile()) {
            GetStage().Push("plugin.manage", {{"id", selected_}});
            return true;
        }
        if (nav != input::Nav::Back) {
            return false;
        }
        if (GetStage().Depth() > 1) {
            GetStage().Pop();
        } else {
            GetStage().Replace("home");
        }
        return true;
    }

private:
    static void AddHeading(ui::Node* list, const std::string& text) {
        auto heading = ui::MakeHeading(text);
        heading->size = 20;
        heading->tone = ui::Tone::Dim;
        heading->pad_top = 8;
        list->Add(std::move(heading));
    }

    bool OnTile() const {
        return !selected_.empty() && Focus() && Focus()->id == "plugins.tile." + selected_;
    }

    std::string selected_;
    bool selected_look_ = false; // the selected tile is an interface: Confirm uses it
    std::uint64_t revision_ = 0;
    float width_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("plugins", PluginsScreen);

} // namespace den::app
