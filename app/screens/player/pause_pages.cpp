// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Pages of the pause menu: core options (for the console or for this game), the buttons with
// their remap, the players, and the picture preset.

#include <algorithm>
#include <format>

#include "den/app/pages.h"
#include "den/player/buttons.h"
#include "glyphs.h"
#include "player_screen.h"

namespace den::app {

namespace {

using input::Nav;
using input::PadButton;
using input::PadFamily;

constexpr float ROW_HEIGHT = 48;
constexpr double CAPTURE_SECONDS = 6.0;
constexpr float TRIGGER_PRESSED = 0.35f;
constexpr int MAX_PORTS = 4;

// The game buttons a console has when its plugin names none.
constexpr const char* DEFAULT_BUTTONS[] = {"a", "b", "x", "y", "l", "r", "start", "select"};

std::string Upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

} // namespace

PadFamily PlayerScreen::PadFamilyShown() const {
    return Svc().input->ActiveFamily();
}

void PlayerScreen::SetPicture(int index) {
    const auto& presets = player::PicturePresets();
    if (index < 0 || index >= static_cast<int>(presets.size())) {
        return;
    }
    picture_ = index;
    const std::string& id = presets[static_cast<std::size_t>(index)].id;
    // Remembered for the console: a handheld and a home console want different pictures.
    Svc().settings->SetString("player.picture_for." + game_.system, id);
    Svc().player->SetPicture(id);
}

void PlayerScreen::BuildOptions(ui::Node* list) {
    options_ = Svc().player->Options();
    std::vector<ui::RowSpec> rows;
    const std::string everyone = "All " + (console_.short_name.empty() ? console_.name : console_.short_name) + " games";
    rows.push_back(ChoiceRow(
        "option.scope", "Apply to", {everyone, "This game"}, [this] { return options_for_game_ ? 1 : 0; },
        [this](int index) { options_for_game_ = index == 1; }));
    const bool own = std::any_of(options_.begin(), options_.end(),
                                 [](const player::CoreOption& option) { return option.for_game; });
    if (own) {
        rows.push_back(ActionRow("option.reset", "Use the console's options for this game", [this] {
            Svc().player->ClearGameOptions();
            options_for_game_ = false;
            GetStage().Toast("This game follows the console's options again");
            Rebuild();
        }));
    }
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
                Svc().player->SetOption(options_[i].key, value,
                                        options_for_game_ ? player::OptionScope::Game
                                                          : player::OptionScope::Console);
                options_[i].value = value;
            }));
    }
    if (options_.empty()) {
        rows.push_back(ValueRow("option.none", "This core has no options", [] { return std::string(); }));
    }
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

void PlayerScreen::BuildButtons(ui::Node* list) {
    Services& svc = Svc();
    const PadFamily family = PadFamilyShown();
    const bool keyboard = family == PadFamily::Keyboard;
    const player::ButtonMap map = player::LoadButtons(
        svc.settings, game_.system, family, svc.settings->GetBool("player.pad_by_label", true));

    // The console's own names for its buttons, from its plugin manifest; each row shows the
    // button of the pad in use that presses it, and Confirm asks for another.
    std::vector<std::pair<int, std::string>> buttons;
    for (const plugins::ButtonName& button : console_.buttons) {
        if (const int index = player::GameButtonFromName(button.pad); index >= 0) {
            buttons.emplace_back(index, button.label.empty() ? Upper(button.pad) : button.label);
        }
    }
    if (buttons.empty()) {
        for (const char* name : DEFAULT_BUTTONS) {
            buttons.emplace_back(player::GameButtonFromName(name), Upper(name));
        }
    }
    for (const auto& [index, name] : buttons) {
        const PadButton source = map[static_cast<std::size_t>(index)];
        const bool waiting = capture_ == index;
        auto* row = list->Emplace<GlyphRow>(
            std::format("buttons.map.{}", player::GameButtonName(index)), name, family,
            waiting ? PadButton::Count : source,
            waiting ? std::string(keyboard ? "Press a key\xE2\x80\xA6" : "Press a button\xE2\x80\xA6")
                    : std::string(source == PadButton::Count ? "None" : ""));
        row->on_activate = [this, index] { StartCapture(index); };
    }

    std::vector<ui::RowSpec> rows;
    if (player::HasRemap(*svc.settings, game_.system)) {
        rows.push_back(ActionRow("buttons.reset", "Default buttons", [this] {
            player::ResetButtons(*Svc().settings, game_.system);
            Svc().player->ReloadButtons();
            Rebuild();
        }));
    }
    const auto add = [&rows](const std::string& what, const std::string& how) {
        rows.push_back(ValueRow(std::format("buttons.info.{}", rows.size()), what, [how] { return how; }));
    };
    const std::string select = PadButtonText(family, PadButton::Select);
    add("Menu", keyboard ? "Esc"
                         : std::format("{}, or hold {} + {}", PadButtonText(family, PadButton::Guide),
                                       PadButtonText(family, PadButton::Start), select));
    add("Fast-forward", keyboard ? "Hold F"
                                 : console_.right_stick_holds
                                       ? "Right stick right"
                                       : std::format("{} + {}", select, PadButtonText(family, PadButton::R1)));
    add("Rewind", keyboard ? "Hold R"
                           : console_.right_stick_holds
                                 ? "Right stick left"
                                 : std::format("{} + {}", select, PadButtonText(family, PadButton::L1)));
    add("Frame rate", "F3");
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

void PlayerScreen::BuildPlayers(ui::Node* list) {
    const input::System& input = *Svc().input;
    std::vector<ui::RowSpec> rows;
    for (int port = 0; port < MAX_PORTS; ++port) {
        const input::PadState& pad = input.Pad(port);
        std::string who = pad.connected ? (pad.name.empty() ? std::string("Pad") : pad.name)
                                        : std::string(port == 0 ? "" : "No pad");
        if (port == 0) {
            who = who.empty() ? "Keyboard" : who + " + keyboard";
        }
        // Choosing a pad's row makes it player 1; the pad that was player 1 takes its place.
        rows.push_back(ValueRow(std::format("players.{}", port + 1), std::format("Player {}", port + 1),
                                [who] { return who; }, [this, port] {
                                    if (port > 0 && Svc().input->Pad(port).connected) {
                                        Svc().input->SetPlayer(port, 0);
                                        Show(View::Players, "players.1");
                                    }
                                }));
    }
    ui::RowSpec note = ValueRow("players.note", "Pads join in the order they connect; choose one to make it player 1",
                                [] { return std::string(); });
    rows.push_back(std::move(note));
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

std::uint32_t PlayerScreen::HeldButtons() const {
    const input::System& input = *Svc().input;
    std::uint32_t held = input.KeyboardPad().buttons;
    for (int port = 0; port < MAX_PORTS; ++port) {
        const input::PadState& pad = input.Pad(port);
        if (!pad.connected) {
            continue;
        }
        held |= pad.buttons;
        held |= pad.lt > TRIGGER_PRESSED ? 1u << static_cast<std::uint32_t>(PadButton::L2) : 0;
        held |= pad.rt > TRIGGER_PRESSED ? 1u << static_cast<std::uint32_t>(PadButton::R2) : 0;
    }
    return held & ~(1u << static_cast<std::uint32_t>(PadButton::Guide));
}

void PlayerScreen::StartCapture(int game_button) {
    capture_ = game_button;
    capture_armed_ = false;
    capture_since_ = pal::NowSeconds();
    Rebuild();
}

void PlayerScreen::TickCapture() {
    const std::uint32_t held = HeldButtons();
    if (capture_ < 0) {
        swallow_nav_ = swallow_nav_ && held != 0;
        return;
    }
    if (view_ != View::Buttons || pal::NowSeconds() - capture_since_ > CAPTURE_SECONDS) {
        capture_ = -1;
        Rebuild();
        return;
    }
    // The press that opened the capture has to end first.
    if (!capture_armed_) {
        capture_armed_ = held == 0;
        return;
    }
    if (held == 0) {
        return;
    }
    int bit = 0;
    while (!((held >> bit) & 1u)) {
        ++bit;
    }
    const auto source = static_cast<PadButton>(bit);
    player::SaveButton(*Svc().settings, game_.system, capture_, source);
    Svc().player->ReloadButtons();
    capture_ = -1;
    swallow_nav_ = true;
    Rebuild();
}

} // namespace den::app
