// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Built instead of automation.cpp when EMUDEN_DEV_TOOLS is off: there is no endpoint.

#include "automation.h"

namespace den::app {

void Automation::Start() {}

void Automation::Pump() {}

void Automation::AfterDraw(int, int) {}

} // namespace den::app
