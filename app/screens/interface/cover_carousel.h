// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One row of covers that glides: the selected one large in the middle, the others smaller and
// darker beside it, with a mirror image under each. It takes the focus as one node; its id
// ends in the selected game's id, so the second screen follows it like any game tile.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "den/library/library.h"
#include "den/ui/node.h"
#include "interface_spec.h"

namespace den::app {

class CoverCarousel final : public ui::Node {
public:
    explicit CoverCarousel(std::shared_ptr<const InterfaceSpec> spec);

    /// Shows `games` with `selected` in the middle (clamped).
    void SetGames(std::vector<library::Entry> games, std::size_t selected, float aspect,
                  gfx::Color console_color);
    const library::Entry* Selected() const;
    std::size_t Index() const {
        return index_;
    }
    void Select(std::size_t index, bool glide = true);
    /// Plays the launch animation, then runs `then`.
    void Launch(std::function<void()> then);
    bool Launching() const {
        return launch_ >= 0;
    }
    /// 0..1, eased, while the launch animation runs; else 0.
    float LaunchAmount() const;
    gfx::Color ConsoleColor() const {
        return color_;
    }

    std::function<void(const library::Entry&)> on_select;

    bool OnNav(input::Nav nav) override;
    gfx::Rect FocusRect() const override;
    bool WantsDrag(float x, float y) const override;
    void DragTo(float x, float y) override;

protected:
    void DrawContent(ui::Context& ctx) override;
    void OnTick(ui::Context& ctx) override;

private:
    struct Slot {
        gfx::Rect rect;
        float weight = 0; // 1 for the selected cover, 0 beside it
    };
    Slot SlotAt(float offset) const;
    float CoverHeight() const;
    float Step() const; // centre-to-centre distance of two side covers
    void DrawOne(ui::Context& ctx, const library::Entry& game, const Slot& slot, float lift);

    std::shared_ptr<const InterfaceSpec> spec_;
    std::vector<library::Entry> games_;
    std::size_t index_ = 0;
    float pos_ = 0;
    float aspect_ = 0.75f;
    gfx::Color color_;
    double time_ = 0;
    float launch_ = -1; // 0..1 while the launch animation runs
    std::function<void()> after_launch_;
    // A drag is a swipe: the row follows the finger from where it was when the drag began.
    int ticks_ = 0, drag_tick_ = -10;
    float drag_x_ = 0, drag_pos_ = 0;
};

} // namespace den::app
