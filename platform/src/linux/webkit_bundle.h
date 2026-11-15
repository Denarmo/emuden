// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The WebKitGTK the AppImage carries (`usr/lib/webkit`, made by tools/linux/bundle-webkit.sh),
// for systems without one, SteamOS among them.

#pragma once

#include <string>

namespace den::pal::gtk {

/// The bundled engine library beside the executable (`<exe>/../lib/webkit/`); empty when
/// the app carries none.
std::string BundledWebKit();

/// Readies the process for the bundled engine just loaded from `library`: its helper
/// programs, its injected bundle, its TLS module. False with `reason` filled when it cannot
/// be used.
bool PrepareBundledWebKit(const std::string& library, std::string& reason);

} // namespace den::pal::gtk
