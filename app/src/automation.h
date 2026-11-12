// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "den/platform/pal.h"

namespace den::app {

/// The automation endpoint (docs/interfaces.md): off unless EMUDEN_AUTOMATION=<port> is set.
class Automation {
public:
    /// Starts listening when the environment asks for it.
    void Start();
    /// Handles waiting requests. Call once per frame before the events are dispatched.
    void Pump();
    /// Takes the screenshot a request asked for. Call after drawing, before the swap.
    void AfterDraw(int width, int height);

private:
    std::unique_ptr<pal::LineServer> server_;
    std::uint64_t shot_id_ = 0;
    std::string shot_path_;
};

} // namespace den::app
