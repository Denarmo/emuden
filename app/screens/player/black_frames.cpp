// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/app/black_frames.h"

#include <cmath>
#include <format>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/base/log.h"
#include "den/store/settings.h"
#include "den/app/game_prefs.h"

namespace den::app {

const system::Key<bool> BFI_KEY{"player.bfi", false,
                                {.label = BFI_LABEL,
                                 .section = "Player",
                                 .per_console = true,
                                 .per_game = true,
                                 .device_only = true}};
const system::Key<std::string> BFI_STRENGTH_KEY{
    "player.bfi_strength", "full",
    {.label = "Black frames", .section = "Player", .options = {"full", "half"}, .device_only = true}};

namespace {

constexpr const char* WARNED_KEY = "player.bfi_warned";
constexpr const char* WARNING =
    "Black frame insertion flickers on purpose. If flicker bothers you, turn it off.";
constexpr double RATES_EVERY = 1.0; // seconds between asking the display while it matters
constexpr double MISSES_BELOW = 0.97; // of the display's rate drawn: refreshes are being missed

player::BfiStrength Strength() {
    return Svc().settings->GetString(BFI_STRENGTH_KEY.name(), BFI_STRENGTH_KEY.fallback()) == "half"
               ? player::BfiStrength::Half
               : player::BfiStrength::Full;
}

} // namespace

ui::RowSpec BfiConsoleRow(const std::string& system) {
    ui::RowSpec row = ToggleRow(
        "console.bfi", BFI_LABEL,
        [system] { return system::Store().Get(BFI_KEY, system::Scope::Console(system)); },
        [system](bool on) { system::Store().Set(BFI_KEY, on, system::Scope::Console(system)); });
    row.description = "On this device only. Needs a display at a whole multiple of the game's rate, such as 120 Hz";
    return row;
}

void BlackFrames::Start(const library::Entry& game) {
    game_ = game;
    started_ = true;
    on_ = system::HasStore() && system::Store().Get(BFI_KEY, ScopeOf(game));
    rates_at_ = -100;
}

void BlackFrames::ReadRates() {
    const double now = pal::NowSeconds();
    if (now - rates_at_ >= RATES_EVERY && Svc().window) {
        rates_ = pal::MainRefreshRates(*Svc().window);
        rates_at_ = now;
    }
}

void BlackFrames::Update(const player::Snapshot& snapshot, bool show, bool main_display) {
    Services& svc = Svc();
    main_display_ = main_display;
    const bool want = started_ && on_ && !snapshot.external && main_display;
    if (want || requested_ > 0) {
        ReadRates();
    }
    if (want) {
        // A rate the game fits, asked for even when the display runs at it already, so that
        // it stays there.
        const float pick = player::PickBfiRefresh(snapshot.core_fps, rates_.current, rates_.available);
        if (pick > 0 && std::fabs(pick - requested_) >= 0.5f) {
            log::Info("player", "black frames: asks the display for {:.2f} Hz (runs at {:.2f})", pick,
                      rates_.current);
            pal::RequestMainRefresh(*svc.window, pick);
            requested_ = pick;
            rates_at_ = -100; // read again on the next frame
        }
    } else if (requested_ > 0) {
        pal::RequestMainRefresh(*svc.window, 0);
        requested_ = 0;
    }
    player::BfiRequest request;
    request.on = want;
    request.hold = !show || snapshot.fast_forward || snapshot.rewinding;
    request.refresh_hz = rates_.current;
    request.strength = Strength();
    svc.player->SetBfi(request);
}

void BlackFrames::Stop() {
    if (requested_ > 0 && Svc().window) {
        pal::RequestMainRefresh(*Svc().window, 0);
    }
    requested_ = 0;
    started_ = false;
    if (Svc().player) {
        Svc().player->SetBfi({});
    }
}

void BlackFrames::SetOn(bool on) {
    on_ = on;
    if (system::HasStore()) {
        const system::Scope game = ScopeOf(game_);
        system::Store().Set(BFI_KEY, on, system::Store().IsSet(BFI_KEY.name(), game)
                                             ? game
                                             : system::Scope::Console(game_.system));
    }
    if (on && !Svc().settings->GetBool(WARNED_KEY, false)) {
        Svc().settings->SetBool(WARNED_KEY, true);
        Svc().stage->Toast(WARNING, 6.0f);
    }
}

std::string BlackFrames::Refusal(const player::Snapshot& snapshot) const {
    if (snapshot.external) {
        return "Not with this engine";
    }
    if (!main_display_) {
        return "Only on the main display";
    }
    // What the display could be asked for, or what it runs at when it offers nothing better.
    pal::RefreshRates rates = rates_;
    if (rates_at_ < 0 && Svc().window) {
        rates = pal::MainRefreshRates(*Svc().window);
    }
    const float pick = player::PickBfiRefresh(snapshot.core_fps, rates.current, rates.available);
    const float best = pick > 0 ? pick : rates.current;
    return player::BfiReason(player::PlanBfi(best, snapshot.core_fps));
}

std::string BlackFrames::Status(const player::Snapshot& snapshot) const {
    if (const std::string refused = Refusal(snapshot); !refused.empty()) {
        return refused;
    }
    if (!on_) {
        return "Sharper motion, a darker picture. It flickers";
    }
    const player::BfiPlan& plan = snapshot.bfi;
    if (!plan.Active()) {
        return std::format("Switching the display to {} Hz", player::RateText(requested_));
    }
    if (snapshot.fast_forward || snapshot.rewinding) {
        return "Paused while fast-forwarding or rewinding";
    }
    if (!snapshot.bfi_steady && snapshot.bfi_display_hz > 0) {
        return std::format("The display shows only {} frames a second", player::RateText(snapshot.bfi_display_hz));
    }
    if (snapshot.bfi_display_hz > 0 && snapshot.bfi_display_hz < plan.refresh_hz * MISSES_BELOW) {
        // The game is too heavy to draw every refresh: a missed one shows no black frame.
        return std::format("Uneven: {} of {} refreshes a second are drawn", player::RateText(snapshot.bfi_display_hz),
                           player::RateText(plan.refresh_hz));
    }
    return std::format("At {} Hz: {} black frame{} after each of the game's", player::RateText(plan.refresh_hz),
                       plan.black, plan.black == 1 ? "" : "s");
}

} // namespace den::app
