// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Variables the app sets in its own environment for helpers of its own (the web engine it
// carries starts processes that read them). Programs the app starts get the values from
// before, so none of it reaches an emulator or a browser.

#pragma once

#include <string>
#include <vector>

namespace den::pal {

struct OwnEnvChange {
    std::string name;
    bool had = false; // whether the variable was set before
    std::string before;
};

/// Sets `name` in the app's environment and remembers what it was. Main thread, before any
/// child is started.
void SetOwnEnv(const char* name, const std::string& value);

const std::vector<OwnEnvChange>& OwnEnvChanges();

} // namespace den::pal
