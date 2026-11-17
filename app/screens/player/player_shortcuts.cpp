// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pause menu's Buttons: every control of the pad in use with what it does, and for the one
// chosen, the console's buttons and the shortcuts this game's core can do. A control does one
// thing: giving it a shortcut takes it away from the game.

#include <algorithm>
#include <format>

#include "den/app/strings.h"
#include "den/app/pages.h"
#include "den/base/files.h"
#include "den/app/glyphs.h"
#include "den/app/picture_choice.h"
#include "player_screen.h"

namespace den::app {

namespace {

using input::PadButton;
using input::PadFamily;
using player::Shortcut;

constexpr float ROW_HEIGHT = 48;
constexpr float TRIGGER_PRESSED = 0.35f;
constexpr int QUICK_SLOT = 0;

// The game buttons a console has when its plugin names none.
constexpr const char* DEFAULT_BUTTONS[] = {"a", "b", "x", "y", "l", "r", "start", "select"};

// Every control a pad has, in the order the page lists them. The home button opens the menu.
constexpr PadButton CONTROLS[] = {
    PadButton::South,  PadButton::East,     PadButton::West,     PadButton::North,
    PadButton::DpadUp, PadButton::DpadDown, PadButton::DpadLeft, PadButton::DpadRight,
    PadButton::L1,     PadButton::R1,       PadButton::L2,       PadButton::R2,
    PadButton::L3,     PadButton::R3,       PadButton::Start,    PadButton::Select};

std::string ShortcutLabel(Shortcut shortcut) {
    switch (shortcut) {
    case Shortcut::Menu:
        return "Open the menu";
    case Shortcut::FastForward:
        return "Fast-forward on or off";
    case Shortcut::Rewind:
        return "Rewind on or off";
    case Shortcut::SaveState:
        return TrF("Save state (slot {})", QUICK_SLOT + 1);
    case Shortcut::LoadState:
        return TrF("Load state (slot {})", QUICK_SLOT + 1);
    case Shortcut::NextPicture:
        return "Next picture";
    case Shortcut::SwapScreens:
        return "Swap screens";
    case Shortcut::FrameRate:
        return "Frame rate";
    case Shortcut::Microphone:
        return "Microphone (hold)";
    default:
        return {};
    }
}

std::string Upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::uint32_t Bit(PadButton button) {
    return 1u << static_cast<std::uint32_t>(button);
}

// The console's buttons by RetroPad index, with the names its plugin gives them.
std::vector<std::pair<int, std::string>> GameButtons(const plugins::Console& console) {
    std::vector<std::pair<int, std::string>> buttons;
    for (const plugins::ButtonName& button : console.buttons) {
        if (const int index = player::GameButtonFromName(button.pad); index >= 0) {
            buttons.emplace_back(index, button.label.empty() ? Upper(button.pad) : button.label);
        }
    }
    if (buttons.empty()) {
        for (const char* name : DEFAULT_BUTTONS) {
            buttons.emplace_back(player::GameButtonFromName(name), Upper(name));
        }
    }
    // Every console has a D-pad, named or not.
    for (const auto& [name, label] : {std::pair{"up", "Up"}, {"down", "Down"}, {"left", "Left"}, {"right", "Right"}}) {
        const int index = player::GameButtonFromName(name);
        if (std::none_of(buttons.begin(), buttons.end(), [index](const auto& b) { return b.first == index; })) {
            buttons.emplace_back(index, label);
        }
    }
    return buttons;
}

} // namespace

void PlayerScreen::LoadShortcutMap() {
    shortcuts_ = player::LoadShortcuts(Svc().settings, game_.system);
    shortcut_down_ = 0;
}

std::vector<Shortcut> PlayerScreen::ShortcutsHere() const {
    const player::Snapshot snapshot = Svc().player->GetSnapshot();
    std::vector<Shortcut> here{Shortcut::Menu};
    if (snapshot.can_fast_forward) {
        here.push_back(Shortcut::FastForward);
    }
    if (snapshot.can_rewind) {
        here.push_back(Shortcut::Rewind);
    }
    if (snapshot.can_save_states) {
        here.push_back(Shortcut::SaveState);
        here.push_back(Shortcut::LoadState);
    }
    if (snapshot.can_picture) {
        here.push_back(Shortcut::NextPicture);
    }
    if (dual_.Active()) {
        here.push_back(Shortcut::SwapScreens);
    }
    if (Svc().player->MicrophoneButton() >= 0) {
        here.push_back(Shortcut::Microphone);
    }
    here.push_back(Shortcut::FrameRate);
    return here;
}

std::string PlayerScreen::ControlText(PadButton control) const {
    for (int i = 0; i < player::SHORTCUTS; ++i) {
        if (shortcuts_[static_cast<std::size_t>(i)] == control) {
            return ShortcutLabel(static_cast<Shortcut>(i));
        }
    }
    const player::ButtonMap map = player::LoadButtons(Svc().settings, game_.system, PadFamilyShown(),
                                                      Svc().settings->GetBool("player.pad_by_label", true));
    std::string text;
    const auto buttons = GameButtons(console_);
    const int mic = Svc().player->MicrophoneButton();
    for (int index = 0; index < player::GAME_BUTTONS; ++index) {
        if (map[static_cast<std::size_t>(index)] != control) {
            continue;
        }
        const auto named = std::find_if(buttons.begin(), buttons.end(),
                                         [index](const auto& button) { return button.first == index; });
        if (named != buttons.end()) {
            text += (text.empty() ? "" : ", ") + named->second;
        } else if (index == mic) {
            text += (text.empty() ? "" : ", ") + std::string("Microphone");
        }
    }
    return text.empty() ? "Nothing" : text;
}

void PlayerScreen::BuildButtons(ui::Node* list) {
    const PadFamily family = PadFamilyShown();
    for (const PadButton control : CONTROLS) {
        if (PadButtonText(family, control).empty()) {
            continue; // a keyboard has no such key
        }
        auto* row = list->Emplace<GlyphRow>(std::string("buttons.control.") + player::PadButtonName(control), family,
                                            control, ControlText(control));
        row->on_activate = [this, control] {
            pick_ = control;
            Show(View::ButtonPick, "pick.current");
        };
    }
    std::vector<ui::RowSpec> rows;
    if (player::HasRemap(*Svc().settings, game_.system)) {
        rows.push_back(ActionRow("buttons.reset", "Default buttons", [this] {
            player::ResetButtons(*Svc().settings, game_.system);
            Svc().player->ReloadButtons();
            LoadShortcutMap();
            Rebuild();
        }));
    }
    const std::string select = PadButtonText(family, PadButton::Select);
    const auto add = [&rows](const std::string& what, const std::string& how) {
        rows.push_back(ValueRow(std::format("buttons.info.{}", rows.size()), what, [how] { return how; }));
    };
    if (family != PadFamily::Keyboard) {
        add("Menu, always", std::format("{} + {}, or hold {}", PadButtonText(family, PadButton::Start), select, select));
    }
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

void PlayerScreen::BuildButtonPick(ui::Node* list) {
    const std::string current = ControlText(pick_);
    const auto mark = [&current](const std::string& label) {
        return std::string(label == current ? "In use" : "");
    };
    std::vector<ui::RowSpec> rows;
    rows.push_back(ValueRow("pick.current", "Now", [current] { return current; }));
    rows.push_back(ValueRow("pick.none", "Nothing", [mark] { return mark("Nothing"); },
                            [this] { Assign(pick_, -1, Shortcut::Count); }));
    AddRows(list, std::move(rows), ROW_HEIGHT);

    list->Add(ui::MakeHeading(console_.short_name.empty() ? console_.name : console_.short_name));
    rows.clear();
    for (const auto& [index, name] : GameButtons(console_)) {
        rows.push_back(ValueRow(std::format("pick.game.{}", player::GameButtonName(index)), name,
                                [mark, label = name] { return mark(label); },
                                [this, game = index] { Assign(pick_, game, Shortcut::Count); }));
    }
    AddRows(list, std::move(rows), ROW_HEIGHT);

    list->Add(ui::MakeHeading("Shortcuts"));
    rows.clear();
    for (const Shortcut shortcut : ShortcutsHere()) {
        rows.push_back(ValueRow(std::string("pick.") + player::ShortcutName(shortcut), ShortcutLabel(shortcut),
                                [mark, label = ShortcutLabel(shortcut)] { return mark(label); },
                                [this, shortcut] { Assign(pick_, -1, shortcut); }));
    }
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

void PlayerScreen::Assign(PadButton control, int game_button, Shortcut shortcut) {
    store::Settings& settings = *Svc().settings;
    const player::ButtonMap map = player::LoadButtons(&settings, game_.system, PadFamilyShown(),
                                                      settings.GetBool("player.pad_by_label", true));
    // What the control did before goes.
    for (int i = 0; i < player::GAME_BUTTONS; ++i) {
        if (map[static_cast<std::size_t>(i)] == control && i != game_button) {
            player::SaveButton(settings, game_.system, i, PadButton::Count);
        }
    }
    for (int i = 0; i < player::SHORTCUTS; ++i) {
        if (shortcuts_[static_cast<std::size_t>(i)] == control) {
            player::SaveShortcut(settings, game_.system, static_cast<Shortcut>(i), PadButton::Count);
        }
    }
    if (game_button >= 0) {
        player::SaveButton(settings, game_.system, game_button, control);
    } else if (shortcut != Shortcut::Count) {
        player::SaveShortcut(settings, game_.system, shortcut, control);
    }
    Svc().player->ReloadButtons();
    LoadShortcutMap();
    Show(View::Buttons, std::string("buttons.control.") + player::PadButtonName(control));
}

void PlayerScreen::FeedShortcuts(input::PadState* pads, int count) {
    std::uint32_t down = 0;
    for (int i = 0; i < player::SHORTCUTS; ++i) {
        const PadButton control = shortcuts_[static_cast<std::size_t>(i)];
        if (control == PadButton::Count) {
            continue;
        }
        for (int port = 0; port < count; ++port) {
            input::PadState& pad = pads[port];
            const bool trigger = (control == PadButton::L2 && pad.lt > TRIGGER_PRESSED) ||
                                 (control == PadButton::R2 && pad.rt > TRIGGER_PRESSED);
            if (pad.Down(control) || trigger) {
                down |= Bit(control);
            }
            // The control is the shortcut's, not the game's.
            pad.buttons &= ~Bit(control);
            pad.lt = control == PadButton::L2 ? 0 : pad.lt;
            pad.rt = control == PadButton::R2 ? 0 : pad.rt;
        }
    }
    const std::uint32_t pressed = down & ~shortcut_down_;
    shortcut_down_ = down;
    // The microphone is held, not pressed: the core hears noise while the control is down.
    const PadButton mic_control = shortcuts_[static_cast<std::size_t>(Shortcut::Microphone)];
    const int mic = Svc().player->MicrophoneButton();
    const bool blowing = mic >= 0 && mic_control != PadButton::Count && (down & Bit(mic_control));
    Svc().player->SetHeldButtons(blowing ? static_cast<std::uint16_t>(1u << mic) : 0);
    for (int i = 0; i < player::SHORTCUTS; ++i) {
        const PadButton control = shortcuts_[static_cast<std::size_t>(i)];
        if (control != PadButton::Count && (pressed & Bit(control))) {
            RunShortcut(static_cast<Shortcut>(i));
        }
    }
}

void PlayerScreen::RunShortcut(Shortcut shortcut) {
    switch (shortcut) {
    case Shortcut::Menu:
        OpenMenu();
        break;
    case Shortcut::FastForward:
        fast_toggle_ = !fast_toggle_;
        rewind_toggle_ = false;
        break;
    case Shortcut::Rewind:
        rewind_toggle_ = !rewind_toggle_;
        fast_toggle_ = fast_toggle_ && !rewind_toggle_;
        break;
    case Shortcut::SaveState:
        if (const Status saved = Svc().player->SaveState(QUICK_SLOT); saved) {
            GetStage().Toast(TrF("Saved to slot {}", QUICK_SLOT + 1));
            Svc().textures->Forget(PathToUtf8(Svc().player->StateThumbnail(QUICK_SLOT)));
        } else {
            GetStage().Toast(saved.error.empty() ? "The state could not be saved" : saved.error, 5.0f);
        }
        break;
    case Shortcut::LoadState:
        if (!Svc().player->HasState(QUICK_SLOT)) {
            GetStage().Toast(TrF("Slot {} is empty", QUICK_SLOT + 1));
        } else if (const Status loaded = Svc().player->LoadState(QUICK_SLOT); loaded) {
            GetStage().Toast(TrF("Loaded slot {}", QUICK_SLOT + 1));
        } else {
            GetStage().Toast(loaded.error.empty() ? "The state could not be loaded" : loaded.error, 5.0f);
        }
        break;
    case Shortcut::NextPicture: {
        std::vector<std::string> ids{player::PICTURE_AUTO};
        for (const player::PicturePreset& preset : player::PicturePresets()) {
            ids.push_back(preset.id);
        }
        const auto it = std::find(ids.begin(), ids.end(), picture_);
        picture_ = it == ids.end() || it + 1 == ids.end() ? ids.front() : *(it + 1);
        SavePicture(game_, static_cast<PictureScope>(picture_scope_), picture_);
        Svc().player->SetPicture(picture_);
        GetStage().Toast(PictureName(picture_, game_.system));
        break;
    }
    case Shortcut::SwapScreens:
        if (dual_.Active()) {
            dual_.Swap();
        }
        break;
    case Shortcut::FrameRate:
        SetFpsMode((fps_mode_ + 1) % FPS_MODES);
        break;
    default:
        break;
    }
}

} // namespace den::app
