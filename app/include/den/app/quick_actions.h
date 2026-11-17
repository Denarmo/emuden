// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Actions a feature adds to the quick settings of the second screen, by one of the ids
// they know (quick_spec.h, QuickActionIds()): the layouts place it with the others, and an
// interface plugin can order or hide it. An id nobody added is not shown.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace den::app {

struct QuickActionHook {
    std::string id;    // "cheats"
    std::string label; // "Cheats"
    std::function<void()> run;
    std::function<bool()> available;    // while a game runs; empty: always
    std::function<std::string()> value; // a short state at the right ("2 on"); optional
};

/// Adds or replaces the action with that id.
void AddQuickAction(QuickActionHook hook);
const std::vector<QuickActionHook>& QuickActionHooks();

} // namespace den::app

/// DEN_REGISTER_QUICK_ACTION({"cheats", "Cheats", [] { OpenCheats(); }});
#define DEN_REGISTER_QUICK_ACTION(...)                                                            \
    static const bool DEN_QUICK_CONCAT(den_quick_action_, __LINE__) =                             \
        (::den::app::AddQuickAction(::den::app::QuickActionHook __VA_ARGS__), true)
#define DEN_QUICK_CONCAT_(a, b) a##b
#define DEN_QUICK_CONCAT(a, b) DEN_QUICK_CONCAT_(a, b)
