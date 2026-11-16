// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace den::store {
class Settings;
}

namespace den::system {

struct StartInfo {
    bool first_run = false;       // this data folder has never been started before
    bool setup_done = false;      // the user finished (or skipped) first-run setup
    bool upgraded = false;        // the last start was another version of the app
    std::string previous_version; // empty on the first run
};

/// Call once per start, before the first screen: says what kind of start this is and
/// records the version for the next one.
StartInfo NoteStart(store::Settings& settings);

/// "setup.done": the app shows the setup route first until this is set.
bool SetupDone(const store::Settings& settings);
void MarkSetupDone(store::Settings& settings);

} // namespace den::system
