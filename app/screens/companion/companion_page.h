// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include "den/ui/page.h"

namespace den::app {

/// A page of the second screen. It is used by touch only, the pad stays with the main
/// screen: no hint bar, and no selection border (there is one selection, on the main screen).
class CompanionPage : public ui::Page {
public:
    bool Immersive() const override {
        return true;
    }
    std::vector<ui::Hint> Hints() const override {
        return {};
    }
    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        SetFocus(nullptr, false);
        ui::Page::Draw(ctx, area);
    }
};

} // namespace den::app
