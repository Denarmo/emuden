// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Core options a console plugin chooses by device: its components' "tuning".

#include <algorithm>

#include "den/system/device.h"

namespace den::system {

namespace {

constexpr std::size_t MAX_CASES = 32;

int WholeNumber(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Number && value->number >= 0 && value->number < 1e6
               ? static_cast<int>(value->number)
               : 0;
}

int MainScreenHeight(const DeviceProfile& profile) {
    if (profile.info.screens.empty()) {
        return 1080;
    }
    const Screen& screen = profile.info.screens.front();
    return screen.width <= 0 || screen.height <= 0 ? 1080 : std::min(screen.width, screen.height);
}

} // namespace

std::vector<TuningRule> ParseTuning(const JsonValue& tuning, const std::string& key_prefix) {
    std::vector<TuningRule> out;
    for (const auto& [option, cases] : tuning.members) {
        if (option.empty() || cases.kind != JsonValue::Kind::Array) {
            continue;
        }
        TuningRule rule;
        std::string safe = option;
        std::replace(safe.begin(), safe.end(), '.', '_');
        rule.key = key_prefix + safe;
        for (const JsonValue& item : cases.items) {
            const JsonValue* value = item.Find("value");
            if (value == nullptr || value->kind != JsonValue::Kind::String || rule.cases.size() >= MAX_CASES) {
                continue;
            }
            TuningCase one;
            if (const std::string tier = item.String("min_tier"); !tier.empty()) {
                one.min_tier = TierFromId(tier);
                if (!one.min_tier) {
                    continue; // a tier this version does not know is not met
                }
            }
            one.min_screen_height = WholeNumber(item, "min_screen_height");
            one.min_cpu_cores = WholeNumber(item, "min_cpu_cores");
            one.value = value->string;
            one.describe = item.String("describe");
            rule.cases.push_back(std::move(one));
        }
        if (!rule.cases.empty()) {
            out.push_back(std::move(rule));
        }
    }
    return out;
}

const TuningCase* TuneFor(const TuningRule& rule, const DeviceProfile& profile) {
    const int height = MainScreenHeight(profile);
    for (const TuningCase& one : rule.cases) {
        if ((!one.min_tier || profile.tier >= *one.min_tier) && height >= one.min_screen_height &&
            profile.info.cpu_cores >= one.min_cpu_cores) {
            return &one;
        }
    }
    return nullptr;
}

} // namespace den::system
