// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/system/settings.h"

namespace den::system {

namespace {

// Keys register from static initialisers, so the list is created on first use.
std::vector<KeyInfo>& Registry() {
    static std::vector<KeyInfo> keys;
    return keys;
}

} // namespace

const std::vector<KeyInfo>& Keys() {
    return Registry();
}

const KeyInfo* FindKey(const std::string& key) {
    for (const KeyInfo& info : Registry()) {
        if (info.key == key) {
            return &info;
        }
    }
    return nullptr;
}

std::vector<KeyInfo> KeysIn(const std::string& section) {
    std::vector<KeyInfo> out;
    for (const KeyInfo& info : Registry()) {
        if (info.meta.section == section) {
            out.push_back(info);
        }
    }
    return out;
}

void RegisterKey(KeyInfo info) {
    if (!FindKey(info.key)) {
        Registry().push_back(std::move(info));
    }
}

} // namespace den::system
