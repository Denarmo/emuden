// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/player/bfi.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace den::player {

namespace {

constexpr int MAX_RUN = 3;            // game frames caught up after a long refresh
constexpr double SMOOTHING = 0.05;    // of the periods per refresh, about a quarter second at 60 Hz
constexpr double UNSTEADY_ABOVE = 1.3; // periods per refresh where the loop is behind the display
constexpr double STEADY_BELOW = 1.1;
// The loop's time against one refresh per call. A frame that took long is usually made up by
// the next ones (the display queues finished frames): only time the loop stays behind counts
// as refreshes it missed, and it fades, so a miss long past does not come back later.
constexpr double BEHIND_LEAK = 0.95;
constexpr double BEHIND_MISSED = 1.5;   // refreshes behind that count as one more
constexpr double STALL_PERIODS = 2.5;   // a call this long counts as the refreshes it took

// A display rate the game needs, as displays are sold: 119.46 Hz for a 59.73 fps game is
// a 120 Hz display.
std::string HzText(double hz) {
    const double tens = std::round(hz / 10.0) * 10.0;
    return std::fabs(hz / tens - 1.0) <= BFI_TOLERANCE ? std::format("{:.0f}", tens) : RateText(hz);
}

} // namespace

BfiPlan PlanBfi(float refresh_hz, double content_fps, BfiStrength strength) {
    BfiPlan plan;
    plan.refresh_hz = refresh_hz;
    plan.content_fps = content_fps;
    if (refresh_hz <= 0 || content_fps <= 0) {
        plan.refusal = BfiRefusal::Unknown;
        return plan;
    }
    const double ratio = refresh_hz / content_fps;
    const double whole = std::round(ratio);
    if (ratio < 2.0 * (1.0 - BFI_TOLERANCE)) {
        plan.refusal = BfiRefusal::LowRefresh;
        return plan;
    }
    if (std::fabs(ratio / whole - 1.0) > BFI_TOLERANCE) {
        plan.refusal = BfiRefusal::NotMultiple;
        return plan;
    }
    plan.refusal = BfiRefusal::None;
    plan.cycle = static_cast<int>(whole);
    plan.black = strength == BfiStrength::Full ? plan.cycle - 1 : std::max(1, plan.cycle / 2);
    return plan;
}

float PickBfiRefresh(double content_fps, float current, const std::vector<float>& available) {
    if (PlanBfi(current, content_fps).Active()) {
        return current;
    }
    std::vector<float> rates = available;
    std::sort(rates.begin(), rates.end());
    for (const float rate : rates) {
        if (PlanBfi(rate, content_fps).Active()) {
            return rate;
        }
    }
    return 0;
}

std::string RateText(double rate) {
    const double whole = std::round(rate);
    // NTSC's 59.94 and its multiples read as the round number; a handheld's 59.73 does not.
    return std::fabs(rate - whole) < std::max(0.1, rate * 0.002) ? std::format("{:.0f}", whole)
                                                                 : std::format("{:.1f}", rate);
}

std::string BfiReason(const BfiPlan& plan) {
    switch (plan.refusal) {
    case BfiRefusal::None:
        return {};
    case BfiRefusal::Unknown:
        return "The display's refresh rate is not known";
    case BfiRefusal::LowRefresh:
        return "Needs a " + HzText(plan.content_fps * 2) + " Hz display";
    case BfiRefusal::NotMultiple: {
        const double below = std::max(2.0, std::floor(plan.refresh_hz / plan.content_fps));
        return std::format("This game runs at {} fps; needs {} or {} Hz", RateText(plan.content_fps),
                           HzText(plan.content_fps * below), HzText(plan.content_fps * (below + 1)));
    }
    }
    return {};
}

void BfiCadence::Reset() {
    phase_ = -1;
    carry_ = 0;
    behind_ = 0;
}

BfiStep BfiCadence::Next(const BfiPlan& plan, double seconds) {
    BfiStep step;
    const double periods = std::max(0.0, seconds * plan.refresh_hz);
    if (measured_hz_ <= 0) {
        periods_ = std::max(periods, 0.5);
    } else {
        periods_ += (periods - periods_) * SMOOTHING;
    }
    measured_hz_ = periods_ > 0 ? plan.refresh_hz / periods_ : 0;
    steady_ = steady_ ? periods_ <= UNSTEADY_ABOVE : periods_ < STEADY_BELOW;
    const int cycle = std::max(1, plan.cycle);
    if (phase_ < 0) {
        phase_ = 0;
        step.run = 1;
        return step;
    }
    int refreshes = 1;
    if (!steady_ || periods >= STALL_PERIODS) {
        // The refreshes the time took: the game keeps its speed on a loop slower than the display.
        refreshes = std::max(1, static_cast<int>(std::lround(periods)));
        behind_ = 0;
    } else {
        behind_ = std::clamp(behind_ * BEHIND_LEAK + periods - 1.0, -BEHIND_MISSED, 2 * BEHIND_MISSED);
        if (behind_ >= BEHIND_MISSED) {
            refreshes = 2;
            behind_ -= 1.0;
        }
    }
    const int elapsed = phase_ + refreshes;
    if (elapsed < cycle) {
        phase_ = elapsed;
        // Black only while the loop keeps up: a slower one would hold it on screen too long.
        step.black = steady_ && phase_ <= plan.black;
        return step;
    }
    // A new game frame, shown at once; refreshes past the cycle's end are owed to the next ones.
    int run = elapsed / cycle;
    carry_ += elapsed % cycle;
    run += carry_ / cycle;
    carry_ %= cycle;
    step.run = std::min(run, MAX_RUN);
    phase_ = 0;
    return step;
}

} // namespace den::player
