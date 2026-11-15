// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sync engine as the screens reach it (docs/sync.md): created on the first frame, once
// the feature host is there, then pumped every frame with the game that runs.

#pragma once

#include <string>

#include "den/sync/engine.h"

namespace den::app {

/// The engine, or null before the first frame.
sync::Engine* SyncSvc();
/// "Today, 14:05"-style text for a sync time; "Never" for 0.
std::string SyncTimeText(std::int64_t unix_time);

} // namespace den::app
