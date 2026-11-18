// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Settings rows for on-screen touch controls, shared between Settings and the pause menu.

#pragma once

#include <vector>

#include "den/ui/widgets.h"

namespace den::app {

ui::RowSpec TouchModeRow();
std::vector<ui::RowSpec> TouchSettingRows();

} // namespace den::app
