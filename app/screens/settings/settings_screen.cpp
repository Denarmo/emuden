// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// System settings: the sections features registered with DEN_REGISTER_SETTINGS at the left,
// the rows of the chosen one at the right. On a narrow page they are two levels: the sections,
// then the rows of the one chosen.

#include <algorithm>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/ui/glyphs.h"
#include "page_widths.h"

namespace den::app {

namespace {

// A square with a chevron pointing back, for a finger (the pad's Back does the same).
class BackButton final : public ui::Node {
public:
    BackButton() {
        id = "settings.back";
        label = "Back";
        focusable = true;
        chrome = true; // the rows get the first focus
        width = height = 48;
    }

protected:
    void DrawContent(ui::Context& ctx) override {
        const ui::Theme& theme = *ctx.theme;
        ctx.canvas->FillRect(rect, focused ? theme.panel_strong : theme.panel, theme.radius);
        const float cx = rect.CenterX() - 2, cy = rect.CenterY();
        ui::DrawLine(*ctx.canvas, cx + 5, cy - 9, cx - 4, cy, 3, theme.fg);
        ui::DrawLine(*ctx.canvas, cx - 4, cy, cx + 5, cy + 9, 3, theme.fg);
    }
};

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
                    opened_ = true;
                    focus_after_build_ = "section." + sections[i].title;
                }
            }
        }
        current_ = std::min(current_, sections.empty() ? 0 : sections.size() - 1);
        signature_ = Signature();
        side_by_side_ = SettingsSideBySide(RailContentWidth(PageWidth(), svc.theme->margin));
        if (!side_by_side_) {
            BuildNarrow(sections);
            return;
        }

        ui::Node* content = BuildRailFrame(*this, "Settings");
        content->pad_right = svc.theme->margin;
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
        if (SettingsSideBySide(RailContentWidth(PageWidth(), Svc().theme->margin)) != side_by_side_) {
            // Narrowed with the rows in use: they stay open on their own.
            ui::Node* rows = Root().FindById("settings.rows");
            opened_ = side_by_side_ ? rows && Focus() && IsUnder(Focus(), rows) : opened_;
            if (!opened_) {
                focus_after_build_ = SectionId();
            }
            Rebuild(false);
        }
    }

    std::vector<ui::Hint> Hints() const override {
        if (!side_by_side_ && !opened_) {
            return {{input::Nav::Confirm, "Open"}, {input::Nav::Back, "Back"}};
        }
        return {{input::Nav::Confirm, "Change"}, {input::Nav::Back, "Back"}};
    }

    bool OnNav(input::Nav nav) override {
        if (nav != input::Nav::Back) {
            return false;
        }
        // Back leaves the rows first, then the page.
        if (!side_by_side_ && opened_) {
            Close();
            return true;
        }
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

    // Narrow pages: the sections fill the content column, or the chosen section's rows do,
    // under its name.
    void BuildNarrow(const std::vector<SettingsSection>& sections) {
        const ui::Theme& theme = *Svc().theme;
        if (!opened_ || sections.empty()) {
            ui::Node* content = BuildRailFrame(*this, "Settings");
            content->pad_right = theme.margin;
            ui::Node* list = content->Add(ui::MakeList(6));
            list->id = "settings.sections";
            list->pad_bottom = 24;
            for (std::size_t i = 0; i < sections.size(); ++i) {
                auto* button = list->Emplace<ui::Button>(sections[i].title);
                button->id = "section." + sections[i].title;
                button->centered = false;
                button->on_activate = [this, i] {
                    current_ = i;
                    opened_ = true;
                    focus_after_build_.clear();
                    Rebuild(false);
                };
            }
            return;
        }
        ui::Node* content = BuildRailFrame(*this, "");
        content->pad_right = theme.margin;
        ui::Node* header = content->Add(ui::MakeBox(ui::Axis::Row, 12));
        header->align = ui::Align::Center;
        auto* back = header->Emplace<BackButton>();
        back->on_activate = [this] { Close(); };
        header->Emplace<ui::Label>(sections[current_].title, theme.text_title - 4, gfx::FONT_BOLD);
        ui::Node* rows = content->Add(ui::MakeList(8));
        rows->id = "settings.rows";
        rows->pad_bottom = 24;
        AddRows(rows, sections[current_].rows());
    }

    void Close() {
        opened_ = false;
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
    bool side_by_side_ = true;
    bool opened_ = false; // narrow pages: the rows of current_ are shown, not the sections
};

} // namespace

DEN_REGISTER_SCREEN("settings", SettingsScreen);
DEN_REGISTER_RAIL(90, "Settings", "settings");

} // namespace den::app
