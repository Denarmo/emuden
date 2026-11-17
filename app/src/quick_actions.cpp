// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/app/quick_actions.h"

#include <algorithm>

namespace den::app {

namespace {

std::vector<QuickActionHook>& Hooks() {
    static std::vector<QuickActionHook> hooks;
    return hooks;
}

} // namespace

void AddQuickAction(QuickActionHook hook) {
    std::vector<QuickActionHook>& hooks = Hooks();
    const auto it = std::find_if(hooks.begin(), hooks.end(), [&](const QuickActionHook& h) { return h.id == hook.id; });
    if (it != hooks.end()) {
        *it = std::move(hook);
    } else {
        hooks.push_back(std::move(hook));
    }
}

const std::vector<QuickActionHook>& QuickActionHooks() {
    return Hooks();
}

} // namespace den::app
