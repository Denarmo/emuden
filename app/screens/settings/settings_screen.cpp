// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The settings page: every section a feature registered with DEN_REGISTER_SETTINGS, as rows.

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

class SettingsScreen final : public ui::Page {
public:
    void Build() override {
        ui::Node* content = BuildRailFrame(*this, "Settings");
        content->pad_right = Svc().theme->margin;
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "settings.list";
        list->pad_bottom = 24;
        for (const SettingsSection& section : Registry::Get().Settings()) {
            auto heading = ui::MakeHeading(section.title);
            heading->pad_top = 14;
            heading->pad_bottom = 4;
            list->Add(std::move(heading));
            for (ui::RowSpec& spec : section.rows()) {
                list->Emplace<ui::Row>(std::move(spec));
            }
        }
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Change"}, {input::Nav::Back, "Back"}};
    }

    bool OnNav(input::Nav nav) override {
        if (nav == input::Nav::Back) {
            Svc().stage->Replace("home");
            return true;
        }
        return false;
    }
};

} // namespace

DEN_REGISTER_SCREEN("settings", SettingsScreen);
DEN_REGISTER_RAIL(90, "Settings", "settings");

} // namespace den::app
