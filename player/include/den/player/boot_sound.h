// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace den::player {

/// EmuDEN's start-up sound (made here, about a second long) at `volume` (0..1), on a
/// stream of its own beside the game's. Does nothing without a sound device.
void PlayBootSound(float volume);

} // namespace den::player
