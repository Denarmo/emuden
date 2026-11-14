// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The desktop starts engines as child processes; there are no engine apps.

#include "den/platform/engine_apps.h"

namespace den::pal::engine_apps {

bool Available() {
    return false;
}

Installed Query(const std::string&) {
    return {};
}

Status Install(const std::filesystem::path&) {
    return Status::Fail("This device installs no apps.");
}

Status OpenStore(const std::string&) {
    return Status::Fail("This device has no app store.");
}

Status Play(const PlayRequest&) {
    return Status::Fail("This device starts no apps.");
}

Status Open(const PlayRequest&) {
    return Status::Fail("This device starts no apps.");
}

Status Resume(const std::string&, apps::Display) {
    return Status::Fail("This device starts no apps.");
}

} // namespace den::pal::engine_apps
