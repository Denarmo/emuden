// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Player section of the settings: what den::player reads when a game opens.

#include <algorithm>
#include <iterator>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

constexpr int SPEEDS[] = {2, 3, 4, 8};

ui::RowSpec Switch(const char* key, const char* label, bool fallback, const char* description) {
    ui::RowSpec row = ToggleRow(
        key, label, [key, fallback] { return Svc().settings->GetBool(key, fallback); },
        [key](bool on) { Svc().settings->SetBool(key, on); });
    row.description = description;
    return row;
}

std::vector<ui::RowSpec> PlayerRows() {
    std::vector<ui::RowSpec> rows;
    rows.push_back(Switch("player.resume", "Continue where I left off", true,
                          "A game opens at the moment it was closed"));
    rows.push_back(Switch("player.rewind", "Rewind", true, "Keeps the last moments of play to go back to"));
    rows.push_back(ChoiceRow(
        "player.fast_forward_speed", "Fast-forward speed", {"2x", "3x", "4x", "8x"},
        [] {
            const int speed = static_cast<int>(Svc().settings->GetNumber("player.fast_forward_speed", 3));
            const auto it = std::find(std::begin(SPEEDS), std::end(SPEEDS), speed);
            return it == std::end(SPEEDS) ? 1 : static_cast<int>(it - std::begin(SPEEDS));
        },
        [](int index) { Svc().settings->SetNumber("player.fast_forward_speed", SPEEDS[index]); }));
    rows.push_back(Switch("player.smooth", "Smooth picture", true, "Off shows sharp pixels"));
    {
        std::vector<std::string> names;
        for (const player::PicturePreset& preset : player::PicturePresets()) {
            names.push_back(preset.name);
        }
        ui::RowSpec picture = ChoiceRow(
            "player.picture", "Picture", std::move(names),
            [] {
                const std::string id = Svc().settings->GetString("player.picture", "plain");
                const auto& presets = player::PicturePresets();
                for (std::size_t i = 0; i < presets.size(); ++i) {
                    if (presets[i].id == id) {
                        return static_cast<int>(i);
                    }
                }
                return 0;
            },
            [](int index) {
                Svc().settings->SetString("player.picture",
                                          player::PicturePresets()[static_cast<std::size_t>(index)].id);
            });
        picture.description = "For consoles without a choice of their own in the pause menu";
        rows.push_back(std::move(picture));
    }
    rows.push_back(Switch("player.pad_by_label", "Buttons by their label", true,
                          "A is A on every pad; off uses the place of the button"));
    rows.push_back(Switch("player.show_fps", "Show the frame rate", false, "F3 switches it while playing"));
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(25, "Player", PlayerRows);

} // namespace den::app
