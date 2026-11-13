// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "den/platform/device.h"

namespace den::pal {

/// Adds what only the operating system knows (model, physical screen size, built-in pad) to
/// the facts SDL gave. One implementation per system: win/, posix/ (Linux), android/.
void FillNativeFacts(DeviceFacts& facts);

} // namespace den::pal
