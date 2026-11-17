// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The device profile: what the app runs on, what kind of device that is and how fast, and
// the settings that suit it. The settings are a defaults layer below the user's own choices
// (store::Settings::SetDefaults): a stored value always wins, removing it brings the device's
// value back.
//
//     system::InitDevice(settings);              // at startup, before the window
//     system::NoteGpu(renderer, version);        // once the GL context exists
//     system::Device().tier, system::UiScale()
//
// EMUDEN_FAKE_DEVICE=<id> (retroid-pocket-duo, ayn-thor, legion-go-s, steam-deck, rog-ally,
// retroid-pocket-5, retroid-pocket-classic, desktop, tv, tablet, phone-low) stands in for the
// real device.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/json.h"

namespace den::store {
class Settings;
}

namespace den::system {

enum class FormFactor { HandheldSmall, HandheldDual, Tablet, Desktop, Tv };
enum class Tier { Low, Mid, High, VeryHigh };

struct Screen {
    int width = 0, height = 0; // pixels, landscape
    float refresh_hz = 0;      // 0 when unknown
    float density = 1.0f;
    float inches = 0;          // the diagonal; 0 when unknown
};

/// The facts, from the platform (or a test).
struct DeviceInfo {
    std::string manufacturer, model, soc;
    std::string device_name; // the name its owner gave it ("DENA-PC"); may be empty
    std::string os; // "windows", "linux", "android", "macos"
    std::string os_version;
    int api_level = 0;           // Android SDK level
    std::vector<Screen> screens; // the main one first
    bool touch = false;
    bool builtin_pad = false;
    bool handheld = false; // the system says so (Android, a handheld's game mode)
    int cpu_cores = 0;
    int ram_mb = 0;
    std::string gpu;     // the renderer's name; empty until the graphics context exists
    std::string gpu_api; // "OpenGL ES 3.2 ...", "4.6.0 ..."
};

struct DeviceProfile {
    DeviceInfo info;
    std::string name; // "Retroid Pocket Duo"; maker and model when the device is not in the table
    bool known = false;
    FormFactor form = FormFactor::Desktop;
    Tier tier = Tier::Mid;     // in effect (the user's override when there is one)
    Tier detected = Tier::Mid; // what the device was classified as
    std::string tier_reason;   // "known device", "Adreno 740", "memory and processor"
};

/// One setting chosen for the device.
struct DeviceDefault {
    std::string key;
    JsonValue value;
};

const char* Label(FormFactor form); // "Handheld with two screens"
const char* Label(Tier tier);       // "Very high"
const char* Id(Tier tier);          // "very_high"
std::optional<Tier> TierFromId(std::string_view id);

/// Form factor and tier from the facts; `override_tier` replaces the detected tier.
DeviceProfile Classify(DeviceInfo info, std::optional<Tier> override_tier = std::nullopt);
/// One value of a tuned setting: it is chosen when the device meets every bound it gives.
struct TuningCase {
    std::optional<Tier> min_tier;
    int min_screen_height = 0; // of the main screen, landscape
    int min_cpu_cores = 0;
    std::string value;
    std::string describe; // a few words for the setup page ("PSP 3x"); empty: not shown
    bool operator==(const TuningCase&) const = default;
};

/// A setting chosen by device: a core option a console plugin tunes (its components'
/// "tuning"). The first case the device meets wins; none: no default.
struct TuningRule {
    std::string key; // "core_options.ppsspp.ppsspp_internal_resolution"
    std::vector<TuningCase> cases;
    bool operator==(const TuningRule&) const = default;
};

/// A component's tuning object, {"<option>": [{"min_tier": "mid", "min_screen_height": 605,
/// "min_cpu_cores": 4, "value": "...", "describe": "..."}, ...]}; each key is `key_prefix`
/// and the option with dots as underscores. Cases without a value are left out.
std::vector<TuningRule> ParseTuning(const JsonValue& tuning, const std::string& key_prefix);
/// The case of the rule the device meets first; null when none.
const TuningCase* TuneFor(const TuningRule& rule, const DeviceProfile& profile);

/// The settings that suit the profile, with the plugins' tuned core options.
std::vector<DeviceDefault> DeriveDefaults(const DeviceProfile& profile, const std::vector<TuningRule>& tuning = {});
/// What was chosen, as short lines for the setup page ("Interface size: 145%").
std::vector<std::string> DescribeDefaults(const DeviceProfile& profile, const std::vector<TuningRule>& tuning = {});
/// A made-up device by id, for tests and EMUDEN_FAKE_DEVICE.
std::optional<DeviceInfo> FakeDevice(std::string_view id);

// The running app's device. Main thread.

/// Detects the device, classifies it and installs its defaults in `settings` (which outlives
/// the app's use of this module). Call once at startup, before settings are read.
void InitDevice(store::Settings& settings);
void ShutdownDevice();
/// The tuned core options of the installed console plugins; the defaults are derived again.
void SetTuning(std::vector<TuningRule> rules);
const std::vector<TuningRule>& Tuning();
/// The graphics context exists: the GPU's name refines the tier and the defaults.
void NoteGpu(const std::string& renderer, const std::string& version);
const DeviceProfile& Device();
/// Whether EMUDEN_FAKE_DEVICE replaced the real device.
bool DeviceIsFake();
/// Grows whenever the profile or its defaults changed.
std::uint64_t DeviceRevision();

/// "device.tier": "auto" or a tier id. Setting it derives the defaults again.
std::optional<Tier> TierOverride();
void SetTierOverride(std::optional<Tier> tier);

/// "ui.scale": how much bigger than the base layout everything is drawn (1.0 = a window 720
/// units high). The device's value unless the user chose one.
float UiScale();
float DeviceUiScale();
/// 0 or less returns to the device's value.
void SetUiScale(float scale);

/// Removes the user's own value of every setting the device has a default for, and the tier
/// override. Returns how many values were removed.
int ResetToDeviceDefaults();

inline constexpr const char* UI_SCALE_KEY = "ui.scale";
inline constexpr const char* TIER_KEY = "device.tier";
inline constexpr const char* FULLSCREEN_KEY = "window.fullscreen";
inline constexpr const char* COMPANION_KEY = "ui.companion.enabled";
inline constexpr const char* DUAL_SCREEN_KEY = "player.dual_screen.mode";
inline constexpr const char* SHADER_WEIGHT_KEY = "player.shader_weight";  // light, medium, heavy
inline constexpr const char* VSYNC_KEY = "player.vsync";
inline constexpr const char* REFRESH_KEY = "player.refresh_hz";
inline constexpr const char* REWIND_KEY = "player.rewind";
inline constexpr const char* RUN_AHEAD_KEY = "player.run_ahead";          // frames; 0 = off
inline constexpr const char* AUDIO_LATENCY_KEY = "player.audio_latency_ms";

} // namespace den::system
