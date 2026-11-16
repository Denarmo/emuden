// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An app open beside the running game: the state (system::AppBeside) kept in step with the
// platform every frame, and what the quick settings and the player ask of it.

#pragma once

#include <string>

#include "den/system/app_beside.h"
#include "den/system/app_shelf.h"
#include "den/ui/page.h"

namespace den::app {

/// Where the game, the quick settings and an app beside the game are right now.
const system::AppBeside& Beside();
/// Opens an app while a game runs: on the display of the quick settings with two displays
/// (the game goes on), over the paused game with one. Problems are toasts on `stage`.
void BesideOpen(const system::AppItem& app, ui::Stage& stage);
/// "Back to <app>": the quick settings give the display back to the app under them.
void BesideBackToApp();
/// "Close <app>": plain quick settings again.
void BesideClose();
/// The quick settings come over whatever covers them (the tab on the game's display).
void BesidePullSettings();

/// What a display is called on this device: "Top screen" and "Bottom screen" on a handheld
/// with two, else "Main screen" and "Second screen".
std::string ScreenName(system::BesideScreen screen);

} // namespace den::app
