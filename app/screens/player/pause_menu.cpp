// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pause menu of the player screen: a panel at the left, its pages as rows of data.

#include <algorithm>
#include <format>

#include "den/app/pages.h"
#include "den/base/files.h"
#include "player_screen.h"

namespace den::app {

namespace {

using input::Nav;

constexpr float PANEL_WIDTH = 460;
constexpr float ROW_HEIGHT = 48;

std::string Glyph(Nav nav) {
    return input::GlyphLabel(Svc().input->ActiveFamily(), nav);
}

bool OnKeyboard() {
    return Svc().input->ActiveFamily() == input::PadFamily::Keyboard;
}

// What a RetroPad button is on the keyboard.
const char* KeyFor(const std::string& pad) {
    static const std::pair<const char*, const char*> keys[] = {
        {"b", "Z"},      {"a", "X"},       {"y", "A"},          {"x", "S"},
        {"l", "Q"},      {"r", "W"},       {"start", "Enter"},  {"select", "Right Shift"},
    };
    for (const auto& [name, key] : keys) {
        if (pad == name) {
            return key;
        }
    }
    return "";
}

// Where a RetroPad button is on a pad.
const char* PlaceFor(const std::string& pad) {
    static const std::pair<const char*, const char*> places[] = {
        {"b", "Bottom button"}, {"a", "Right button"}, {"y", "Left button"}, {"x", "Top button"},
        {"l", "Left shoulder"}, {"r", "Right shoulder"}, {"l2", "Left trigger"}, {"r2", "Right trigger"},
        {"l3", "Left stick press"}, {"r3", "Right stick press"}, {"start", "Start"}, {"select", "Select"},
    };
    for (const auto& [name, place] : places) {
        if (pad == name) {
            return place;
        }
    }
    return "";
}

} // namespace

void PlayerScreen::BuildMenu() {
    const ui::Theme& theme = *Svc().theme;
    ui::Node& root = Root();
    root.axis = ui::Axis::Row;
    ui::Node* panel = root.Add(ui::MakeBox(ui::Axis::Column, 10));
    panel->id = "menu";
    panel->width = PANEL_WIDTH;
    panel->background = theme.dialog;
    panel->SetPadding(24, 26);

    const char* titles[] = {"", "", "Save state", "Load state", "Core options", "Buttons"};
    const bool main = view_ == View::Main;
    panel->Emplace<ui::Label>(main ? game_.name : titles[static_cast<int>(view_)], theme.text_heading,
                              gfx::FONT_BOLD);
    auto* under = panel->Emplace<ui::Label>(main ? console_.name + "  \xC2\xB7  " + core_.name : game_.name,
                                            theme.text_small);
    under->tone = ui::Tone::Dim;
    under->pad_bottom = 8;

    ui::Node* list = panel->Add(ui::MakeList(6));
    list->id = "menu.list";
    switch (view_) {
    case View::Main:
        BuildMain(list);
        break;
    case View::Save:
        BuildSlots(list, true);
        break;
    case View::Load:
        BuildSlots(list, false);
        break;
    case View::Options:
        BuildOptions(list);
        break;
    case View::Buttons:
        BuildButtons(list);
        break;
    case View::Closed:
        break;
    }
    auto* hints = panel->Emplace<ui::Label>(
        std::format("{}  Select      {}  {}", Glyph(Nav::Confirm), Glyph(Nav::Back), main ? "Resume" : "Back"),
        theme.text_small);
    hints->tone = ui::Tone::Dim;
    hints->pad_top = 6;
}

void PlayerScreen::BuildMain(ui::Node* list) {
    const player::Snapshot snapshot = Svc().player->GetSnapshot();
    std::vector<ui::RowSpec> rows;
    rows.push_back(ActionRow("menu.resume", "Resume", [this] { CloseMenu(); }));
    if (snapshot.can_save_states) {
        rows.push_back(ActionRow("menu.save", "Save state", [this] { Show(View::Save); }));
        rows.push_back(ActionRow("menu.load", "Load state", [this] { Show(View::Load); }));
    } else {
        rows.push_back(ValueRow("menu.save", "Save states", [] { return std::string("Not with this core"); }));
    }
    rows.push_back(ToggleRow("menu.fast", "Fast-forward", [this] { return fast_toggle_; },
                             [this](bool on) { fast_toggle_ = on; }));
    rows.push_back(ValueRow("menu.rewind", "Rewind", [this, can = snapshot.can_rewind] {
        if (!can) {
            return std::string("Not with this core");
        }
        if (OnKeyboard()) {
            return std::string("Hold R while playing");
        }
        return std::string(console_.right_stick_holds ? "Hold the right stick left" : "Hold R on a keyboard");
    }));
    rows.push_back(ChoiceRow(
        "menu.size", "Screen size", ScreenSizeNames(), [this] { return static_cast<int>(size_); },
        [this](int index) {
            size_ = static_cast<ScreenSize>(index);
            Svc().settings->SetNumber("player.screen_size", index);
        }));
    rows.push_back(ValueRow(
        "menu.options", "Core options", [this] { return std::format("{}", options_.size()); },
        [this] { Show(View::Options); }));
    rows.push_back(ActionRow("menu.buttons", "Buttons", [this] { Show(View::Buttons); }));
    rows.push_back(ActionRow("menu.reset", "Reset", [this] {
        GetStage().Confirm("Reset the game?", "The game starts again. Progress that is not saved is lost.",
                           "Reset", [this] {
                               Svc().player->Reset();
                               CloseMenu();
                           });
    }));
    rows.push_back(ActionRow("menu.exit", "Exit to home", [this] { Leave(); }));
    options_ = Svc().player->Options();
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

void PlayerScreen::BuildSlots(ui::Node* list, bool save) {
    for (int slot = 0; slot < player::STATE_SLOTS; ++slot) {
        auto* row = list->Emplace<SlotRow>(slot, Svc().player->HasState(slot),
                                           Svc().player->StateThumbnail(slot));
        row->on_activate = [this, slot, save] {
            if (save) {
                SaveSlot(slot);
            } else {
                LoadSlot(slot);
            }
        };
    }
}

void PlayerScreen::SaveSlot(int slot) {
    const auto save = [this, slot] {
        const Status saved = Svc().player->SaveState(slot);
        if (!saved) {
            GetStage().Toast(saved.error.empty() ? "The state could not be saved" : saved.error, 5.0f);
            return;
        }
        GetStage().Toast(std::format("Saved to slot {}", slot + 1));
        // The slot's picture changed under the same file name.
        Svc().textures->Forget(PathToUtf8(Svc().player->StateThumbnail(slot)));
        Rebuild();
    };
    if (Svc().player->HasState(slot)) {
        GetStage().Confirm(std::format("Replace slot {}?", slot + 1), "The state saved there is overwritten.",
                           "Replace", save);
    } else {
        save();
    }
}

void PlayerScreen::LoadSlot(int slot) {
    if (!Svc().player->HasState(slot)) {
        GetStage().Toast(std::format("Slot {} is empty", slot + 1));
        return;
    }
    const Status loaded = Svc().player->LoadState(slot);
    if (!loaded) {
        GetStage().Toast(loaded.error.empty() ? "The state could not be loaded" : loaded.error, 5.0f);
        return;
    }
    GetStage().Toast(std::format("Loaded slot {}", slot + 1));
    CloseMenu();
}

void PlayerScreen::BuildOptions(ui::Node* list) {
    options_ = Svc().player->Options();
    std::vector<ui::RowSpec> rows;
    for (std::size_t i = 0; i < options_.size(); ++i) {
        const player::CoreOption& option = options_[i];
        if (option.values.empty()) {
            continue;
        }
        rows.push_back(ChoiceRow(
            "option." + option.key, option.label.empty() ? option.key : option.label, option.values,
            [this, i] {
                if (i >= options_.size()) {
                    return 0;
                }
                const player::CoreOption& current = options_[i];
                const auto it = std::find(current.values.begin(), current.values.end(), current.value);
                return it == current.values.end() ? 0 : static_cast<int>(it - current.values.begin());
            },
            [this, i](int index) {
                if (i >= options_.size() || index >= static_cast<int>(options_[i].values.size())) {
                    return;
                }
                const std::string value = options_[i].values[static_cast<std::size_t>(index)];
                Svc().player->SetOption(options_[i].key, value);
                options_[i].value = value;
            }));
    }
    if (rows.empty()) {
        rows.push_back(ValueRow("option.none", "This core has no options", [] { return std::string(); }));
    }
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

void PlayerScreen::BuildButtons(ui::Node* list) {
    const bool keyboard = OnKeyboard();
    std::vector<ui::RowSpec> rows;
    const auto add = [&rows](const std::string& what, const std::string& how) {
        rows.push_back(ValueRow(std::format("buttons.{}", rows.size()), what, [how] { return how; }));
    };
    add("Menu", keyboard ? "Esc" : std::format("Hold {} + {}", Glyph(Nav::Menu), Glyph(Nav::View)));
    add("Fast-forward", keyboard || !console_.right_stick_holds ? "Hold F" : "Right stick right");
    add("Rewind", keyboard || !console_.right_stick_holds ? "Hold R" : "Right stick left");
    add("Frame rate", "F3");
    if (keyboard) {
        add("Directions", "Arrow keys");
    }
    // The console's own names for the buttons, from its plugin manifest.
    for (const plugins::ButtonName& button : console_.buttons) {
        const std::string where = keyboard ? KeyFor(button.pad) : PlaceFor(button.pad);
        if (!where.empty()) {
            add(button.label.empty() ? button.pad : button.label, where);
        }
    }
    if (console_.buttons.empty()) {
        add("Game buttons", keyboard ? "Z X A S, Q W, Enter, right Shift" : "By their place on the pad");
    }
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

} // namespace den::app
