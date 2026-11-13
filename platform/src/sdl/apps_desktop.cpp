// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The desktop has no list of other apps to open: the section is an Android one.

#include "den/platform/apps.h"

namespace den::pal::apps {

bool Available() {
    return false;
}

std::vector<App> List() {
    return {};
}

std::uint64_t Revision() {
    return 0;
}

Status Launch(const std::string&, Display) {
    return Status::Fail("this device cannot open other apps");
}

void Raise(Display) {}

void Lower(Display, const std::string&) {}

bool ShowHandle(Display) {
    return false;
}

void HideHandle() {}

void CatchBack(Display, bool) {}

std::vector<Event> TakeEvents() {
    return {};
}

bool HasInputFocus() {
    return true;
}

} // namespace den::pal::apps
