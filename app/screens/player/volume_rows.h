// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The volume sliders of the quick settings and the pause menu, named by where the sound
// comes from: "Game" is the game's own volume (a share of the system's), "Other app" the
// system's media volume, which is what an app open beside the game plays at. Android has no
// volume for one other app, so that is as close as it gets; the hardware keys move it too.

#pragma once

#include <string>
#include <vector>

#include "den/ui/widgets.h"

namespace den::app {

/// The rows, ids `prefix`.volume and `prefix`.volume_other. `other_app` adds the second
/// one where the platform has a media volume to set. Each says which screen it belongs to
/// when there are two. They carry a description, so give them a row height of 64 or more.
std::vector<ui::RowSpec> VolumeRows(const std::string& prefix, bool other_app);

} // namespace den::app
