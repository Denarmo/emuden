// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The device's other apps for the screens that show them: the list from the platform
// (pal::apps, read on a job and again when an app is installed or removed), the user's
// favourites and the apps opened last (settings "apps.favourites", "apps.recent").

#pragma once

#include <cstdint>
#include <string>

#include "den/system/app_shelf.h"
#include "den/ui/page.h"

namespace den::app {

/// The device can list and open apps: the Apps section and the Apps button exist.
bool AppsAvailable();
/// The apps, their favourites and the recent ones. Empty until the first list arrived.
system::AppShelf& Apps();
/// Grows whenever the list, a favourite or the recent apps changed: pages rebuild.
std::uint64_t AppsRevision();
/// The first list is still being read.
bool AppsLoading();
/// The list is the desktop's made-up one (EMUDEN_FAKE_APPS=1): nothing really opens.
bool AppsFake();
void ToggleAppFavourite(const std::string& id);

/// Opens an app; problems and notes are shown as toasts on `stage`. `beside_game`: from the
/// quick settings while a game runs. With two displays the app then opens on the display
/// the quick settings are on, whichever that is, and the game goes on on the other; with
/// one the game is paused first and the app takes the screen (beside.h).
void OpenApp(const std::string& id, bool beside_game, ui::Stage& stage);
/// An app opened beside the game has the keys and the pad now. A touch on the game's
/// display brings them back; this is what the player screen tells the user.
bool PadWithOtherApp();

} // namespace den::app
