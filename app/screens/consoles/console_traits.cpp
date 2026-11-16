// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the console plugins say about their consoles, handed to the modules that use it: pad
// layouts to the player and tuned core options to the device defaults. Again whenever the
// plugins change.

#include <cctype>
#include <optional>
#include <set>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/player/buttons.h"
#include "den/system/device.h"

namespace den::app {

namespace {

std::string OptionsPrefix(const std::string& options_name) {
    std::string name;
    for (const char c : options_name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return "core_options." + name + ".";
}

void HandOut() {
    plugins::Host* host = Svc().plugins;
    if (host == nullptr) {
        return;
    }
    std::vector<system::TuningRule> tuning;
    std::set<std::string> keys;
    for (const plugins::Console& console : host->Consoles()) {
        std::optional<bool> nintendo;
        if (!console.pad_layout.empty()) {
            nintendo = console.pad_layout == "nintendo";
        }
        player::SetPadTraits(console.id, nintendo, console.digital_only);
        for (const plugins::CoreInfo& core : host->CoresFor(console.id)) {
            if (core.tuning.kind != JsonValue::Kind::Object) {
                continue;
            }
            // A core two consoles share (Dolphin) is tuned once.
            for (system::TuningRule& rule : system::ParseTuning(core.tuning, OptionsPrefix(core.options_name))) {
                if (keys.insert(rule.key).second) {
                    tuning.push_back(std::move(rule));
                }
            }
        }
    }
    if (tuning != system::Tuning()) {
        system::SetTuning(std::move(tuning));
    }
}

void Watch() {
    static std::optional<std::uint64_t> seen;
    if (Svc().plugins != nullptr && Svc().plugins->Revision() != seen) {
        seen = Svc().plugins->Revision();
        HandOut();
    }
}

} // namespace

DEN_REGISTER_FRAME(Watch);

} // namespace den::app
