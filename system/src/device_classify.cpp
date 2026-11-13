// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// From the facts to a form factor and a performance tier.

#include <algorithm>
#include <cctype>

#include "device_table.h"

namespace den::system {

namespace {

std::string Lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

FormFactor FormOf(const DeviceInfo& info) {
    const Screen main = info.screens.empty() ? Screen{} : info.screens.front();
    if (info.os == "android") {
        // A second screen the size of a hand is part of the device; a big one is a TV.
        if (info.screens.size() >= 2) {
            const Screen& second = info.screens[1];
            if (second.inches > 0 ? second.inches <= 7.5f : info.builtin_pad) {
                return FormFactor::HandheldDual;
            }
            return FormFactor::Tv;
        }
        if (info.builtin_pad) {
            return FormFactor::HandheldSmall;
        }
        if (!info.touch) {
            return FormFactor::Tv;
        }
        return main.inches >= 7.5f ? FormFactor::Tablet : FormFactor::HandheldSmall;
    }
    if (info.handheld) {
        // A handheld's game mode on a big screen is the handheld docked, or a living-room PC.
        return main.inches > 13.0f ? FormFactor::Tv : FormFactor::HandheldSmall;
    }
    if (info.touch && main.inches > 0 && main.inches <= 13.0f) {
        return FormFactor::Tablet;
    }
    return FormFactor::Desktop;
}

Tier ByMemory(const DeviceInfo& info) {
    const bool android = info.os == "android";
    if (info.ram_mb <= 0) {
        return Tier::Mid;
    }
    if (info.ram_mb >= (android ? 11000 : 15000) && info.cpu_cores >= 8) {
        return Tier::High;
    }
    return info.ram_mb >= (android ? 5500 : 7500) ? Tier::Mid : Tier::Low;
}

} // namespace

const char* Label(FormFactor form) {
    switch (form) {
    case FormFactor::HandheldSmall:
        return "Handheld";
    case FormFactor::HandheldDual:
        return "Handheld with two screens";
    case FormFactor::Tablet:
        return "Tablet";
    case FormFactor::Tv:
        return "TV";
    default:
        return "Desktop";
    }
}

const char* Label(Tier tier) {
    switch (tier) {
    case Tier::Low:
        return "Low";
    case Tier::Mid:
        return "Medium";
    case Tier::High:
        return "High";
    default:
        return "Very high";
    }
}

const char* Id(Tier tier) {
    switch (tier) {
    case Tier::Low:
        return "low";
    case Tier::Mid:
        return "mid";
    case Tier::High:
        return "high";
    default:
        return "very_high";
    }
}

std::optional<Tier> TierFromId(std::string_view id) {
    for (const Tier tier : {Tier::Low, Tier::Mid, Tier::High, Tier::VeryHigh}) {
        if (id == Id(tier)) {
            return tier;
        }
    }
    return std::nullopt;
}

DeviceProfile Classify(DeviceInfo info, std::optional<Tier> override_tier) {
    DeviceProfile profile;
    const detail::KnownDevice* known = detail::FindKnown(info);
    if (known) {
        profile.known = true;
        profile.name = known->name;
        if (info.screens.empty()) {
            info.screens.emplace_back();
        }
        if (info.screens.front().inches <= 0) {
            info.screens.front().inches = known->inches;
        }
        info.builtin_pad = true;
    } else {
        profile.name = info.manufacturer;
        // "LENOVO 83L3": the model alone when it already names the maker.
        if (Lower(info.model).find(Lower(info.manufacturer)) != std::string::npos) {
            profile.name.clear();
        }
        if (!info.model.empty()) {
            profile.name += (profile.name.empty() ? "" : " ") + info.model;
        }
        if (profile.name.empty()) {
            profile.name = "this device";
        }
    }

    profile.form = FormOf(info);
    if (known) {
        // A known handheld stays one, unless it is plainly on a big screen right now.
        profile.form = profile.form == FormFactor::Tv ? FormFactor::Tv : known->form;
        if (known->form == FormFactor::HandheldDual && info.screens.size() < 2) {
            profile.form = FormFactor::HandheldSmall;
        }
    }

    if (known) {
        profile.detected = known->tier;
        profile.tier_reason = "known device";
        // The same handheld is sold with a faster chip.
        if (Lower(info.soc).find("extreme") != std::string::npos && profile.detected < Tier::High) {
            profile.detected = Tier::High;
            profile.tier_reason = info.soc;
        }
    } else if (const auto gpu = detail::GpuTier(info.gpu)) {
        profile.detected = *gpu;
        profile.tier_reason = info.gpu;
    } else {
        profile.detected = ByMemory(info);
        profile.tier_reason = "memory and processor";
    }
    // Little memory holds every device back: big cores and rewind histories need it.
    if (info.ram_mb > 0 && info.ram_mb < 3500 && profile.detected > Tier::Low) {
        profile.detected = Tier::Low;
        profile.tier_reason = "little memory";
    } else if (info.ram_mb > 0 && info.ram_mb < 5500 && profile.detected > Tier::Mid) {
        profile.detected = Tier::Mid;
        profile.tier_reason = "little memory";
    }
    profile.tier = override_tier.value_or(profile.detected);
    profile.info = std::move(info);
    return profile;
}

} // namespace den::system
