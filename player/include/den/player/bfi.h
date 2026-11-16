// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Black frame insertion: at a display rate that is a whole multiple N (2 and more) of the
// game's, each game frame is shown for one refresh and black for the next ones, which makes
// motion look sharper on a sample-and-hold panel. 60 fps at 120 Hz: game, black, game, black.
// Anything that is not a whole multiple would judder, so it is refused with the reason.

#pragma once

#include <string>
#include <vector>

namespace den::player {

/// How many of the refreshes after the game's frame are black: all of them, or half (at
/// least one), which is brighter at 180 Hz and above. At 120 Hz both are one.
enum class BfiStrength { Full, Half };

enum class BfiRefusal {
    None,
    Unknown,     // the display's rate is not known
    LowRefresh,  // under twice the game's rate
    NotMultiple, // not a whole multiple of it (120 Hz for a 50 fps game)
};

struct BfiPlan {
    BfiRefusal refusal = BfiRefusal::Unknown;
    int cycle = 1; // refreshes per game frame
    int black = 0; // black refreshes after the game's frame in each cycle
    float refresh_hz = 0;
    double content_fps = 0;

    bool Active() const {
        return refusal == BfiRefusal::None && cycle >= 2 && black >= 1;
    }
};

/// How far refresh / fps may be from a whole number: what the sound's rate control takes up
/// when the game is shown at the display's pace (59.73 fps at 120 Hz is 0.5 % off).
inline constexpr double BFI_TOLERANCE = 0.012;

BfiPlan PlanBfi(float refresh_hz, double content_fps, BfiStrength strength = BfiStrength::Full);

/// The display rate to ask for: `current` when it already makes a plan, else the lowest of
/// `available` that does (the brightest picture); 0 when none does.
float PickBfiRefresh(double content_fps, float current, const std::vector<float>& available);

/// Why there is no plan, for a settings row: "Needs a 120 Hz display", "This game runs at
/// 50 fps; needs 100 or 150 Hz". Empty for an active plan.
std::string BfiReason(const BfiPlan& plan);

/// A rate as the user reads it: "60", "59.7", "120".
std::string RateText(double rate);

/// What to show on one refresh, following the plan.
struct BfiStep {
    int run = 0;        // game frames to emulate now
    bool black = false; // this refresh is a black one
};

/// The refreshes counted against the time that passed. Main thread; one Next per refresh.
class BfiCadence {
public:
    void Reset();
    /// One refresh, `seconds` after the last. A slow call followed by quick ones is one
    /// refresh each (the display queues finished frames); a loop that stays behind, or a stall
    /// of several periods, counts the refreshes it missed, so the game keeps its speed. The
    /// next game frame then comes at once and the cycle starts again from it.
    BfiStep Next(const BfiPlan& plan, double seconds);
    /// The loop keeps up with the display: about one refresh per call, over the last second.
    bool Steady() const {
        return steady_;
    }
    /// The refreshes the loop actually shows per second (smoothed), 0 before the first.
    double MeasuredHz() const {
        return measured_hz_;
    }

private:
    int phase_ = -1; // refreshes since the game frame shown; -1 before the first
    int carry_ = 0;  // refreshes past a cycle's end, owed to the game
    double behind_ = 0; // refreshes the loop is behind the display, fading
    double periods_ = 1.0;  // smoothed refresh periods per call
    double measured_hz_ = 0;
    bool steady_ = true;
};

} // namespace den::player
