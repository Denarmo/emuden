// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace den::achievements {

/// The unlock sound, at `volume` (0..1), over whatever plays. Does nothing without a sound device.
void PlayChime(float volume);

} // namespace den::achievements
