// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "den/app/app.h"

namespace den::app::web {

constexpr float BAR_HEIGHT = 72;
constexpr float BAR_BUTTON = 48;

enum class BarAction { Back, Forward, Reload, Close, Menu, Address };

struct BarHit {
    gfx::Rect rect;
    bool enabled = true;
    BarAction action = BarAction::Close;
};

struct BarInput {
    bool live = false; // a web view shows the page (not the fallback)
    bool browser = false;
    bool can_back = false, can_forward = false;
    std::string title;
    std::string line;     // the host and what the page is doing
    float progress = -1;  // of the download, 0..1; negative for none
};

/// Draws the bar and returns where its buttons are.
std::vector<BarHit> DrawWebBar(ui::Context& ctx, const gfx::Rect& area, const BarInput& in);

} // namespace den::app::web
