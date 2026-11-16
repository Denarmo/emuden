// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include "den/plugins/host.h"

namespace den::app {

/// Lets the user choose a folder holding the console's BIOS files and copies the
/// ones the manifest names into the core's system folder, which may be out of the user's
/// reach (an Android app's own storage).
void ImportBios(const plugins::Console& console, const std::filesystem::path& system_dir);
/// Counts finished imports, so a page showing the BIOS state knows to look again.
int BiosImports();

} // namespace den::app
