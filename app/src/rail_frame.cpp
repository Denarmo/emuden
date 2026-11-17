// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The frame of the top-level screens: the rail at the left and a titled content column. A
// narrow page gets a slim rail of icons with short captions, so the content keeps the width.

#include <cmath>
#include <numbers>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/platform/pal.h"
#include "den/ui/glyphs.h"
#include "den/ui/widgets.h"
#include "page_widths.h"

namespace den::app {

namespace {

// EmuDEN's mark ("D.", assets/branding), drawn in the theme's ink so it follows the theme.
class BrandMark final : public ui::Node {
public:
    explicit BrandMark(float side) {
        width = height = side;
    }

protected:
    void DrawContent(ui::Context& ctx) override {
        static const std::string path = PathToUtf8(pal::AssetsDir() / "branding" / "mark-white.png");
        if (const gfx::Texture texture = ctx.textures ? ctx.textures->Get(path, 128) : gfx::Texture{}) {
            ctx.canvas->DrawImage(rect, texture, ctx.theme->fg);
        }
    }
};

// The picture of a rail entry, drawn in a 24-unit square with its top left at x, y; false for
// an entry without one.
bool DrawRailIcon(gfx::Canvas& canvas, const std::string& route, float x, float y, gfx::Color ink) {
    const float c = x + 12; // the centre line
    constexpr float LINE = 2.5f;
    if (route == "home") {
        ui::DrawLine(canvas, x + 1, y + 11, c, y + 2, LINE, ink);
        ui::DrawLine(canvas, c, y + 2, x + 23, y + 11, LINE, ink);
        canvas.StrokeRect(gfx::Rect{x + 5, y + 9, 14, 13}, ink, LINE, 2);
        canvas.FillRect(gfx::Rect{c - 2.5f, y + 15, 5, 7}, ink, 1);
    } else if (route == "library") {
        canvas.FillRect(gfx::Rect{x + 2, y + 3, 5, 19}, ink, 1.5f);
        canvas.FillRect(gfx::Rect{x + 9, y + 3, 5, 19}, ink, 1.5f);
        ui::DrawLine(canvas, x + 17, y + 21, x + 21, y + 4, 4.5f, ink);
    } else if (route == "search") {
        canvas.StrokeRect(gfx::Rect{x + 2, y + 2, 15, 15}, ink, LINE, 7.5f);
        ui::DrawLine(canvas, x + 14.5f, y + 14.5f, x + 21.5f, y + 21.5f, 3.2f, ink);
    } else if (route == "consoles") {
        canvas.StrokeRect(gfx::Rect{x, y + 5, 24, 15}, ink, LINE, 7);
        canvas.FillRect(gfx::Rect{x + 4.5f, y + 11.25f, 7, 2.5f}, ink, 1);
        canvas.FillRect(gfx::Rect{x + 6.75f, y + 9, 2.5f, 7}, ink, 1);
        canvas.FillRect(gfx::Rect{x + 15, y + 9, 3.2f, 3.2f}, ink, 1.6f);
        canvas.FillRect(gfx::Rect{x + 18, y + 12.5f, 3.2f, 3.2f}, ink, 1.6f);
    } else if (route == "plugins") {
        canvas.FillRect(gfx::Rect{x + 2, y + 2, 9, 9}, ink, 2);
        canvas.FillRect(gfx::Rect{x + 2, y + 13, 9, 9}, ink, 2);
        canvas.FillRect(gfx::Rect{x + 13, y + 13, 9, 9}, ink, 2);
        canvas.StrokeRect(gfx::Rect{x + 13, y + 2, 9, 9}, ink, 2, 2);
    } else if (route == "settings") {
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(k) * std::numbers::pi_v<float> / 4;
            ui::DrawLine(canvas, c + std::cos(a) * 7, y + 12 + std::sin(a) * 7, c + std::cos(a) * 10.5f,
                         y + 12 + std::sin(a) * 10.5f, 3.5f, ink);
        }
        canvas.StrokeRect(gfx::Rect{c - 8, y + 4, 16, 16}, ink, 3, 8);
    } else {
        return false;
    }
    return true;
}

// A rail entry of the slim rail: the picture over a short caption.
class RailIcon final : public ui::Node {
public:
    RailIcon(std::string route, std::string text) : route_(std::move(route)), text_(std::move(text)) {
        focusable = true;
        label = text_;
        height = 58;
    }
    bool selected = false;

protected:
    void DrawContent(ui::Context& ctx) override {
        const ui::Theme& theme = *ctx.theme;
        ctx.canvas->FillRect(rect, selected || focused ? theme.panel_strong : theme.panel, theme.radius);
        const gfx::Rect box{rect.CenterX() - 12, rect.y + 8, 24, 24};
        if (!DrawRailIcon(*ctx.canvas, route_, box.x, box.y, theme.fg) && !text_.empty()) {
            // An entry a feature added without a picture: its initial in a ring.
            ctx.canvas->StrokeRect(box.Inset(1), theme.fg, 2, 11);
            ui::DrawTextIn(ctx, box, text_.substr(0, 1), theme.text_small, gfx::FONT_BOLD, theme.fg, ui::Align::Center);
        }
        ui::DrawTextIn(ctx, gfx::Rect{rect.x + 2, rect.y + 35, rect.w - 4, 16}, text_, 12,
                       selected || focused ? gfx::FONT_BOLD : gfx::FONT_REGULAR, theme.fg, ui::Align::Center);
    }

private:
    std::string route_, text_;
};

std::function<void()> GoTo(const std::string& route, const std::string& current) {
    return [route, current] {
        if (route != current) {
            Svc().stage->PopToRoot();
            Svc().stage->Replace(route);
        }
    };
}

// A page built with the other rail than its width now asks for is built again.
void FollowWidth() {
    ui::Page* page = Svc().stage ? Svc().stage->Top() : nullptr;
    ui::Node* rail = page ? page->Root().FindById("rail") : nullptr;
    if (rail && rail->Parent() == &page->Root() && rail->width != RailWidth(PageWidth())) {
        page->Rebuild();
    }
}

} // namespace

float PageWidth() {
    const Services& svc = Svc();
    return svc.canvas && svc.canvas->Width() > 0 ? svc.canvas->Width() : 1280.0f;
}

ui::Node* BuildRailFrame(ui::Page& page, const std::string& title) {
    const ui::Theme& theme = *Svc().theme;
    ui::Node& root = page.Root();
    root.axis = ui::Axis::Row;
    const bool slim = SlimRail(PageWidth());

    ui::Node* rail = root.Add(ui::MakeBox(ui::Axis::Column, slim ? 6 : 8));
    rail->id = "rail";
    rail->chrome = true;
    rail->width = slim ? SLIM_RAIL_WIDTH : FULL_RAIL_WIDTH;
    rail->background = theme.panel;
    if (slim) {
        // No room for the brand; the entries scroll when the page is short.
        rail->SetPadding(8, 14);
        rail->scrollable = true;
        rail->scroll_margin = 14;
    } else {
        rail->SetPadding(16, 24);
        ui::Node* brand = rail->Add(ui::MakeBox(ui::Axis::Row, 8));
        brand->align = ui::Align::Center;
        brand->pad_left = 6;
        brand->pad_bottom = 18;
        brand->Emplace<BrandMark>(40.0f);
        ui::Node* word = brand->Add(ui::MakeBox(ui::Axis::Row, 0));
        word->Emplace<ui::Label>("Emu", 26.0f, gfx::FONT_REGULAR);
        word->Emplace<ui::Label>("DEN", 26.0f, gfx::FONT_BOLD);
    }
    for (const RailEntry& entry : Registry::Get().Rail()) {
        ui::Node* item = nullptr;
        if (slim) {
            auto* icon = rail->Emplace<RailIcon>(entry.route, entry.label);
            icon->selected = entry.route == page.Route();
            item = icon;
        } else {
            auto* button = rail->Emplace<ui::Button>(entry.label);
            button->centered = false;
            button->selected = entry.route == page.Route();
            item = button;
        }
        item->id = "rail." + entry.route;
        item->on_activate = GoTo(entry.route, page.Route());
    }

    ui::Node* content = root.Add(ui::MakeBox(ui::Axis::Column, 16));
    content->id = "content";
    content->grow = 1;
    content->pad_left = theme.margin;
    content->pad_top = slim ? 20 : 28;
    if (!title.empty()) {
        content->Emplace<ui::Label>(title, slim ? theme.text_title - 4 : theme.text_title, gfx::FONT_BOLD);
    }
    return content;
}

ui::Node* BuildPlainFrame(ui::Page& page, const std::string& title) {
    const ui::Theme& theme = *Svc().theme;
    ui::Node& root = page.Root();
    root.axis = ui::Axis::Column;
    root.gap = 16;
    root.SetPadding(theme.margin, 28);
    if (!title.empty()) {
        root.Emplace<ui::Label>(title, SlimRail(PageWidth()) ? theme.text_title - 4 : theme.text_title,
                                gfx::FONT_BOLD);
    }
    return &root;
}

DEN_REGISTER_FRAME(FollowWidth);

} // namespace den::app
