// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pause menu of the player screen: a panel at the left, its pages as rows of data.

#include <algorithm>
#include <format>

#include "den/app/pages.h"
#include "den/base/files.h"
#include "glyphs.h"
#include "player_screen.h"

namespace den::app {

namespace {

using input::Nav;

constexpr float PANEL_WIDTH = 460;
constexpr float ROW_HEIGHT = 48;

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

    const char* titles[] = {"", "", "Save state", "Load state", "Core options", "Buttons", "Players"};
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
    case View::Players:
        BuildPlayers(list);
        break;
    case View::Closed:
        break;
    }
    // Hints in the glyphs of the pad in use.
    auto* hints = panel->Emplace<HintLine>(
        Svc().input->ActiveFamily(),
        std::vector<HintLine::Item>{{Nav::Confirm, "Select"}, {Nav::Back, main ? "Resume" : "Back"}});
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
        if (PadFamilyShown() == input::PadFamily::Keyboard) {
            return std::string("Hold R while playing");
        }
        const input::PadFamily family = PadFamilyShown();
        return console_.right_stick_holds
                   ? std::string("Hold the right stick left")
                   : "Hold " + PadButtonText(family, input::PadButton::Select) + " + " +
                         PadButtonText(family, input::PadButton::L1);
    }));
    rows.push_back(ChoiceRow(
        "menu.size", "Screen size", ScreenSizeNames(), [this] { return static_cast<int>(size_); },
        [this](int index) {
            size_ = static_cast<ScreenSize>(index);
            Svc().settings->SetNumber("player.screen_size", index);
        }));
    {
        std::vector<std::string> names;
        for (const player::PicturePreset& preset : player::PicturePresets()) {
            names.push_back(preset.name);
        }
        rows.push_back(ChoiceRow(
            "menu.picture", "Picture", std::move(names), [this] { return picture_; },
            [this](int index) { SetPicture(index); }));
    }
    rows.push_back(ValueRow(
        "menu.options", "Core options", [this] { return std::format("{}", options_.size()); },
        [this] {
            options_for_game_ = std::any_of(options_.begin(), options_.end(),
                                            [](const player::CoreOption& option) { return option.for_game; });
            Show(View::Options);
        }));
    rows.push_back(ActionRow("menu.buttons", "Buttons", [this] { Show(View::Buttons); }));
    rows.push_back(ValueRow(
        "menu.players", "Players",
        [] { return std::format("{}", std::max(1, std::min(4, Svc().input->PadCount()))); },
        [this] { Show(View::Players); }));
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

} // namespace den::app
