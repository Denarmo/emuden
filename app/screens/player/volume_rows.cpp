// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <format>

#include "../apps/beside.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/platform/audio.h"
#include "den/platform/second_display.h"
#include "volume_rows.h"

namespace den::app {

namespace {

constexpr const char* VOLUME_KEY = "player.volume";

std::string Percent(float value, float top) {
    return std::format("{}%", static_cast<int>(std::lround(top > 0 ? value * 100.0f / top : 0.0f)));
}

// The system's volume is asked for through the platform: not on every frame a row is drawn.
// The hardware keys change it too, so it is read again four times a second.
double g_read_at = -1;
int g_step = 0;

float SystemStep() {
    const double now = pal::NowSeconds();
    if (g_read_at < 0 || now - g_read_at > 0.25) {
        g_read_at = now;
        g_step = pal::audio::SystemVolume();
    }
    return static_cast<float>(g_step);
}

} // namespace

std::vector<ui::RowSpec> VolumeRows(const std::string& prefix, bool other_app) {
    // The device's own panel (Retroid Pocket Duo, AYN Thor) sets each screen's volume and
    // brightness, which this app may not: a button opens it instead of sliders that could
    // only set part of it.
    if (pal::audio::HasScreenControls()) {
        ui::RowSpec panel = ActionRow(prefix + ".screen_controls", "Volume and brightness",
                                      [] { pal::audio::OpenScreenControls(); });
        panel.description = "Each screen's, in the device's dual-screen panel";
        return {std::move(panel)};
    }
    const bool system = pal::audio::HasSystemVolume();
    const bool two = pal::GetSecondDisplay().Present();
    const system::AppBeside& beside = Beside();
    std::vector<ui::RowSpec> rows;

    ui::RowSpec game = SliderRow(
        prefix + ".volume", "Game", 0, 100, 5,
        [] { return static_cast<float>(std::clamp(Svc().settings->GetNumber(VOLUME_KEY, 1.0), 0.0, 1.0) * 100.0); },
        [](float percent) {
            const double volume = std::clamp(percent, 0.0f, 100.0f) / 100.0;
            Svc().settings->SetNumber(VOLUME_KEY, volume);
            Svc().player->SetVolume(static_cast<float>(volume));
        },
        [](float percent) { return Percent(percent, 100); });
    if (two) {
        game.description = ScreenName(beside.GameScreen());
    }
    if (system) {
        game.description += (game.description.empty() ? "" : "  \xC2\xB7  ") +
                            std::string("The game's share of the system volume");
    }
    rows.push_back(std::move(game));

    if (other_app && system && pal::audio::SystemVolumeSteps() > 0) {
        const float steps = static_cast<float>(pal::audio::SystemVolumeSteps());
        ui::RowSpec other = SliderRow(
            prefix + ".volume_other", "Other app", 0, steps, 1, [] { return SystemStep(); },
            [](float step) {
                g_step = static_cast<int>(std::lround(step));
                g_read_at = pal::NowSeconds(); // shown at once; the system's answer follows
                pal::audio::SetSystemVolume(g_step);
            },
            [steps](float step) { return Percent(step, steps); });
        other.description = (two ? ScreenName(beside.SettingsScreen()) + "  \xC2\xB7  " : std::string()) +
                            "The system volume";
        rows.push_back(std::move(other));
    }
    return rows;
}

} // namespace den::app
