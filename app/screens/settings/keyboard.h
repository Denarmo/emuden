// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "den/ui/widgets.h"

namespace den::app {

/// Adds the "Keyboard: System / EmuDEN" row where the platform has a keyboard of its own
/// (keyboard.cpp); false and no row elsewhere.
bool AddKeyboardRow(std::vector<ui::RowSpec>& rows);

} // namespace den::app
