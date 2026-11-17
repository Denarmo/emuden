// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Built instead of update_test.cpp when EMUDEN_DEV_TOOLS is off: no overrides.

#include "update_state.h"

namespace den::app {

update::TestOverrides RawTestOverrides() {
    return {};
}

} // namespace den::app
