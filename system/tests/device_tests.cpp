// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/store/settings.h"
#include "den/system/device.h"
#include "den/system/settings.h"

using namespace den;
using system::FormFactor;
using system::Tier;

namespace {

system::DeviceProfile Profile(const char* id) {
    const auto info = system::FakeDevice(id);
    REQUIRE(info);
    return system::Classify(*info);
}

// The tuned core options of every bundled console manifest, keyed as the app keys them.
const std::vector<system::TuningRule>& BundledTuning() {
    static const std::vector<system::TuningRule> rules = [] {
        std::vector<system::TuningRule> out;
        for (const auto& dir : std::filesystem::directory_iterator(DEN_BUNDLED_PLUGINS)) {
            const auto text = ReadTextFile(dir.path() / "plugin.json");
            const auto root = text ? ParseJson(*text) : std::nullopt;
            const JsonValue* components = root ? root->Find("components") : nullptr;
            for (const JsonValue& component : components ? components->items : std::vector<JsonValue>{}) {
                const JsonValue* tuning = component.Find("tuning");
                if (tuning == nullptr) {
                    continue;
                }
                std::string name = component.String("options_name");
                name = name.empty() ? component.String("id") : name;
                std::string key;
                for (const char c : name) {
                    if (std::isalnum(static_cast<unsigned char>(c))) {
                        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                    }
                }
                for (system::TuningRule& rule : system::ParseTuning(*tuning, "core_options." + key + ".")) {
                    out.push_back(std::move(rule));
                }
            }
        }
        return out;
    }();
    return rules;
}

std::vector<system::DeviceDefault> Defaults(const system::DeviceProfile& profile) {
    return system::DeriveDefaults(profile, BundledTuning());
}

const JsonValue* Find(const std::vector<system::DeviceDefault>& defaults, const std::string& key) {
    for (const system::DeviceDefault& item : defaults) {
        if (item.key == key) {
            return &item.value;
        }
    }
    return nullptr;
}

std::string Text(const std::vector<system::DeviceDefault>& defaults, const std::string& key) {
    const JsonValue* value = Find(defaults, key);
    return value ? value->string : std::string("<none>");
}

double Number(const std::vector<system::DeviceDefault>& defaults, const std::string& key) {
    const JsonValue* value = Find(defaults, key);
    return value ? value->number : -1;
}

bool Flag(const std::vector<system::DeviceDefault>& defaults, const std::string& key) {
    const JsonValue* value = Find(defaults, key);
    return value && value->boolean;
}

} // namespace

TEST_CASE("Retroid Pocket Duo: two screens, medium, a clearly bigger interface") {
    const system::DeviceProfile profile = Profile("retroid-pocket-duo");
    CHECK(profile.known);
    CHECK(profile.name == "Retroid Pocket Duo");
    CHECK(profile.form == FormFactor::HandheldDual);
    CHECK(profile.tier == Tier::Mid);

    const auto defaults = Defaults(profile);
    CHECK(Number(defaults, "ui.scale") == Catch::Approx(1.55));
    CHECK(Flag(defaults, "window.fullscreen"));
    CHECK(Flag(defaults, "ui.companion.enabled"));
    CHECK(Text(defaults, "player.dual_screen.mode") == "auto");
    CHECK(Number(defaults, "player.refresh_hz") == 120);
    CHECK(Flag(defaults, "player.rewind"));
    CHECK(Number(defaults, "player.run_ahead") == 0);
    CHECK(Number(defaults, "player.audio_latency_ms") == 96);
    CHECK(Text(defaults, "player.shader_weight") == "medium");
    CHECK(Text(defaults, "core_options.ppsspp.ppsspp_internal_resolution") == "1440x816");
    CHECK(Text(defaults, "core_options.mupen64plusnext.mupen64plus-43screensize") == "960x720");
    CHECK(Text(defaults, "engine.switch2.settings.resolution") == "0.75x");
    CHECK_FALSE(Flag(defaults, "engine.switch2.settings.docked"));
}

TEST_CASE("Retroid Pocket Classic: a small near-square screen keeps room across for the menus") {
    const system::DeviceProfile profile = Profile("retroid-pocket-classic");
    CHECK(profile.name == "Retroid Pocket Classic");
    CHECK(profile.form == FormFactor::HandheldSmall);
    // Its size alone asks for 1.6; at 1240x1080 that leaves 517 units across, so 1.45 (570).
    const double scale = Number(Defaults(profile), "ui.scale");
    CHECK(scale == Catch::Approx(1.45));
    CHECK(720.0 * 1240.0 / 1080.0 / scale >= 560.0);
    CHECK(Number(Defaults(Profile("retroid-pocket-5")), "ui.scale") == Catch::Approx(1.55)); // 16:9: unchanged
}

TEST_CASE("AYN Thor: two screens, high") {
    const system::DeviceProfile profile = Profile("ayn-thor");
    CHECK(profile.name == "AYN Thor");
    CHECK(profile.form == FormFactor::HandheldDual);
    CHECK(profile.tier == Tier::High);

    const auto defaults = Defaults(profile);
    CHECK(Number(defaults, "ui.scale") == Catch::Approx(1.45));
    CHECK(Text(defaults, "player.dual_screen.mode") == "auto");
    CHECK(Number(defaults, "player.run_ahead") == 1);
    CHECK(Text(defaults, "player.shader_weight") == "heavy");
    CHECK(Text(defaults, "core_options.ppsspp.ppsspp_internal_resolution") == "1920x1088");
    CHECK(Text(defaults, "core_options.pcsxrearmed.pcsx_rearmed_neon_enhancement_enable") == "enabled");
    CHECK(Text(defaults, "core_options.dolphinemu.dolphin_efb_scale") == "x2 (1280 x 1056)");
    CHECK(Flag(defaults, "engine.switch2.settings.docked"));
}

TEST_CASE("Legion Go S: a handheld PC found by its family name") {
    const system::DeviceProfile profile = Profile("legion-go-s");
    CHECK(profile.known);
    CHECK(profile.name == "Legion Go S");
    CHECK(profile.form == FormFactor::HandheldSmall);
    CHECK(profile.tier == Tier::Mid);

    const auto defaults = Defaults(profile);
    CHECK(Number(defaults, "ui.scale") == Catch::Approx(1.0));
    CHECK(Flag(defaults, "window.fullscreen"));
    CHECK(Find(defaults, "ui.companion.enabled") == nullptr);
    CHECK(Number(defaults, "player.refresh_hz") == 120);
    CHECK(Number(defaults, "player.audio_latency_ms") == 80);
    CHECK(Text(defaults, "core_options.ppsspp.ppsspp_internal_resolution") == "1440x816");

    // The same handheld with the faster chip is a tier up.
    auto fast = *system::FakeDevice("legion-go-s");
    fast.soc = "AMD Ryzen Z1 Extreme";
    CHECK(system::Classify(fast).tier == Tier::High);
}

TEST_CASE("a desktop PC: windowed, base size, very high by its GPU") {
    const system::DeviceProfile profile = Profile("desktop");
    CHECK_FALSE(profile.known);
    CHECK(profile.form == FormFactor::Desktop);
    CHECK(profile.tier == Tier::VeryHigh);
    CHECK(profile.tier_reason.find("RTX") != std::string::npos);

    const auto defaults = Defaults(profile);
    CHECK(Number(defaults, "ui.scale") == Catch::Approx(1.0));
    CHECK_FALSE(Flag(defaults, "window.fullscreen"));
    CHECK(Number(defaults, "player.refresh_hz") == 144);
    CHECK(Number(defaults, "player.audio_latency_ms") == 48);
    CHECK(Text(defaults, "core_options.ppsspp.ppsspp_internal_resolution") == "2400x1360");
    CHECK(Text(defaults, "core_options.flycast.flycast_internal_resolution") == "1920x1440");
    CHECK(Text(defaults, "engine.switch2.settings.resolution") == "2x");
}

TEST_CASE("other devices fall back on the GPU family and on memory") {
    CHECK(Profile("steam-deck").tier == Tier::Mid);
    CHECK(Profile("steam-deck").form == FormFactor::HandheldSmall);
    CHECK(Profile("tv").form == FormFactor::Tv);
    CHECK(Profile("tablet").form == FormFactor::Tablet);
    CHECK(Profile("tablet").tier == Tier::High);
    CHECK(Profile("rog-ally").tier == Tier::High);
    CHECK(Profile("retroid-pocket-5").tier == Tier::Mid);

    const system::DeviceProfile phone = Profile("phone-low");
    CHECK(phone.tier == Tier::Low);
    const auto defaults = Defaults(phone);
    CHECK_FALSE(Flag(defaults, "player.rewind"));
    CHECK(Text(defaults, "player.shader_weight") == "light");
    CHECK(Text(defaults, "core_options.ppsspp.ppsspp_internal_resolution") == "480x272");

    // No GPU name yet (before the graphics context): memory and cores decide.
    auto unknown = *system::FakeDevice("desktop");
    unknown.gpu.clear();
    CHECK(system::Classify(unknown).tier == Tier::High);
    CHECK(system::Classify(unknown).tier_reason == "memory and processor");

    // The user's tier replaces the detected one and the defaults follow.
    const auto forced = system::Classify(*system::FakeDevice("desktop"), Tier::Low);
    CHECK(forced.tier == Tier::Low);
    CHECK(forced.detected == Tier::VeryHigh);
    CHECK_FALSE(Flag(Defaults(forced), "player.rewind"));
    CHECK(system::DescribeDefaults(forced, BundledTuning()).size() >= 4);
}

TEST_CASE("device defaults sit below the user's choices") {
    const auto dir = std::filesystem::temp_directory_path() / "emuden-device-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        store::Settings settings(dir / "settings.json");
        system::SettingsStore store(&settings, dir / "settings-overrides.json");
        std::vector<std::pair<std::string, JsonValue>> layer;
        for (const auto& item : Defaults(Profile("retroid-pocket-duo"))) {
            layer.emplace_back(item.key, item.value);
        }
        settings.SetDefaults(std::move(layer));

        CHECK(settings.GetNumber("ui.scale", 1.0) == Catch::Approx(1.55));
        CHECK(settings.Has("ui.scale"));
        CHECK_FALSE(settings.Stored("ui.scale"));
        CHECK(store.Source("ui.scale") == system::Layer::Default);
        CHECK(store.Resolve("player.dual_screen.mode").string == "auto");

        settings.SetNumber("ui.scale", 1.15); // the user's change wins
        CHECK(settings.GetNumber("ui.scale", 1.0) == Catch::Approx(1.15));
        CHECK(store.Source("ui.scale") == system::Layer::Global);
        settings.Remove("ui.scale"); // and removing it restores the device's value
        CHECK(settings.GetNumber("ui.scale", 1.0) == Catch::Approx(1.55));
    }
    {
        // Defaults are never written to disk.
        store::Settings settings(dir / "settings.json");
        CHECK_FALSE(settings.Has("ui.scale"));
        CHECK(settings.GetNumber("ui.scale", 1.0) == Catch::Approx(1.0));
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("tuned core options: the first case the device meets") {
    const auto parsed = ParseJson(R"({"opt.a": [{"min_tier": "high", "min_screen_height": 1000, "value": "big"},
                                                {"min_cpu_cores": 8, "value": "many"},
                                                {"min_tier": "warp", "value": "never"},
                                                {"value": "small", "describe": "Small"}]})");
    REQUIRE(parsed);
    const auto rules = system::ParseTuning(*parsed, "core_options.x.");
    REQUIRE(rules.size() == 1);
    CHECK(rules[0].key == "core_options.x.opt_a");
    CHECK(rules[0].cases.size() == 3); // an unknown tier is left out
    auto profile = Profile("desktop");
    CHECK(system::TuneFor(rules[0], profile)->value == "big");
    profile.info.screens = {system::Screen{1280, 720}};
    profile.info.cpu_cores = 4;
    CHECK(system::TuneFor(rules[0], profile)->value == "small");
    const auto lines = system::DescribeDefaults(profile, rules);
    CHECK(std::find(lines.begin(), lines.end(), "Small") != lines.end());
    CHECK(Text(Defaults(Profile("retroid-pocket-duo")), "core_options.dolphinemu.dolphin_efb_scale") == "x1 (640 x 528)");
}
