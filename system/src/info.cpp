// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/platform/pal.h"
#include "den/system/info.h"

namespace den::system {

std::string Version() {
    return "0.1.0";
}

std::string Describe() {
    return "EmuDEN " + Version() + " (" + pal::GetCaps().platform_key +
           (pal::IsPortable() ? ", portable)" : ")");
}

} // namespace den::system
