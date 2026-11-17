// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Cheats" in the quick settings, once the cheats page (den/app/cheats.h) is in the build.

#if __has_include("den/app/cheats.h")

#include "den/app/cheats.h"
#include "den/app/quick_actions.h"

DEN_REGISTER_QUICK_ACTION({"cheats", "Cheats", [] { ::den::app::OpenCheats(); }});

#endif
