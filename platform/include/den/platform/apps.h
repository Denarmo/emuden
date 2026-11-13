// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The other apps installed on the device, where the platform can list and open them
// (Android: the launcher's activities). On the desktop there is no such list.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::pal::apps {

struct App {
    std::string id;    // stable for the app's life on the device ("package/activity")
    std::string label; // its name as the launcher shows it
    std::string icon;  // UTF-8 path of a PNG in the app cache; empty when it has none
};

/// Which display an app opens on, seen from this app's own window.
enum class Display {
    Default, // wherever the system puts it
    Main,    // the display this app's window is on
    Second,  // the other display (pal::GetSecondDisplay)
};

/// The platform can list and open apps. Constant for the life of the process.
bool Available();
/// Every app the launcher would show except this one, in no order. It renders icons that
/// are not in the cache yet, so call it from a job. Empty when not Available().
std::vector<App> List();
/// Grows whenever an app was installed, removed or changed: List() again.
std::uint64_t Revision();
/// Opens the app in a task of its own. With Display::Second it opens beside this app, which
/// keeps running; when the system refuses that display it opens where the system likes and
/// the notes of the status say so.
Status Launch(const std::string& id, Display display);
/// This app's window is the one keys and the pad go to. False while another app (one
/// opened on the other display) has them; a touch on this app's window brings them back.
bool HasInputFocus();

} // namespace den::pal::apps
