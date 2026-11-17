// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/platform/pal.h"
#include "den/system/info.h"

namespace den::system {

std::string Version() {
    const std::string release = ReleaseVersion();
    return release.empty() ? DEN_PROJECT_VERSION : release;
}

std::string ReleaseVersion() {
    return DEN_RELEASE_VERSION;
}

std::string Describe() {
    return "EmuDEN " + Version() + " (" + pal::GetCaps().platform_key +
           (pal::IsPortable() ? ", portable)" : ")");
}

} // namespace den::system
