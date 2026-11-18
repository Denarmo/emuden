// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Display and Audio. The text size is applied here: the theme in use is replaced by a copy
// with scaled text sizes, so every widget follows without knowing about it.

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "theme_choice.h"

namespace den::app {

namespace {

constexpr const char* TEXT_SCALE_KEY = "ui.text_scale";
constexpr const char* ORIENTATION_KEY = "ui.orientation";
constexpr const char* VOLUME_KEY = "player.volume"; // 0..1, read by the player when a game opens
constexpr float TEXT_SCALES[] = {0.9f, 1.0f, 1.15f, 1.3f};
constexpr const char* ORIENTATIONS[] = {"auto", "landscape", "portrait"};

ui::Theme g_scaled;
const ui::Theme* g_base = nullptr;
float g_scale = 1.0f;
int g_orientation = -1;

int OrientationIndex(std::string_view value) {
    if (value == "landscape") {
        return 1;
    }
    if (value == "portrait") {
        return 2;
    }
    return 0;
}

void ApplyOrientation() {
    Services& svc = Svc();
    if (!svc.settings || !svc.window || pal::GetCaps().windowed) {
        return;
    }
    const int idx = OrientationIndex(svc.settings->GetString(ORIENTATION_KEY, "auto"));
    if (idx == g_orientation) {
        return;
    }
    g_orientation = idx;
    const pal::Orientation mode = idx == 1 ? pal::Orientation::Landscape
                                           : (idx == 2 ? pal::Orientation::Portrait : pal::Orientation::Auto);
    svc.window->SetOrientation(mode);
}

void ApplyTextScale() {
    Services& svc = Svc();
    if (!svc.settings || !svc.theme) {
        return;
    }
    const float scale =
        std::clamp(static_cast<float>(svc.settings->GetNumber(TEXT_SCALE_KEY, 1.0)), 0.8f, 1.5f);
    const bool replaced = svc.theme != &g_scaled; // SetTheme chose another theme
    if (!replaced && scale == g_scale) {
        return;
    }
    if (replaced) {
        if (scale == 1.0f) {
            return;
        }
        g_base = svc.theme;
    }
    g_scaled = *g_base;
    g_scaled.text_title *= scale;
    g_scaled.text_heading *= scale;
    g_scaled.text_body *= scale;
    g_scaled.text_small *= scale;
    g_scale = scale;
    svc.theme = &g_scaled;
    RebuildTop();
}

std::vector<ui::RowSpec> DisplayRows() {
    std::vector<ui::RowSpec> rows;
    rows.push_back(ThemeRow());

    rows.push_back(ChoiceRow(
        TEXT_SCALE_KEY, "Text size", {"Small", "Normal", "Large", "Extra large"},
        [] {
            const float scale = static_cast<float>(Svc().settings->GetNumber(TEXT_SCALE_KEY, 1.0));
            int best = 1;
            for (int i = 0; i < static_cast<int>(std::size(TEXT_SCALES)); ++i) {
                if (std::fabs(TEXT_SCALES[i] - scale) < std::fabs(TEXT_SCALES[best] - scale)) {
                    best = i;
                }
            }
            return best;
        },
        [](int index) { Svc().settings->SetNumber(TEXT_SCALE_KEY, TEXT_SCALES[index]); }));

    if (pal::GetCaps().windowed) {
        ui::RowSpec fullscreen = ToggleRow(
            "window.fullscreen", "Full screen", [] { return Svc().window->Fullscreen(); },
            [](bool on) { SetFullscreen(on); });
        fullscreen.description = "F11 switches too";
        rows.push_back(std::move(fullscreen));
    } else {
        rows.push_back(ChoiceRow(
            ORIENTATION_KEY, "Orientation", {"Auto", "Landscape", "Portrait"},
            [] { return OrientationIndex(Svc().settings->GetString(ORIENTATION_KEY, "auto")); },
            [](int index) {
                Svc().settings->SetString(ORIENTATION_KEY, ORIENTATIONS[std::clamp(index, 0, 2)]);
            }));
    }
    return rows;
}

std::vector<ui::RowSpec> AudioRows() {
    ui::RowSpec volume = SliderRow(
        VOLUME_KEY, "Game volume", 0.0f, 100.0f, 5.0f,
        [] { return static_cast<float>(Svc().settings->GetNumber(VOLUME_KEY, 1.0)) * 100.0f; },
        [](float percent) { Svc().settings->SetNumber(VOLUME_KEY, percent / 100.0); },
        [](float percent) { return std::format("{}%", static_cast<int>(percent + 0.5f)); });
    volume.description = "How loud games play inside EmuDEN; used from the next game started";
    return {volume};
}

} // namespace

DEN_REGISTER_FRAME(ApplyTextScale);
DEN_REGISTER_FRAME(ApplyOrientation);
DEN_REGISTER_SETTINGS(10, "Display", DisplayRows);
DEN_REGISTER_SETTINGS(20, "Audio", AudioRows);

} // namespace den::app
