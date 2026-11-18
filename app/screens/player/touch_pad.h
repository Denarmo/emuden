// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Hardware-inspired touch overlay & portrait controller deck renderer for the player screen.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "den/gfx/canvas.h"
#include "den/input/input.h"
#include "den/player/session.h"
#include "den/ui/node.h"
#include "den/ui/widgets.h"
#include "touch_spec.h"

namespace den::app {

class TouchPad {
public:
    TouchPad();
    ~TouchPad();

    void Sync(std::string_view system_id, const gfx::Rect& area, bool dual_screen, bool can_fast_forward,
              bool fast_forward, bool playing);
    void Stop();

    bool Visible() const {
        return visible_;
    }
    bool PortraitActive(const gfx::Rect& area) const {
        return visible_ && area.h > area.w && layout_.portrait;
    }
    gfx::Rect GameRoom() const {
        return layout_.game_room;
    }

    player::TouchState State() const;

    struct InputAction {
        bool consumed = false;
        bool open_menu = false;
        bool toggle_fast = false;
        bool swap_screens = false;
    };

    InputAction HandleEvent(const input::Event& event, float scale);
    void DrawShell(ui::Context& ctx) const;
    void DrawControls(ui::Context& ctx, bool black_frame = false) const;

    std::string Json() const;

private:
    bool ShouldShow() const;
    void EnsureLayout(std::string_view system_id, const gfx::Rect& area, bool dual_screen,
                      bool can_fast_forward);

    TouchController controller_;
    bool visible_ = false;
    bool playing_ = false;
    bool fast_forward_ = false;
    bool touch_woken_ = false;
    std::string system_id_;
    gfx::Rect last_area_{};
    float safe_l_ = -1, safe_t_ = -1, safe_r_ = -1, safe_b_ = -1;
    bool last_dual_ = false;
    bool last_ff_cap_ = true;
    TouchDeckLayout layout_{};
};

ui::RowSpec TouchModeRow();
std::vector<ui::RowSpec> TouchSettingRows();
std::string TouchPadJson();

} // namespace den::app
