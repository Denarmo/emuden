// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The running game's cheats page: its cheats with toggles, Add, Edit, Delete and importing a
// RetroArch .cht file. Route "player.cheats" on the main stage (by pad) and
// "companion.cheats" on the second screen's (by touch).

#pragma once

#include <string>

namespace den::ui {
class Stage;
}

namespace den::app {

/// Opens the page on `stage`, the main one or the second screen's.
void OpenCheats(ui::Stage& stage);
/// Opens it on the second screen's stage (its quick settings).
void OpenCheats();
/// What a row that opens the page shows at its right: "2 on", "None on", or why they are off.
std::string CheatsSummary();

} // namespace den::app
