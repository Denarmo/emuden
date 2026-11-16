// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "den/player/bfi.h"

using namespace den::player;

namespace {

// 'G' for a refresh that shows a new game frame, 'g' for the same frame again, 'B' for black.
std::string Pattern(BfiCadence& cadence, const BfiPlan& plan, int refreshes, double period_scale = 1.0) {
    std::string out;
    for (int i = 0; i < refreshes; ++i) {
        const BfiStep step = cadence.Next(plan, period_scale / plan.refresh_hz);
        out += step.black ? 'B' : step.run > 0 ? 'G' : 'g';
    }
    return out;
}

} // namespace

TEST_CASE("a whole multiple of the game's rate makes a plan") {
    const BfiPlan at120 = PlanBfi(120, 60);
    CHECK(at120.Active());
    CHECK(at120.cycle == 2);
    CHECK(at120.black == 1);

    const BfiPlan at240 = PlanBfi(240, 60);
    CHECK(at240.cycle == 4);
    CHECK(at240.black == 3);
    CHECK(PlanBfi(240, 60, BfiStrength::Half).black == 2);
    CHECK(PlanBfi(180, 60, BfiStrength::Half).black == 1);
    CHECK(PlanBfi(120, 60, BfiStrength::Half).black == 1);

    // NTSC rates and handhelds a little off 60 are taken up by the sound's rate control.
    CHECK(PlanBfi(120, 59.94).Active());
    CHECK(PlanBfi(120.00001f, 59.7275).Active());
    CHECK(PlanBfi(120, 60.0988).Active());
    CHECK(PlanBfi(100, 50).Active());
    CHECK(PlanBfi(60, 30).cycle == 2);
}

TEST_CASE("black frames are refused where they would judder") {
    CHECK(PlanBfi(120, 50).refusal == BfiRefusal::NotMultiple);
    CHECK(PlanBfi(144, 60).refusal == BfiRefusal::NotMultiple);
    CHECK(PlanBfi(60, 60).refusal == BfiRefusal::LowRefresh);
    CHECK(PlanBfi(90, 60).refusal == BfiRefusal::LowRefresh);
    CHECK(PlanBfi(0, 60).refusal == BfiRefusal::Unknown);
    CHECK(PlanBfi(120, 0).refusal == BfiRefusal::Unknown);
    CHECK_FALSE(PlanBfi(120, 50).Active());

    CHECK(BfiReason(PlanBfi(60, 60)) == "Needs a 120 Hz display");
    CHECK(BfiReason(PlanBfi(60, 59.94)) == "Needs a 120 Hz display");
    CHECK(BfiReason(PlanBfi(120, 50)) == "This game runs at 50 fps; needs 100 or 150 Hz");
    CHECK(BfiReason(PlanBfi(144, 60)) == "This game runs at 60 fps; needs 120 or 180 Hz");
    CHECK(BfiReason(PlanBfi(120, 60)).empty());
    CHECK(RateText(59.7275) == "59.7");
    CHECK(BfiReason(PlanBfi(59, 59.7275)) == "Needs a 120 Hz display");
    CHECK(BfiReason(PlanBfi(175, 59.7275)) == "This game runs at 59.7 fps; needs 120 or 180 Hz");
}

TEST_CASE("the display rate asked for keeps a good one and picks the lowest that works") {
    CHECK(PickBfiRefresh(60, 120, {120, 60}) == 120);
    CHECK(PickBfiRefresh(60, 60, {60, 120}) == 120);
    CHECK(PickBfiRefresh(60, 240, {60, 120, 240}) == 240); // already right: not switched
    CHECK(PickBfiRefresh(60, 144, {60, 120, 144}) == 120);
    CHECK(PickBfiRefresh(50, 120, {120, 60}) == 0);
    CHECK(PickBfiRefresh(50, 60, {60, 100, 120}) == 100);
}

TEST_CASE("the cadence shows the game frame then black ones, locked to the refreshes") {
    BfiCadence cadence;
    CHECK(Pattern(cadence, PlanBfi(120, 60), 8) == "GBGBGBGB");
    cadence.Reset();
    CHECK(Pattern(cadence, PlanBfi(240, 60), 8) == "GBBBGBBB");
    cadence.Reset();
    CHECK(Pattern(cadence, PlanBfi(240, 60, BfiStrength::Half), 8) == "GBBgGBBg");
    cadence.Reset();
    CHECK(Pattern(cadence, PlanBfi(180, 60), 6) == "GBBGBB");
    CHECK(cadence.Steady());
}

TEST_CASE("a slow frame made up by quick ones keeps the cadence, a stall keeps the game's speed") {
    const BfiPlan plan = PlanBfi(120, 60);
    BfiCadence cadence;
    std::string out;
    int frames = 0;
    // Emulating takes most of a refresh on the game's frames and nothing on the black ones,
    // as a DS core does: the loop's times swing, the display's do not.
    const double periods[] = {1.0, 1.45, 0.55, 1.45, 0.55, 1.3, 0.7, 1.45, 0.55, 1.0};
    for (const double p : periods) {
        const BfiStep step = cadence.Next(plan, p / 120.0);
        frames += step.run;
        out += step.black ? 'B' : 'G';
    }
    CHECK(out == "GBGBGBGBGB");
    CHECK(frames == 5);

    // A stall of a few refreshes (a load) counts them all, and the next frame comes at once.
    cadence.Reset();
    frames = 0;
    out.clear();
    for (const double p : {1.0, 1.0, 4.0, 1.0, 1.0}) {
        const BfiStep step = cadence.Next(plan, p / 120.0);
        frames += step.run;
        out += step.black ? 'B' : 'G';
    }
    CHECK(out == "GBGBG");
    CHECK(frames == 1 + 2 + 1); // 8 refreshes after the first are 4 frames: 1 + 2 owed + 1
}

TEST_CASE("a loop that runs at half the display's rate shows every frame, not black ones") {
    const BfiPlan plan = PlanBfi(120, 60);
    BfiCadence cadence;
    const std::string pattern = Pattern(cadence, plan, 60, 2.0);
    CHECK(pattern.find('B', 4) == std::string::npos); // once it is seen to be behind
    CHECK(pattern.substr(4).find('g') == std::string::npos);
    CHECK_FALSE(cadence.Steady());
    CHECK(cadence.MeasuredHz() < 70);

    // Back at the display's rate it is steady again.
    Pattern(cadence, plan, 120);
    CHECK(cadence.Steady());
    CHECK(cadence.MeasuredHz() > 110);
}
