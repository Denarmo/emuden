// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Pacing beside Advance's own: the frame rate measured, and black frame insertion, which
// emulates one game frame per cycle of refreshes and has the screen draw the others black.

#include "session_impl.h"

namespace den::player {

void PlayerSession::MeasureFps(double seconds) {
    fps_window_ += seconds;
    if (fps_window_ >= 0.5) {
        snapshot_.measured_fps = fps_frames_ / fps_window_;
        fps_window_ = 0.0;
        fps_frames_ = 0;
    }
}

void PlayerSession::SetBfi(const BfiRequest& request) {
    bfi_ = request;
    if (!request.on || request.hold) {
        snapshot_.bfi_black = false;
    }
}

void PlayerSession::PlanBfiFrame() {
    snapshot_.bfi_black = false;
    const BfiPlan plan = bfi_.on && core_ ? PlanBfi(bfi_.refresh_hz, av_.fps, bfi_.strength) : BfiPlan{};
    // A new cycle (another display rate, or the game's) starts at a game frame.
    if (!bfi_ran_ || plan.cycle != snapshot_.bfi.cycle || plan.black != snapshot_.bfi.black) {
        bfi_cadence_.Reset();
    }
    snapshot_.bfi = plan;
    bfi_ran_ = false;
}

bool PlayerSession::AdvanceBfi(double seconds) {
    if (!snapshot_.bfi.Active() || bfi_.hold) {
        return false;
    }
    const BfiStep step = bfi_cadence_.Next(snapshot_.bfi, seconds);
    bfi_ran_ = true;
    snapshot_.bfi_black = step.black;
    snapshot_.bfi_steady = bfi_cadence_.Steady();
    snapshot_.bfi_display_hz = bfi_cadence_.MeasuredHz();
    // The display's pace, as when it runs at the game's rate: the sound's rate control takes
    // up the small difference.
    due_ = 0.0;
    RunForward(step.run, true);
    return true;
}

} // namespace den::player
