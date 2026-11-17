// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The interface layouts that draw only what is on the screen, so a tab of thousands of games
// costs what one screenful does: "grid", "list", "wheel" and "hero". Each is a CoverCarousel
// (one focusable node whose id ends in the selected game's id, the launch timing, the page's
// tabs and buttons) that shows the selected game's details itself. A drag scrolls them through
// the node's own scroller, which an invisible child as long as the content gives its bounds.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cover_carousel.h"
#include "layout_math.h"

namespace den::app {

class GameView : public CoverCarousel {
public:
    explicit GameView(std::shared_ptr<const InterfaceSpec> spec);

    /// The Continue tab's: per game, the line under its name and the picture of its moment.
    void SetNotes(std::vector<std::string> lines, std::vector<std::string> moments);

    gfx::Rect FocusRect() const override;
    gfx::Rect LaunchRect() const override;
    bool OnNav(input::Nav nav) override;
    bool WantsDrag(float x, float y) const override;
    void DragTo(float x, float y) override;

protected:
    /// Where the selected game is, for the selection border.
    virtual gfx::Rect SelectedRect() const = 0;
    /// The game drawn at a point, if any.
    virtual std::optional<std::size_t> GameAt(float x, float y) const = 0;
    /// A pad direction; false lets the page move the focus out.
    virtual bool Move(layout::Move move) = 0;
    /// How long the scrolled content is (0: nothing scrolls).
    virtual float Extent() const = 0;
    /// The selection follows the scroll position (wheel, hero) instead of the scroll keeping
    /// the selection in view (grid, list).
    virtual bool Centred() const {
        return false;
    }
    /// Grid and list: the scroll that shows the selection.
    virtual float Reveal(float from) const {
        return from;
    }
    /// Wheel and hero: the distance between two games along the scroll.
    virtual float Pitch() const {
        return 1;
    }

    void OnTick(ui::Context& ctx) override;
    void OnGamesSet() override;

    float Scroll() const {
        return scroller.Position();
    }
    /// Wheel and hero: the selection's place along the scroll, fractional while it moves.
    float Place() const {
        return Scroll() / std::max(1.0f, Pitch());
    }
    void SelectFromPad(std::size_t index);

    std::string Line(std::size_t index) const;   // the Continue note, else the game's records
    std::string Moment(std::size_t index) const; // its moment's picture, else ""
    /// The game's cover in `rect` (the whole picture centred when `fit`), its name over a
    /// missing or drawn cover, the HACK sticker. Returns where the picture went.
    gfx::Rect DrawArt(ui::Context& ctx, const library::Entry& game, const gfx::Rect& rect, float light,
                      bool fit, int side = 384) const;
    /// The selected game's art (its moment for a Continue card), crossfading from the last one.
    gfx::Rect DrawFeature(ui::Context& ctx, const gfx::Rect& rect, bool fit);
    /// The selected game's name (up to `lines` lines), its records and its place in the tab,
    /// from `top`, aligned left or centred. Returns the height used.
    float DrawDetails(ui::Context& ctx, const gfx::Rect& rect, bool centred, int lines, float name_size);
    /// `rect` grown by the zoom of a launch that runs.
    gfx::Rect Grown(const gfx::Rect& rect) const;
    /// The text sizes of the page's theme.
    const ui::Theme& Theme() const {
        return *theme_;
    }

    ui::Node* spacer_ = nullptr;
    const ui::Theme* theme_ = nullptr;
    std::vector<std::string> lines_, moments_;
    mutable gfx::Rect launch_rect_{};
    bool touch_ = false;  // the scroll moves by a finger: the selection follows it (Centred)
    bool reveal_ = true;  // jump to the selection once the size is known
    std::size_t shown_ = ~std::size_t{0}; // the selection the scroll last followed
    std::string feature_, feature_before_;
    float feature_fade_ = 1;
    int touch_tick_ = -10;
};

/// Rows of covers, as many per row as fit, beside the selected game's details.
class GridView final : public GameView {
public:
    using GameView::GameView;

protected:
    void DrawContent(ui::Context& ctx) override;
    gfx::Rect SelectedRect() const override;
    std::optional<std::size_t> GameAt(float x, float y) const override;
    bool Move(layout::Move move) override;
    float Extent() const override;
    float Reveal(float from) const override;

private:
    struct Geometry {
        gfx::Rect grid, panel; // panel empty when there is none
        bool panel_below = false;
        int columns = 1;
        float cell_w = 1, cover_h = 1, gap = 12;
        layout::Rows rows;
    };
    Geometry Measure() const;
    gfx::Rect Cell(const Geometry& g, std::size_t index) const;
    void DrawPanel(ui::Context& ctx, const Geometry& g);
};

/// The games as rows of text with a small cover, the selected one's cover large beside them.
class ListView final : public GameView {
public:
    using GameView::GameView;

protected:
    void DrawContent(ui::Context& ctx) override;
    gfx::Rect SelectedRect() const override;
    std::optional<std::size_t> GameAt(float x, float y) const override;
    bool Move(layout::Move move) override;
    float Extent() const override;
    float Reveal(float from) const override;

private:
    struct Geometry {
        gfx::Rect list, art; // art empty when there is none
        layout::Rows rows;
    };
    Geometry Measure() const;
    gfx::Rect Row(const Geometry& g, std::size_t index) const;
    void DrawRow(ui::Context& ctx, const Geometry& g, std::size_t index);
};

/// Names on a turning arc at one side, the selected game's art filling the rest.
class WheelView final : public GameView {
public:
    using GameView::GameView;

protected:
    void DrawContent(ui::Context& ctx) override;
    gfx::Rect SelectedRect() const override;
    std::optional<std::size_t> GameAt(float x, float y) const override;
    bool Move(layout::Move move) override;
    float Extent() const override;
    bool Centred() const override {
        return true;
    }
    float Pitch() const override;

private:
    struct Geometry {
        gfx::Rect wheel, art;
        bool behind = false; // a narrow screen: the art behind the names
        float half_h = 1;
    };
    Geometry Measure() const;
    struct Name {
        gfx::Rect rect;
        float size = 18, alpha = 1, weight = 0;
    };
    Name NameAt(const Geometry& g, ui::Context* ctx, std::size_t index) const;
    float NameSize() const;
};

/// The selected game large across the screen, a thin row of covers under it.
class HeroView final : public GameView {
public:
    explicit HeroView(std::shared_ptr<const InterfaceSpec> spec);

    /// A swipe along the row that starts on a cover moves the selection with the finger.
    void DragTo(float x, float y) override;

protected:
    void DrawContent(ui::Context& ctx) override;
    gfx::Rect SelectedRect() const override;
    std::optional<std::size_t> GameAt(float x, float y) const override;
    bool Move(layout::Move move) override;
    float Extent() const override;
    bool Centred() const override {
        return true;
    }
    float Pitch() const override;

private:
    struct Geometry {
        gfx::Rect hero, strip;
        float thumb_w = 1, thumb_h = 1, gap = 12;
    };
    Geometry Measure() const;
    gfx::Rect Thumb(const Geometry& g, std::size_t index) const;
};

/// The view of a "grid", "list", "wheel" or "hero" interface; null for other layouts.
std::unique_ptr<GameView> MakeGameView(const std::shared_ptr<const InterfaceSpec>& spec);

} // namespace den::app
