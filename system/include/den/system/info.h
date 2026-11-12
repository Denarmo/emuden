// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace den::system {

/// "0.1.0".
std::string Version();
/// One line for logs and the About row: version, platform key, portable or not.
std::string Describe();

} // namespace den::system
