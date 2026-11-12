// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the Consoles screens say about a console, and the install jobs they started.

#pragma once

#include <string>

#include "den/base/jobs.h"
#include "den/plugins/host.h"

namespace den::app {

enum class ConsoleState {
    Ready,
    NotInstalled,
    NotAvailable, // no core of the console has a download for this device
    UpdateAvailable,
    MissingBios,
    EngineMissing,
    Busy,
};

ConsoleState StateOf(const plugins::Console& console);
/// "Ready", "Core not installed", "Update available", ... or the running job's progress.
std::string StateText(const plugins::Console& console);
int ReadyCount();

/// The job running for a console, if any.
JobPtr ConsoleJob(const std::string& system);
/// Remembers a job the plugin host started; a toast tells how it ended. `done` names the
/// result ("installed", "removed").
void TrackConsoleJob(const std::string& system, const std::string& name, const std::string& done,
                     JobPtr job);

} // namespace den::app
