// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What pal::Init asks of the platform's lifecycle code (src/android, src/sdl).

#pragma once

namespace den::pal {

/// Before SDL starts: hints that must be set by then.
void LifecycleBeforeInit();
/// After SDL started.
void LifecycleAfterInit();

} // namespace den::pal
