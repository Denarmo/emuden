// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Player section of the settings: what den::player reads when a game opens.

#include <algorithm>
#include <iterator>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "black_frames.h"
#include "dual_screen.h"
#include "picture_choice.h"

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
        const std::vector<std::string> ids = PictureIds();
        std::vector<std::string> names;
        for (const std::string& id : ids) {
            names.push_back(PictureName(id));
        }
        ui::RowSpec picture = ChoiceRow(
            "player.picture", "Picture", std::move(names),
            [ids] {
                const std::string id = Svc().settings->GetString(PICTURE_KEY.name(), PICTURE_KEY.fallback());
                const auto it = std::find(ids.begin(), ids.end(), id);
                return it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
            },
            [ids](int index) {
                Svc().settings->SetString(PICTURE_KEY.name(), ids[static_cast<std::size_t>(index)]);
            });
        picture.description = "Auto gives every console its own; a console or a game can have another in the pause menu";
        rows.push_back(std::move(picture));
    }
    for (ui::RowSpec& row : DualScreenSettingRows()) {
        rows.push_back(std::move(row));
    }
    {
        ui::RowSpec bfi = Switch(BFI_KEY.name().c_str(), BFI_LABEL, false,
                                 "Sharper motion at 120 Hz and above, on this device only; a console or a game "
                                 "can have its own in its settings. It flickers");
        rows.push_back(std::move(bfi));
        ui::RowSpec strength = ChoiceRow(
            BFI_STRENGTH_KEY.name(), "Black frames", {"Every refresh between", "Half of them, brighter"},
            [] { return Svc().settings->GetString(BFI_STRENGTH_KEY.name(), "full") == "half" ? 1 : 0; },
            [](int index) { Svc().settings->SetString(BFI_STRENGTH_KEY.name(), index == 1 ? "half" : "full"); });
        strength.description = "At 180 Hz and above, how many refreshes between the game's frames are black";
        rows.push_back(std::move(strength));
    }
    rows.push_back(Switch("player.overlay_on_second_screen", "Pause menu on the second screen", true,
                          "With two displays the menu opens beside the game, not over it"));
    rows.push_back(Switch("player.swap_displays", "Game on the second screen", false,
                          "With two displays the game and the quick settings change places"));
    rows.push_back(Switch("player.pad_by_label", "Buttons by their label", true,
                          "A is A on every pad; off uses the place of the button"));
    rows.push_back(ChoiceRow(
        "player.fps_overlay", "Frame rate", {"Off", "On", "With a graph"},
        [] {
            const auto& settings = *Svc().settings;
            return std::clamp(static_cast<int>(settings.GetNumber(
                                  "player.fps_overlay", settings.GetBool("player.show_fps", false) ? 1 : 0)),
                              0, 2);
        },
        [](int index) { Svc().settings->SetNumber("player.fps_overlay", index); }));
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(25, "Player", PlayerRows);

} // namespace den::app
