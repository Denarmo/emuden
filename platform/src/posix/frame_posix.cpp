// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../frame_native.h"

namespace den::pal {

// Only Windows windows get the app's own title bar (Caps::custom_frame).
void StyleCustomFrame(void*, std::uint8_t, std::uint8_t, std::uint8_t) {}

} // namespace den::pal
