// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// System settings: the sections features registered with DEN_REGISTER_SETTINGS at the left,
// the rows of the chosen one at the right.

#include <algorithm>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"

namespace den::app {

namespace {

class SettingsScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        const std::vector<SettingsSection> sections = Registry::Get().Settings();
        if (!started_) {
            started_ = true;
            const std::string wanted = Param("section");
            for (std::size_t i = 0; i < sections.size(); ++i) {
                if (!wanted.empty() && EqualsNoCase(sections[i].title, wanted)) {
                    current_ = i;
                    focus_after_build_ = "section." + sections[i].title;
                }
            }
        }
        current_ = std::min(current_, sections.empty() ? 0 : sections.size() - 1);
        signature_ = Signature();

        ui::Node* content = BuildRailFrame(*this, "Settings");
        content->pad_right = svc.theme->margin;
        ui::Node* panes = content->Add(ui::MakeBox(ui::Axis::Row, 20));
        panes->grow = 1;

        ui::Node* list = panes->Add(ui::MakeList(6));
        list->id = "settings.sections";
        list->grow = 0;
        list->width = 220;
        list->pad_bottom = 24;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            auto* button = list->Emplace<ui::Button>(sections[i].title);
            button->id = "section." + sections[i].title;
            button->centered = false;
            button->selected = i == current_;
            button->on_focus = [this, i] {
                if (current_ != i && focus_after_build_.empty()) {
                    current_ = i;
                    Rebuild();
                }
            };
            button->on_activate = [this] { FocusFirstRow(); };
        }

        ui::Node* rows = panes->Add(ui::MakeList(8));
        rows->id = "settings.rows";
        rows->pad_bottom = 24;
        if (!sections.empty()) {
            AddRows(rows, sections[current_].rows());
        }
    }

    void Update(ui::Context&) override {
        // Opened at a section ("section" parameter): the focus goes to it, not to the first.
        if (!focus_after_build_.empty()) {
            ui::Node* button = Root().FindById(focus_after_build_);
            focus_after_build_.clear();
            if (button) {
                SetFocus(button, false);
            }
        }
        if (Signature() != signature_) {
            Rebuild();
        }
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Change"}, {input::Nav::Back, "Back"}};
    }

    bool OnNav(input::Nav nav) override {
        if (nav != input::Nav::Back) {
            return false;
        }
        // Back leaves the rows first, then the page.
        ui::Node* sections = Root().FindById("settings.sections");
        if (Focus() && sections && Focus()->Parent() != sections) {
            const std::vector<SettingsSection> all = Registry::Get().Settings();
            if (current_ < all.size()) {
                if (ui::Node* button = Root().FindById("section." + all[current_].title)) {
                    SetFocus(button);
                    return true;
                }
            }
        }
        if (GetStage().Depth() > 1) {
            GetStage().Pop();
        } else {
            GetStage().Replace("home");
        }
        return true;
    }

private:
    // What the rows show that changes without a row being used.
    static std::uint64_t Signature() {
        Services& svc = Svc();
        return svc.library->Revision() * 31 + svc.plugins->Revision() * 7 +
               static_cast<std::uint64_t>(svc.input->PadCount());
    }

    void FocusFirstRow() {
        if (ui::Node* rows = Root().FindById("settings.rows")) {
            std::vector<ui::Node*> all;
            rows->CollectFocusable(all);
            if (!all.empty()) {
                SetFocus(all.front());
            }
        }
    }

    std::size_t current_ = 0;
    std::uint64_t signature_ = 0;
    std::string focus_after_build_;
    bool started_ = false;
};

} // namespace

DEN_REGISTER_SCREEN("settings", SettingsScreen);
DEN_REGISTER_RAIL(90, "Settings", "settings");

} // namespace den::app
