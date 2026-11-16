// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Built instead of fake_host.cpp and sample_pages.cpp when EMUDEN_DEV_TOOLS is off.

#include "fake_host.h"

namespace den::app {

std::unique_ptr<plugins::FeatureHost> CreateSampleFeatureHost() {
    return nullptr;
}

} // namespace den::app
