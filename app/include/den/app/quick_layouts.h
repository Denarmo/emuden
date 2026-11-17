// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Choosing the layout of the quick settings on the second screen ("ui.companion.quick"):
// EmuDEN's standard one, or a layout of an interface plugin with "quick_settings".

#pragma once

#include "den/ui/widgets.h"

namespace den::app {

/// The Settings row that picks it, shown under Settings, Interface beside the Home interface.
ui::RowSpec QuickLayoutRow();

} // namespace den::app
