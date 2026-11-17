// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// System settings, in two levels: with the rail, the sections features registered with
// DEN_REGISTER_SETTINGS; a chosen section then gives the rail up for the sections beside its
// rows, or for its rows alone on a narrow page.

#include <algorithm>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "page_widths.h"

namespace den::app {

namespace {

class SettingsScreen final : public ui::Page {
public:
    void Build() override {
        const std::vector<SettingsSection> sections = Registry::Get().Settings();
        if (!started_) {
            started_ = true;
            const std::string wanted = Param("section");
            for (std::size_t i = 0; i < sections.size(); ++i) {
                if (!wanted.empty() && EqualsNoCase(sections[i].title, wanted)) {
                    current_ = i;
                    opened_ = true;
                    focus_after_build_ = "section." + sections[i].title;
                }
            }
        }
        current_ = std::min(current_, sections.empty() ? 0 : sections.size() - 1);
        signature_ = Signature();
        side_by_side_ = SideBySide();
        if (!opened_ || sections.empty()) {
            BuildSections(sections);
        } else {
            BuildOpened(sections);
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
        // Into the rows once they are built: a tap on a section rebuilds the page first.
        if (focus_rows_) {
            focus_rows_ = false;
            FocusFirstRow();
        }
        if (Signature() != signature_) {
            Rebuild();
        }
        if (opened_ && SideBySide() != side_by_side_) {
            Rebuild(false);
        }
    }

    std::vector<ui::Hint> Hints() const override {
        if (!opened_) {
            return {{input::Nav::Confirm, "Open"}, {input::Nav::Back, "Back"}};
        }
        return {{input::Nav::Confirm, "Change"}, {input::Nav::Back, "Back"}};
    }

    bool OnNav(input::Nav nav) override {
        ui::Node* sections = Root().FindById("settings.sections");
        const bool on_sections = opened_ && Focus() && sections && IsUnder(Focus(), sections);
        if (nav == input::Nav::Left && on_sections) {
            Close(); // towards the rail, as from the sections beside it
            return true;
        }
        if (nav != input::Nav::Back) {
            return false;
        }
        // Back leaves the rows for their section, then the section for the rail.
        if (opened_) {
            ui::Node* rows = Root().FindById("settings.rows");
            ui::Node* button = Root().FindById(SectionId());
            if (side_by_side_ && button && Focus() && rows && IsUnder(Focus(), rows)) {
                SetFocus(button);
            } else {
                Close();
            }
            return true;
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

    static bool SideBySide() {
        return SettingsSideBySide(LevelContentWidth(PageWidth(), Svc().theme->margin));
    }

    // The first level: the rail and the sections.
    void BuildSections(const std::vector<SettingsSection>& sections) {
        ui::Node* content = BuildRailFrame(*this, "Settings");
        content->pad_right = Svc().theme->margin;
        ui::Node* list = content->Add(ui::MakeList(6));
        list->id = "settings.sections";
        list->pad_bottom = 24;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            auto* button = list->Emplace<ui::Button>(sections[i].title);
            button->id = "section." + sections[i].title;
            button->centered = false;
            button->on_activate = [this, i] { Open(i); };
        }
    }

    // The second level, without the rail: the sections beside the chosen one's rows, or its
    // rows alone under its name.
    void BuildOpened(const std::vector<SettingsSection>& sections) {
        ui::Node* content = BuildLevelFrame(*this, side_by_side_ ? "Settings" : sections[current_].title);
        ui::Node* rows_parent = content;
        if (side_by_side_) {
            ui::Node* panes = content->Add(ui::MakeBox(ui::Axis::Row, SETTINGS_PANE_GAP));
            panes->grow = 1;
            ui::Node* list = panes->Add(ui::MakeList(6));
            list->id = "settings.sections";
            list->grow = 0;
            list->width = SETTINGS_SECTIONS_WIDTH;
            list->pad_bottom = 24;
            for (std::size_t i = 0; i < sections.size(); ++i) {
                auto* button = list->Emplace<ui::Button>(sections[i].title);
                button->id = "section." + sections[i].title;
                button->centered = false;
                button->selected = i == current_;
                button->on_focus = [this, i] {
                    if (current_ != i && focus_after_build_.empty()) {
                        current_ = i;
                        Rebuild(false);
                    }
                };
                button->on_activate = [this] { focus_rows_ = true; };
            }
            rows_parent = panes;
        }
        ui::Node* rows = rows_parent->Add(ui::MakeList(8));
        rows->id = "settings.rows";
        rows->pad_bottom = 24;
        AddRows(rows, sections[current_].rows());
    }

    void Open(std::size_t section) {
        current_ = section;
        opened_ = true;
        focus_after_build_.clear();
        focus_rows_ = true;
        Rebuild(false);
    }

    void Close() {
        opened_ = false;
        focus_rows_ = false;
        focus_after_build_ = SectionId();
        Rebuild(false);
    }

    std::string SectionId() const {
        const std::vector<SettingsSection> all = Registry::Get().Settings();
        return current_ < all.size() ? "section." + all[current_].title : std::string();
    }

    static bool IsUnder(const ui::Node* node, const ui::Node* ancestor) {
        for (; node; node = node->Parent()) {
            if (node == ancestor) {
                return true;
            }
        }
        return false;
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
    bool side_by_side_ = true; // an opened section shows the sections beside its rows
    bool opened_ = false;      // the second level: a section's rows, without the rail
    bool focus_rows_ = false;  // the focus goes into the rows after the next build
};

} // namespace

DEN_REGISTER_SCREEN("settings", SettingsScreen);
DEN_REGISTER_RAIL(90, "Settings", "settings");

} // namespace den::app
