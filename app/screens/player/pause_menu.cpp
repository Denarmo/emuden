// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pause menu of the player screen: a panel at the left, its pages as rows of data.

#include <algorithm>
#include <format>

#include "den/app/strings.h"
#include "den/app/frame.h"
#include "den/app/apps_catalog.h"
#include "den/app/cheats.h"
#include "den/app/second_screen.h"
#include "den/app/pages.h"
#include "den/base/files.h"
#include "den/app/glyphs.h"
#include "den/app/picture_choice.h"
#include "den/app/touch_settings.h"
#include "den/platform/audio.h"
#include "player_screen.h"
#include "den/app/volume_rows.h"

namespace den::app {

namespace {

using input::Nav;

constexpr float ROW_HEIGHT = 48;
constexpr float VOLUME_HEIGHT = 64; // a label and what it belongs to under it

} // namespace

void PlayerScreen::BuildMenu() {
    const ui::Theme& theme = *Svc().theme;
    ui::Node& root = Root();
    // Beside the picture, or under it where the window has no room for both; all of the
    // display when the game is on the other one.
    built_apart_ = menu_apart_;
    menu_ = PlaceMenu(menu_area_.w > 0 ? menu_area_ : area_,
                      !menu_apart_ && (view_ == View::Picture || view_ == View::PictureAdjust));
    const bool choosing = view_ == View::Picture || view_ == View::PictureAdjust;
    const gfx::Rect active_area = menu_area_.w > 0 ? menu_area_ : area_;
    const bool full_portrait = !menu_apart_ && !choosing && active_area.h > active_area.w;
    root.axis = menu_.stacked ? ui::Axis::Column : ui::Axis::Row;
    if (menu_.stacked) {
        root.Add(ui::MakeBox(ui::Axis::Column))->height = menu_.room.h;
    }
    ui::Node* panel = root.Add(ui::MakeBox(ui::Axis::Column, 10));
    panel->id = "menu";
    if (menu_.stacked || menu_apart_ || full_portrait) {
        panel->grow = 1;
    } else {
        panel->width = menu_.panel.w;
    }
    panel->background = theme.dialog;
    const Services::SafePadding safe = SafeMargins();
    panel->pad_left = menu_.stacked ? 24.0f + safe.left : std::max(24.0f, safe.left + 12.0f);
    panel->pad_right = 24.0f + safe.right;
    panel->pad_top = menu_.stacked ? 22.0f : std::max(26.0f, safe.top + 10.0f);
    panel->pad_bottom = std::max(26.0f, safe.bottom + 10.0f);

    const char* titles[] = {"",        "",        "Save state", "Load state",         "Core options",
                            "Buttons", "Players", "Picture",    "Adjust the picture", "Screens", ""};
    const bool main = view_ == View::Main;
    const std::string title = main                       ? game_.name
                              : view_ == View::ButtonPick ? PadButtonText(PadFamilyShown(), pick_) + " does"
                                                          : titles[static_cast<int>(view_)];
    panel->Emplace<ui::Label>(title, theme.text_heading, gfx::FONT_BOLD);
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
    case View::ButtonPick:
        BuildButtonPick(list);
        break;
    case View::Players:
        BuildPlayers(list);
        break;
    case View::Picture:
        BuildPicture(list);
        break;
    case View::PictureAdjust:
        BuildPictureAdjust(list);
        break;
    case View::Screens:
        AddRows(list, dual_.Rows(), ROW_HEIGHT);
        break;
    case View::Closed:
        break;
    }
    // Hints in the glyphs of the pad in use; none where there is no button to name.
    if (!Svc().input->TouchOnly()) {
        auto* hints = panel->Emplace<HintLine>(
            Svc().input->ActiveFamily(),
            std::vector<HintLine::Item>{{Nav::Confirm, "Select"}, {Nav::Back, main ? "Resume" : "Back"}});
        hints->pad_top = 6;
    } else if (!main) {
        auto* back = panel->Emplace<ui::Button>("Back", [this] { OnNav(Nav::Back); });
        back->id = "menu.back";
    }
}

void PlayerScreen::BuildMain(ui::Node* list) {
    const player::Snapshot snapshot = Svc().player->GetSnapshot();
    // An engine app draws the game and has the pads: only what it can do is offered.
    const bool external = snapshot.external;
    std::vector<ui::RowSpec> rows;
    rows.push_back(ActionRow("menu.resume", "Resume", [this] { CloseMenu(); }));
    if (snapshot.can_save_states) {
        rows.push_back(ActionRow("menu.save", "Save state", [this] { Show(View::Save); }));
        if (snapshot.hardcore) {
            rows.push_back(ValueRow("menu.load", "Load state", [] { return std::string("Off in hardcore mode"); }));
        } else if (!snapshot.together) {
            rows.push_back(ActionRow("menu.load", "Load state", [this] { Show(View::Load); }));
        }
    } else if (!external) {
        rows.push_back(ValueRow("menu.save", "Save states", [] { return std::string("Not with this core"); }));
    }
    const PauseContext pause{game_, snapshot};
    const auto feature_rows = [&](PauseSlot slot) {
        for (const PlayerHooks& hooks : AllPlayerHooks()) {
            if (hooks.pause_rows && hooks.slot == slot) {
                for (ui::RowSpec& row : hooks.pause_rows(pause)) {
                    rows.push_back(std::move(row));
                }
            }
        }
    };
    feature_rows(PauseSlot::AfterStates);
    if (snapshot.can_fast_forward && !snapshot.together) {
        rows.push_back(ToggleRow("menu.fast", "Fast-forward", [this] { return fast_toggle_; },
                                 [this](bool on) { fast_toggle_ = on; }));
    }
    if (!external && !snapshot.together) {
        rows.push_back(ValueRow("menu.rewind", "Rewind", [this, can = snapshot.can_rewind, hardcore = snapshot.hardcore] {
            if (hardcore) {
                return std::string("Off in hardcore mode");
            }
            if (!can) {
                return std::string("Not with this core");
            }
            if (PadFamilyShown() == input::PadFamily::Keyboard) {
                return std::string("Hold R while playing");
            }
            const input::PadFamily family = PadFamilyShown();
            return console_.right_stick_holds
                       ? std::string("Hold the right stick left")
                       : TrF("Press {} + {}", PadButtonText(family, input::PadButton::Select),
                             PadButtonText(family, input::PadButton::L1));
        }));
    }
    if (snapshot.can_picture) {
        rows.push_back(ChoiceRow(
            "menu.size", "Screen size", ScreenSizeNames(), [this] { return static_cast<int>(size_); },
            [this](int index) { SetScreenSize(index); }));
    }
    if (CanWidescreen()) {
        ui::RowSpec wide = ToggleRow("menu.widescreen", "Widescreen", [this] { return Widescreen(); },
                                     [this](bool on) {
                                         if (!WidescreenRestarts()) {
                                             return SetWidescreen(on);
                                         }
                                         GetStage().Confirm(WIDESCREEN_TITLE, WIDESCREEN_TEXT, "Start again",
                                                            [this, on] { SetWidescreen(on); });
                                     });
        wide.description = WidescreenRestarts() ? "For this game; it starts again from the beginning"
                                                : "For this game";
        rows.push_back(std::move(wide));
    }
    if (snapshot.can_picture || external) {
        // A little taller, for the line under it that says what it does or why it cannot.
        AddRows(list, std::move(rows), ROW_HEIGHT);
        rows.clear();
        AddRows(list, {BfiRow(snapshot)}, VOLUME_HEIGHT);
    }
    if (dual_.Active()) {
        // Two screens: which display or which place each has.
        rows.push_back(ActionRow("menu.swap", "Swap screens", [this] {
            dual_.Swap();
            Rebuild();
        }));
        rows.push_back(ValueRow(
            "menu.screens", "Screens", [this] { return dual_.Summary(); }, [this] { Show(View::Screens); }));
    } else if (SecondScreenReady() && !external) {
        // Two displays: the game and the quick settings change places.
        rows.push_back(ActionRow("menu.swap", "Swap screens", [this] { SwapDisplays(); }));
    }
    if (snapshot.can_picture) {
        rows.push_back(ValueRow(
            "menu.picture", "Picture", [this] { return PictureName(picture_, game_.system); },
            [this] { Show(View::Picture, "picture." + picture_); }));
    }
    if (snapshot.can_options) {
        rows.push_back(ValueRow(
            "menu.options", external ? "Engine settings" : "Core options",
            [this] { return std::format("{}", options_.size()); },
            [this] {
                OpenPage("options");
                direct_ = false;
            }));
    }
    if (snapshot.can_cheats) {
        rows.push_back(ValueRow("menu.cheats", "Cheats", [] { return CheatsSummary(); },
                                [this] { OpenCheats(GetStage()); }));
    }
    if (!external) {
        rows.push_back(TouchModeRow());
        rows.push_back(ActionRow("menu.buttons", "Buttons", [this] { Show(View::Buttons); }));
        rows.push_back(ValueRow(
            "menu.players", "Players",
            [] { return std::format("{}", std::max(1, std::min(4, Svc().input->PadCount()))); },
            [this] { Show(View::Players); }));
        feature_rows(PauseSlot::AfterPlayers);
    }
    // The volumes sit between the rows above and below, a little taller for their second line.
    AddRows(list, std::move(rows), ROW_HEIGHT);
    if (snapshot.can_volume) {
        AddRows(list, VolumeRows("menu", pal::audio::HasSystemVolume()), VOLUME_HEIGHT);
    }
    rows.clear();
    if (snapshot.can_reset) {
        rows.push_back(ActionRow("menu.reset", "Reset", [this] {
            GetStage().Confirm("Reset the game?", "The game starts again. Progress that is not saved is lost.",
                               "Reset", [this] {
                                   Svc().player->Reset();
                                   CloseMenu();
                               });
        }));
    }
    if (AppsAvailable() && !SecondScreenReady()) {
        // One display: an app takes it and the game waits, paused as it is now. With two
        // the quick settings have the button, and the app opens beside the game.
        rows.push_back(ActionRow("menu.apps", "Apps", [this] { GetStage().Push("apps", {{"from", "game"}}); }));
    }
    rows.push_back(ActionRow("menu.exit", "Exit to home", [this] { Leave(); }));
    options_ = Svc().player->Options();
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

ui::RowSpec PlayerScreen::BfiRow(const player::Snapshot& snapshot) {
    const std::string refused = bfi_.Refusal(snapshot);
    if (!refused.empty() && !bfi_.On()) {
        // Nothing to turn on here; the row says why.
        ui::RowSpec row = ValueRow("menu.bfi", BFI_LABEL, [] { return std::string("Not available"); },
                                   [this, refused] { GetStage().Toast(refused); });
        row.description = refused;
        return row;
    }
    ui::RowSpec row = ToggleRow("menu.bfi", BFI_LABEL, [this] { return bfi_.On(); }, [this](bool on) { SetBfi(on); });
    row.description = bfi_.Status(snapshot);
    row.live_description = [this] { return bfi_.Status(Svc().player->GetSnapshot()); };
    return row;
}

void PlayerScreen::SetBfi(bool on) {
    bfi_.SetOn(on);
    Rebuild();
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
        GetStage().Toast(TrF("Saved to slot {}", slot + 1));
        // The slot's picture changed under the same file name.
        Svc().textures->Forget(PathToUtf8(Svc().player->StateThumbnail(slot)));
        Rebuild();
    };
    if (Svc().player->HasState(slot)) {
        GetStage().Confirm(TrF("Replace slot {}?", slot + 1), "The state saved there is overwritten.",
                           "Replace", save);
    } else {
        save();
    }
}

void PlayerScreen::LoadSlot(int slot) {
    if (!Svc().player->HasState(slot)) {
        GetStage().Toast(TrF("Slot {} is empty", slot + 1));
        return;
    }
    const Status loaded = Svc().player->LoadState(slot);
    if (!loaded) {
        GetStage().Toast(loaded.error.empty() ? "The state could not be loaded" : loaded.error, 5.0f);
        return;
    }
    GetStage().Toast(TrF("Loaded slot {}", slot + 1));
    CloseMenu();
}

} // namespace den::app
