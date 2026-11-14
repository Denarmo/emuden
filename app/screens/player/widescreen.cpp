// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Widescreen: the core options the plugin names for it, kept for the game being played.

#include <algorithm>

#include "den/app/app.h"
#include "den/base/log.h"
#include "player_screen.h"

namespace den::app {

bool PlayerScreen::CanWidescreen() const {
    if (core_.widescreen_options.empty()) {
        return false;
    }
    if (core_.runner != plugins::Runner::Engine) {
        return true;
    }
    // An engine app has the row when its settings have the standard key.
    const std::vector<player::CoreOption> options = Svc().player->Options();
    return std::all_of(core_.widescreen_options.begin(), core_.widescreen_options.end(), [&](const auto& wanted) {
        return std::any_of(options.begin(), options.end(),
                           [&](const player::CoreOption& o) { return o.key == wanted.first; });
    });
}

void PlayerScreen::ReadWidescreen() {
    widescreen_ = false;
    if (!CanWidescreen() || phase_ != Phase::Running) {
        return;
    }
    const std::vector<player::CoreOption> options = Svc().player->Options();
    widescreen_ = std::all_of(core_.widescreen_options.begin(), core_.widescreen_options.end(), [&](const auto& wanted) {
        const auto it = std::find_if(options.begin(), options.end(),
                                     [&](const player::CoreOption& o) { return o.key == wanted.first; });
        return it != options.end() && it->value == wanted.second;
    });
}

void PlayerScreen::SetWidescreen(bool on) {
    if (!CanWidescreen() || phase_ != Phase::Running || on == widescreen_) {
        return;
    }
    const std::vector<player::CoreOption> options = Svc().player->Options();
    for (const auto& [key, value] : core_.widescreen_options) {
        const auto it = std::find_if(options.begin(), options.end(),
                                     [&](const player::CoreOption& o) { return o.key == key; });
        if (it == options.end()) {
            continue;
        }
        Svc().player->SetOption(key, on ? value : it->default_value, player::OptionScope::Game);
    }
    widescreen_ = on;
    if (core_.widescreen_restart) {
        Restart();
    }
}

void PlayerScreen::Restart() {
    if (phase_ != Phase::Running || content_.empty()) {
        return;
    }
    log::Info("player", "restart {} for its core options", game_.name);
    Svc().player->Close();
    phase_ = Phase::Preparing;
    Begin(std::filesystem::path(content_), false);
    if (phase_ == Phase::Running) {
        if (view_ != View::Closed) {
            Svc().player->Pause(); // as the open menu had it
        }
        Rebuild();
    }
}

} // namespace den::app
