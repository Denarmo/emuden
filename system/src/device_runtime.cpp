// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The running app's device: detected once, classified, and its defaults kept installed in
// the settings' defaults layer.

#include <algorithm>
#include <utility>

#include "den/base/log.h"
#include "den/platform/device.h"
#include "den/platform/pal.h"
#include "den/store/settings.h"
#include "den/system/device.h"

namespace den::system {

namespace {

struct State {
    store::Settings* settings = nullptr;
    DeviceInfo info;
    DeviceProfile profile;
    std::vector<DeviceDefault> defaults;
    std::uint64_t revision = 1;
    bool fake = false;
};

State g_device;

DeviceInfo Detect() {
    const pal::DeviceFacts facts = pal::GetDeviceFacts();
    const pal::Caps& caps = pal::GetCaps();
    DeviceInfo info;
    info.manufacturer = facts.manufacturer;
    info.model = facts.model;
    info.device_name = facts.device_name;
    info.soc = facts.soc;
    info.os = caps.os;
    info.os_version = facts.os_version;
    info.api_level = facts.api_level;
    for (const pal::DisplayFacts& display : facts.displays) {
        Screen screen;
        screen.width = std::max(display.width, display.height);
        screen.height = std::min(display.width, display.height);
        screen.refresh_hz = display.refresh_hz;
        screen.density = display.density;
        screen.inches = display.diagonal_inches;
        info.screens.push_back(screen);
    }
    info.touch = facts.touch || caps.touch;
    info.builtin_pad = facts.builtin_pad;
    info.handheld = caps.handheld;
    info.cpu_cores = facts.cpu_cores;
    info.ram_mb = facts.ram_mb;
    return info;
}

void Apply() {
    State& state = g_device;
    state.profile = Classify(state.info, TierOverride());
    state.defaults = DeriveDefaults(state.profile);
    if (state.settings) {
        std::vector<std::pair<std::string, JsonValue>> layer;
        for (const DeviceDefault& item : state.defaults) {
            layer.emplace_back(item.key, item.value);
        }
        state.settings->SetDefaults(std::move(layer));
    }
    ++state.revision;
    const DeviceProfile& profile = state.profile;
    const Screen screen = profile.info.screens.empty() ? Screen{} : profile.info.screens.front();
    log::Info("device", "{}{} | {} | tier {} ({}) | {} screen(s), {}x{} {:.1f} in {:.0f} Hz | {} cores, {} MB | {}",
              profile.name, state.fake ? " (simulated)" : "", Label(profile.form), Id(profile.tier),
              profile.tier_reason, profile.info.screens.size(), screen.width, screen.height, screen.inches,
              screen.refresh_hz, profile.info.cpu_cores, profile.info.ram_mb,
              profile.info.gpu.empty() ? "GPU not known yet" : profile.info.gpu);
}

} // namespace

void InitDevice(store::Settings& settings) {
    State& state = g_device;
    state = State{};
    state.settings = &settings;
    const std::string fake = pal::GetEnv("EMUDEN_FAKE_DEVICE");
    if (auto info = FakeDevice(fake)) {
        state.info = std::move(*info);
        state.fake = true;
    } else {
        if (!fake.empty()) {
            log::Warn("device", "EMUDEN_FAKE_DEVICE names no device: {}", fake);
        }
        state.info = Detect();
    }
    Apply();
}

void ShutdownDevice() {
    if (g_device.settings) {
        g_device.settings->SetDefaults({});
    }
    g_device.settings = nullptr;
}

void NoteGpu(const std::string& renderer, const std::string& version) {
    State& state = g_device;
    if (state.fake || (state.info.gpu == renderer && state.info.gpu_api == version)) {
        return;
    }
    state.info.gpu = renderer;
    state.info.gpu_api = version;
    Apply();
}

const DeviceProfile& Device() {
    return g_device.profile;
}

bool DeviceIsFake() {
    return g_device.fake;
}

std::uint64_t DeviceRevision() {
    return g_device.revision;
}

std::optional<Tier> TierOverride() {
    return g_device.settings ? TierFromId(g_device.settings->GetString(TIER_KEY, "auto")) : std::nullopt;
}

void SetTierOverride(std::optional<Tier> tier) {
    if (!g_device.settings) {
        return;
    }
    if (tier) {
        g_device.settings->SetString(TIER_KEY, Id(*tier));
    } else {
        g_device.settings->Remove(TIER_KEY);
    }
    Apply();
}

float DeviceUiScale() {
    for (const DeviceDefault& item : g_device.defaults) {
        if (item.key == UI_SCALE_KEY) {
            return static_cast<float>(item.value.number);
        }
    }
    return 1.0f;
}

float UiScale() {
    const double scale = g_device.settings ? g_device.settings->GetNumber(UI_SCALE_KEY, 1.0) : 1.0;
    return std::clamp(static_cast<float>(scale), 0.75f, 2.0f);
}

void SetUiScale(float scale) {
    if (!g_device.settings) {
        return;
    }
    if (scale > 0) {
        g_device.settings->SetNumber(UI_SCALE_KEY, scale);
    } else {
        g_device.settings->Remove(UI_SCALE_KEY);
    }
    ++g_device.revision;
}

int ResetToDeviceDefaults() {
    State& state = g_device;
    if (!state.settings) {
        return 0;
    }
    int removed = 0;
    const auto drop = [&](const std::string& key) {
        if (state.settings->Stored(key)) {
            state.settings->Remove(key);
            ++removed;
        }
    };
    drop(TIER_KEY);
    Apply(); // the override is gone: the defaults are the detected tier's again
    for (const DeviceDefault& item : state.defaults) {
        drop(item.key);
    }
    ++state.revision;
    return removed;
}

} // namespace den::system
