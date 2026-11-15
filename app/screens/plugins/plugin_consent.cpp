// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What a plugin asks for, before it is installed or updated (route "plugin.consent", param
// id), and the page that manages an installed one (route "plugin.manage", param id).

#include <format>

#include "../home/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "feature_api.h"
#include "model_widgets.h"

namespace den::app {

namespace {

using plugins::FeaturePlugin;

float TextWidth() {
    const float width = Svc().canvas->Width();
    return std::min((width > 0 ? width : 1280.0f) - Svc().theme->margin * 2 - 12, 860.0f);
}

std::string TrustText(const FeaturePlugin& plugin) {
    switch (plugin.trust) {
    case FeaturePlugin::Trust::Official:
        return "From EmuDEN's own source";
    case FeaturePlugin::Trust::ThirdParty:
        return TrF("source.from_added", plugin.source_name);
    case FeaturePlugin::Trust::Developer:
        return "A developer folder on this device, not signed";
    }
    return {};
}

std::string SizeOf(std::uint64_t bytes) {
    return bytes >= (1u << 20) ? std::format("{:.1f} MB", static_cast<double>(bytes) / (1u << 20))
                               : std::format("{} KB", (bytes + 1023) / 1024);
}

void AddFact(ui::Node* list, const std::string& name, const std::string& value) {
    ui::Node* line = list->Add(ui::MakeBox(ui::Axis::Row, 12));
    auto* label = line->Emplace<ui::Label>(name);
    label->tone = ui::Tone::Dim;
    label->width = 130;
    line->Emplace<ui::Label>(value)->grow = 1;
}

void AddHeading(ui::Node* list, const std::string& text) {
    auto heading = ui::MakeHeading(text);
    heading->size = 20;
    heading->pad_top = 10;
    list->Add(std::move(heading));
}

// Each line can be selected, so a pad reaches the ones a small screen leaves below the fold.
void AddPermissions(ui::Node* list, const std::vector<plugins::PermissionLine>& lines, const std::string& prefix) {
    int index = 0;
    for (const plugins::PermissionLine& line : lines) {
        auto* text = list->Emplace<Paragraph>("\xE2\x80\xA2  " + line.text, TextWidth());
        text->id = std::format("{}.{}", prefix, index++);
        text->label = line.permission;
        text->focusable = true;
    }
}

bool Leave(ui::Page& page, input::Nav nav) {
    if (nav != input::Nav::Back) {
        return false;
    }
    page.GetStage().Pop();
    return true;
}

class ConsentScreen final : public ui::Page {
public:
    void Build() override {
        const std::string id = Param("id");
        FeaturePlugin plugin;
        if (!FindFeaturePlugin(id, plugin)) {
            BuildPlainFrame(*this, "Plugin")->Emplace<ui::Label>("This plugin is no longer offered.");
            return;
        }
        const bool update = plugin.installed && plugin.update_available;
        // On the device already, never agreed to: a plugin of the developer folder.
        const bool allow = plugin.installed && !update;
        const char* const verb = update ? "Update" : (allow ? "Allow" : "Install");
        const bool careful = plugin.trust != FeaturePlugin::Trust::Official;
        ui::Node* content = BuildPlainFrame(*this, std::format("{} {}?", verb, plugin.name));
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "consent.list";
        AddFact(list, "Publisher", plugin.publisher);
        AddFact(list, "Source", plugin.source_name);
        AddFact(list, "Trust", TrustText(plugin));
        AddFact(list, "Version", plugin.version);
        if (careful) {
            auto* warning = list->Emplace<Paragraph>(
                std::format("EmuDEN did not check this plugin. {} it only if you trust {}.", allow ? "Run" : "Install",
                            plugin.publisher.empty() ? std::string("where it comes from") : plugin.publisher),
                TextWidth());
            warning->boxed = true;
            warning->font = gfx::FONT_BOLD;
        }
        if (update && !plugin.new_permissions.empty()) {
            AddHeading(list, "New with this update, it will be able to");
            AddPermissions(list, plugin.new_permissions, "consent.new");
            AddHeading(list, "It already can");
        } else {
            AddHeading(list, "It will be able to");
        }
        AddPermissions(list, plugin.permissions, "consent.has");
        if (plugin.permissions.empty() && plugin.new_permissions.empty()) {
            list->Emplace<ui::Label>("Nothing beyond showing its own pages.")->tone = ui::Tone::Dim;
        }
        auto* never = list->Emplace<Paragraph>(
            "No plugin can read your saves or game files, reach other sites than the ones named here, "
            "or run programs.",
            TextWidth());
        never->tone = ui::Tone::Dim;
        never->size = Svc().theme->text_small;

        ui::Node* buttons = content->Add(ui::MakeBox(ui::Axis::Row, 12));
        buttons->pad_bottom = 56; // above the hint bar
        const auto cancel = [&] {
            auto* button = buttons->Emplace<ui::Button>("Cancel", [this] { GetStage().Pop(); });
            button->id = "consent.cancel";
            button->width = 220;
        };
        const auto accept = [&] {
            auto* button = buttons->Emplace<ui::Button>(verb, [this, id] {
                InstallFeaturePlugin(id, true);
                GetStage().Pop();
            });
            button->id = "consent.install";
            button->width = 220;
        };
        // The first button gets the focus: Cancel for anything EmuDEN did not publish.
        if (careful) {
            cancel();
            accept();
        } else {
            accept();
            cancel();
        }
        first_ = careful ? "consent.cancel" : "consent.install";
    }

    // The selection starts on a button, not on the first line of the list.
    void Update(ui::Context&) override {
        if (!first_.empty()) {
            if (ui::Node* node = Root().FindById(first_)) {
                SetFocus(node);
            }
            first_.clear();
        }
    }

    bool OnNav(input::Nav nav) override {
        return Leave(*this, nav);
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Select"}, {input::Nav::Back, "Cancel"}};
    }

private:
    std::string first_;
};

class ManageScreen final : public ui::Page {
public:
    void Build() override {
        const std::string id = Param("id");
        plugins::FeatureHost* host = FeatureHostSvc();
        revision_ = host ? host->Revision() : 0;
        FeaturePlugin plugin;
        if (!FindFeaturePlugin(id, plugin) || !plugin.installed) {
            BuildPlainFrame(*this, "Plugin")->Emplace<ui::Label>("This plugin is not installed.");
            return;
        }
        ui::Node* content = BuildPlainFrame(*this, plugin.name);
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "manage.list";
        list->pad_bottom = 64;
        if (!plugin.summary.empty()) {
            list->Emplace<Paragraph>(plugin.summary, TextWidth())->tone = ui::Tone::Dim;
        }
        AddFact(list, "Version", plugin.version);
        AddFact(list, "Publisher", plugin.publisher);
        AddFact(list, "Source", plugin.source_name);
        AddFact(list, "Trust", TrustText(plugin));
        AddFact(list, "Its data", SizeOf(plugin.data_bytes));
        if (!plugin.error.empty()) {
            auto* error = list->Emplace<Paragraph>("It stopped: " + plugin.error, TextWidth());
            error->boxed = true;
        }

        std::vector<ui::RowSpec> rows;
        rows.push_back(ActionRow("manage.open", "Open", [id] { Svc().stage->Push("plugin.view", {{"id", id}}); }));
        if (plugin.update_available) {
            ui::RowSpec update = ValueRow(
                "manage.update", "Update", [id] { return JobText(FeaturePluginJob(id), "A newer version is offered"); },
                [id, consent = plugin.needs_consent] {
                    if (consent) {
                        Svc().stage->Push("plugin.consent", {{"id", id}});
                    } else {
                        InstallFeaturePlugin(id, false);
                    }
                });
            if (plugin.needs_consent) {
                update.description = "It asks for more than it has now";
            }
            rows.push_back(std::move(update));
        }
        if (plugin.trust == FeaturePlugin::Trust::Developer) {
            rows.push_back(ActionRow("manage.reload", "Reload the script", [id] {
                FeatureHostSvc()->Reload(id);
                Svc().stage->Toast("Reloaded");
            }));
        }
        rows.push_back(ToggleRow("manage.delete_data", "When removing, also delete its data",
                                 [this] { return delete_data_; }, [this](bool on) { delete_data_ = on; }));
        rows.push_back(ToggleRow("manage.delete_hacks", "When removing, also remove the hacks it installed",
                                 [this] { return delete_hacks_; }, [this](bool on) { delete_hacks_ = on; }));
        rows.push_back(ValueRow(
            "manage.remove", "Remove", [id] { return JobText(FeaturePluginJob(id), ""); },
            [this, id, name = plugin.name] {
                if (FeaturePluginJob(id)) {
                    return;
                }
                std::string message = delete_data_ ? "The plugin and its data are deleted." : "Its data is kept.";
                message += delete_hacks_ ? " The hacks it installed are removed from the library."
                                         : " The hacks it installed stay in your library.";
                GetStage().Confirm(std::format("Remove {}?", name), message, "Remove", [this, id, name] {
                    TrackFeaturePluginJob(id, name, "was removed",
                                          FeatureHostSvc()->Remove(id, delete_data_, delete_hacks_));
                    GetStage().Pop();
                });
            }));
        AddRows(list, std::move(rows));

        AddHeading(list, "It can");
        AddPermissions(list, plugin.permissions, "manage.can");
        if (plugin.permissions.empty()) {
            list->Emplace<ui::Label>("Nothing beyond showing its own pages.")->tone = ui::Tone::Dim;
        }
    }

    void Update(ui::Context&) override {
        plugins::FeatureHost* host = FeatureHostSvc();
        if (host && host->Revision() != revision_) {
            Rebuild();
        }
    }

    bool OnNav(input::Nav nav) override {
        return Leave(*this, nav);
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Select"}, {input::Nav::Back, "Back"}};
    }

private:
    bool delete_data_ = false, delete_hacks_ = false;
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("plugin.consent", ConsentScreen);
DEN_REGISTER_SCREEN("plugin.manage", ManageScreen);

} // namespace den::app
