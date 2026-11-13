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
/// Opens the app in a task of its own, over this app's own window on that display, which
/// keeps running on the other one. When the system refuses the display it opens where the
/// system likes and the notes of the status say so.
Status Launch(const std::string& id, Display display);
/// This app's own window on a display comes to the front of that display, over an app
/// opened there, and takes the keys. The other app stays as it is underneath.
void Raise(Display display);
/// This app's own window on a display goes behind the app under it, which has the display
/// and the keys again without being started anew.
void Lower(Display display);
/// A small round tab over the other app on that display, which the user can drag along the
/// edges; a tap on it is an Event::Handle. False when the platform cannot draw over other
/// apps on that display (nothing is shown then).
bool ShowHandle(Display display);
void HideHandle();
/// While on, the system's Back on that display is reported as Event::Back instead of
/// reaching the app as its own Back.
void CatchBack(Display display, bool on);

struct Event {
    enum class Kind {
        Handle, // the handle was tapped
        Back,   // Back on a display that CatchBack was set for
        Home,   // the system's Home was pressed (which display is not known)
    };
    Kind kind = Kind::Handle;
    Display display = Display::Default;
};
/// What happened since the last call, oldest first.
std::vector<Event> TakeEvents();

/// This app's window is the one keys and the pad go to. False while another app (one
/// opened on the other display) has them; a touch on this app's window brings them back.
bool HasInputFocus();

} // namespace den::pal::apps
