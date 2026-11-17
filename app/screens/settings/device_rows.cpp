// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Device: what the app found out about the device it runs on, the performance tier and
// interface size it chose from that (both can be overridden), and the way back to the
// device's own settings.

#include <cmath>
#include <format>
#include <iterator>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/system/device.h"

namespace den::app {

namespace {

constexpr float SCALES[] = {1.0f, 1.15f, 1.3f, 1.45f, 1.6f};
constexpr system::Tier TIERS[] = {system::Tier::Low, system::Tier::Mid, system::Tier::High,
                                  system::Tier::VeryHigh};

std::string Percent(float scale) {
    return std::format("{}%", static_cast<int>(scale * 100.0f + 0.5f));
}

std::string ScreensText() {
    std::string text;
    for (const system::Screen& screen : system::Device().info.screens) {
        text += text.empty() ? "" : " + ";
        text += std::format("{} x {}", screen.width, screen.height);
        if (screen.inches > 0) {
            text += std::format(", {:.1f} in", screen.inches);
        }
        if (screen.refresh_hz > 0) {
            text += std::format(", {:.0f} Hz", screen.refresh_hz);
        }
    }
    return text.empty() ? "Not known" : text;
}

// Full screen follows the setting in effect without storing it as the user's choice.
void FollowFullscreen() {
    Services& svc = Svc();
    const bool wanted = svc.settings->GetBool(system::FULLSCREEN_KEY, false);
    if (pal::GetCaps().windowed && svc.window->Fullscreen() != wanted) {
        svc.window->SetFullscreen(wanted);
    }
}

std::vector<ui::RowSpec> DeviceRows() {
    const system::DeviceProfile& device = system::Device();
    std::vector<ui::RowSpec> rows;

    ui::RowSpec name = ValueRow("device.name", "Device", [] {
        return system::Device().name + (system::DeviceIsFake() ? " (simulated)" : "");
    });
    name.description = system::Label(device.form);
    rows.push_back(std::move(name));

    std::vector<std::string> tiers{TrF("Automatic ({})", system::Label(device.detected))};
    for (const system::Tier tier : TIERS) {
        tiers.emplace_back(system::Label(tier));
    }
    ui::RowSpec tier = ChoiceRow(
        "device.tier", "Performance", std::move(tiers),
        [] {
            const auto chosen = system::TierOverride();
            return chosen ? static_cast<int>(*chosen) + 1 : 0;
        },
        [](int index) {
            system::SetTierOverride(index <= 0 ? std::nullopt
                                               : std::optional<system::Tier>(TIERS[index - 1]));
            RebuildTop();
        });
    tier.description = "Found from: " + device.tier_reason;
    rows.push_back(std::move(tier));

    std::vector<std::string> sizes{TrF("Automatic ({})", Percent(system::DeviceUiScale()))};
    for (const float scale : SCALES) {
        sizes.push_back(Percent(scale));
    }
    ui::RowSpec size = ChoiceRow(
        "device.ui_scale", "UI size", std::move(sizes),
        [] {
            if (!Svc().settings->Stored(system::UI_SCALE_KEY)) {
                return 0;
            }
            int best = 0;
            for (int i = 0; i < static_cast<int>(std::size(SCALES)); ++i) {
                if (std::fabs(SCALES[i] - system::UiScale()) < std::fabs(SCALES[best] - system::UiScale())) {
                    best = i;
                }
            }
            return best + 1;
        },
        [](int index) { system::SetUiScale(index <= 0 ? 0.0f : SCALES[index - 1]); });
    size.description = "Text, tiles and buttons";
    rows.push_back(std::move(size));

    rows.push_back(ValueRow("device.screens", "Screens", ScreensText));
    rows.push_back(ValueRow("device.hardware", "Processor", [] {
        const system::DeviceInfo& info = system::Device().info;
        std::string text = std::format("{} cores, {:.0f} GB", info.cpu_cores, std::ceil(info.ram_mb / 1024.0));
        return info.soc.empty() ? text : info.soc + ", " + text;
    }));
    ui::RowSpec gpu = ValueRow("device.gpu", "Graphics", [] {
        const system::DeviceInfo& info = system::Device().info;
        return info.gpu.empty() ? std::string("Not known") : info.gpu;
    });
    gpu.description = device.info.gpu_api;
    rows.push_back(std::move(gpu));

    rows.push_back(ActionRow(
        "device.reset", "Reset to device defaults",
        [] {
            Svc().stage->Confirm("Reset to device defaults?",
                                 "Interface size, full screen, resolutions, rewind and sound delay go back to "
                                 "what suits " + system::Device().name + ". Games and saves are not touched.",
                                 "Reset", [] {
                                     const int removed = system::ResetToDeviceDefaults();
                                     FollowFullscreen();
                                     Svc().stage->Toast(removed > 0 ? std::format("{} reset", removed == 1 ? "1 setting" : std::format("{} settings", removed))
                                                                    : std::string("Already at the device defaults"));
                                     RebuildTop();
                                 });
        },
        "What EmuDEN chose for this device"));
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(15, "Device", DeviceRows);

} // namespace den::app
