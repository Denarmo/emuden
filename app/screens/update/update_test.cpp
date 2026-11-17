// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Test-only: lets a test run the updater as another version against a server of its own.
// Built only with EMUDEN_DEV_TOOLS (update_test_off.cpp stands in otherwise).

#include "den/platform/pal.h"
#include "update_state.h"

namespace den::app {

update::TestOverrides RawTestOverrides() {
    return {pal::GetEnv("EMUDEN_TEST_UPDATE_VERSION"), pal::GetEnv("EMUDEN_TEST_UPDATE_URL")};
}

} // namespace den::app
