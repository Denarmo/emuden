// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Pages of the pause menu: core options (for the console or for this game) and the players.
// The buttons are in player_shortcuts.cpp.

#include <algorithm>
#include <format>

#include "den/app/pages.h"
#include "player_screen.h"

namespace den::app {

namespace {

using input::PadFamily;

constexpr float ROW_HEIGHT = 48;
constexpr int MAX_PORTS = 4;

} // namespace

PadFamily PlayerScreen::PadFamilyShown() const {
    return Svc().input->ActiveFamily();
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

} // namespace den::app
