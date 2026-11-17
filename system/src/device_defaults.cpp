// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// From a device profile to the settings that suit it. Keys are the ones the app, the player
// and the plugin host already read from settings.json; a core option is
// "core_options.<core name, letters and digits>.<option key>", as the player stores it.

#include <algorithm>
#include <cmath>
#include <format>

#include "den/system/device.h"

namespace den::system {

namespace {

struct Plan {
    float ui_scale = 1.0f;
    bool fullscreen = false;
    bool two_screens = false;
    const char* shader_weight = "medium";
    int refresh_hz = 60;
    bool rewind = true;
    int run_ahead = 0;
    int audio_latency_ms = 80;
    const char* engine_resolution = "1x";
    bool engine_docked = true;
};

Screen MainScreen(const DeviceProfile& profile) {
    Screen screen = profile.info.screens.empty() ? Screen{} : profile.info.screens.front();
    if (screen.width <= 0 || screen.height <= 0) {
        screen.width = 1920;
        screen.height = 1080;
    }
    if (screen.width < screen.height) {
        std::swap(screen.width, screen.height);
    }
    return screen;
}

// Text and tiles sized for a small screen at arm's length: the base layout is drawn for a
// screen about 4.2 inches high, and a smaller one gets everything proportionally bigger.
float UiScaleFor(const DeviceProfile& profile, const Screen& screen) {
    switch (profile.form) {
    case FormFactor::Desktop:
        return 1.0f;
    case FormFactor::Tv:
        return 1.15f;
    default:
        break;
    }
    if (screen.inches <= 0) {
        return profile.info.os == "android" ? 1.4f : 1.15f;
    }
    const float height_inches =
        screen.inches * static_cast<float>(screen.height) /
        std::hypot(static_cast<float>(screen.width), static_cast<float>(screen.height));
    const float scale = std::round(4.2f / height_inches * 20.0f) / 20.0f;
    // A near-square screen (1240x1080) would get so few units across that the menus crowd:
    // the default leaves them at least MIN_DEFAULT_UNITS wide. The user may still go bigger.
    constexpr float LAYOUT_HEIGHT = 720.0f, MIN_DEFAULT_UNITS = 560.0f;
    const float aspect = static_cast<float>(screen.width) / static_cast<float>(screen.height);
    const float widest = std::floor(aspect * LAYOUT_HEIGHT / MIN_DEFAULT_UNITS * 20.0f) / 20.0f;
    return std::clamp(std::min(scale, widest), 1.0f, 1.6f);
}

int ByTier(Tier tier, int low, int mid, int high, int very_high) {
    switch (tier) {
    case Tier::Low:
        return low;
    case Tier::Mid:
        return mid;
    case Tier::High:
        return high;
    default:
        return very_high;
    }
}

Plan MakePlan(const DeviceProfile& profile) {
    const Screen screen = MainScreen(profile);
    const Tier tier = profile.tier;
    const bool android = profile.info.os == "android";
    const bool handheld = profile.form == FormFactor::HandheldSmall || profile.form == FormFactor::HandheldDual ||
                          profile.form == FormFactor::Tablet;
    Plan plan;
    plan.ui_scale = UiScaleFor(profile, screen);
    plan.fullscreen = profile.form != FormFactor::Desktop;
    plan.two_screens = profile.form == FormFactor::HandheldDual && profile.info.screens.size() >= 2;
    plan.shader_weight = tier == Tier::Low ? "light" : tier == Tier::Mid ? "medium" : "heavy";
    plan.refresh_hz = screen.refresh_hz >= 30 ? static_cast<int>(screen.refresh_hz + 0.5f) : 60;
    plan.rewind = tier >= Tier::Mid;
    plan.run_ahead = tier >= Tier::High ? 1 : 0;
    // Android's sound path is longer and less even than a desktop's.
    plan.audio_latency_ms = ByTier(tier, 112, 80, 64, 48) + (android ? 16 : 0);

    plan.engine_docked = !handheld || tier >= Tier::High;
    plan.engine_resolution = tier == Tier::Low    ? "0.5x"
                             : tier == Tier::Mid  ? (handheld ? "0.75x" : "1x")
                             : tier == Tier::High ? "1x"
                             : screen.height >= 1440 ? "2x" : "1x";
    return plan;
}

std::string Percent(float scale) {
    return std::format("{}%", static_cast<int>(scale * 100.0f + 0.5f));
}

} // namespace

std::vector<DeviceDefault> DeriveDefaults(const DeviceProfile& profile, const std::vector<TuningRule>& tuning) {
    const Plan plan = MakePlan(profile);
    std::vector<DeviceDefault> out;
    const auto text = [&](std::string key, const std::string& value) {
        out.push_back({std::move(key), JsonString(value)});
    };
    const auto number = [&](std::string key, double value) { out.push_back({std::move(key), JsonNumber(value)}); };
    const auto flag = [&](std::string key, bool value) { out.push_back({std::move(key), JsonBool(value)}); };

    number(UI_SCALE_KEY, plan.ui_scale);
    flag(FULLSCREEN_KEY, plan.fullscreen);
    if (plan.two_screens) {
        // The layout itself belongs to the dual-screen code; these only say "use both".
        flag(COMPANION_KEY, true);
        text(DUAL_SCREEN_KEY, "auto");
    }

    text(SHADER_WEIGHT_KEY, plan.shader_weight);
    flag(VSYNC_KEY, true);
    number(REFRESH_KEY, plan.refresh_hz);
    flag(REWIND_KEY, plan.rewind);
    number(RUN_AHEAD_KEY, plan.run_ahead);
    number(AUDIO_LATENCY_KEY, plan.audio_latency_ms);

    // The console plugins' tuned core options (internal resolutions and the like); the core
    // that plays each console is the manifest's default_core.
    for (const TuningRule& rule : tuning) {
        if (const TuningCase* chosen = TuneFor(rule, profile)) {
            text(rule.key, chosen->value);
        }
    }

    // Passed to the Switch 2 engine once it takes settings (contract level 2); level 0 ignores them.
    text("engine.switch2.settings.resolution", plan.engine_resolution);
    flag("engine.switch2.settings.docked", plan.engine_docked);
    number("engine.switch2.settings.frame_limit", plan.refresh_hz);
    return out;
}

std::vector<std::string> DescribeDefaults(const DeviceProfile& profile, const std::vector<TuningRule>& tuning) {
    const Plan plan = MakePlan(profile);
    std::vector<std::string> lines;
    lines.push_back(std::format("{}, {} performance", Label(profile.form),
                                profile.tier == Tier::Mid ? "medium" : profile.tier == Tier::Low ? "low"
                                : profile.tier == Tier::High ? "high" : "very high"));
    lines.push_back(std::format("Interface size {}, {}", Percent(plan.ui_scale),
                                plan.fullscreen ? "full screen" : "in a window"));
    if (plan.two_screens) {
        lines.emplace_back("Both screens: two-screen games and details on the second one");
    }
    std::string tuned;
    for (const TuningRule& rule : tuning) {
        const TuningCase* chosen = TuneFor(rule, profile);
        if (chosen != nullptr && !chosen->describe.empty()) {
            tuned += (tuned.empty() ? "" : ", ") + chosen->describe;
        }
    }
    if (!tuned.empty()) {
        lines.push_back(tuned);
    }
    lines.push_back(std::format("{} Hz screen, rewind {}, run-ahead {}, sound delay {} ms", plan.refresh_hz,
                                plan.rewind ? "on" : "off", plan.run_ahead > 0 ? "on" : "off",
                                plan.audio_latency_ms));
    lines.push_back(std::format("Picture effects: {}; Switch 2 at {} {}", plan.shader_weight, plan.engine_resolution,
                                plan.engine_docked ? "docked" : "handheld"));
    return lines;
}

} // namespace den::system
