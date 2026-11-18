// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The frame of the top-level screens: the rail at the left and a titled content column. A
// narrow page gets a slim rail of icons with short captions, so the content keeps the width.
// A page's second level gives the rail up, so no more than two levels show at once.

#include <cmath>
#include <numbers>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/platform/pal.h"
#include "den/ui/translate.h"
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

// A rail entry of the slim or bottom rail: the picture over a short caption.
class RailIcon final : public ui::Node {
public:
    RailIcon(std::string route, std::string text, bool bottom = false)
        : route_(std::move(route)), text_(std::move(text)), bottom_(bottom) {
        focusable = true;
        label = text_;
        height = bottom_ ? 54.0f : 58.0f;
        if (bottom_) {
            grow = 1.0f;
        }
    }
    bool selected = false;

protected:
    void DrawContent(ui::Context& ctx) override {
        const ui::Theme& theme = *ctx.theme;
        const bool lit = selected || focused;
        if (lit || !bottom_) {
            ctx.canvas->FillRect(rect, lit ? theme.panel_strong : theme.panel, theme.radius);
        }
        const gfx::Rect box{rect.CenterX() - 12, rect.y + (bottom_ ? 6.0f : 8.0f), 24, 24};
        if (!DrawRailIcon(*ctx.canvas, route_, box.x, box.y, theme.fg) && !text_.empty()) {
            // An entry a feature added without a picture: its initial in a ring.
            ctx.canvas->StrokeRect(box.Inset(1), theme.fg, 2, 11);
            ui::DrawTextIn(ctx, box, text_.substr(0, 1), theme.text_small, gfx::FONT_BOLD, theme.fg, ui::Align::Center);
        }
        ui::DrawTextIn(ctx, gfx::Rect{rect.x + 2, rect.y + (bottom_ ? 32.0f : 35.0f), rect.w - 4, 16},
                       ui::Translate(text_), bottom_ ? 11 : 12, lit ? gfx::FONT_BOLD : gfx::FONT_REGULAR,
                       theme.fg, ui::Align::Center);
    }

private:
    std::string route_, text_;
    bool bottom_ = false;
};

// A square with a chevron pointing back, for a finger.
class BackButton final : public ui::Node {
public:
    BackButton() {
        id = "page.back";
        label = "Back";
        focusable = true;
        chrome = true; // the content gets the first focus
        width = height = 48;
    }

protected:
    void DrawContent(ui::Context& ctx) override {
        const ui::Theme& theme = *ctx.theme;
        ctx.canvas->FillRect(rect, focused ? theme.panel_strong : theme.panel, theme.radius);
        const float cx = rect.CenterX() - 2, cy = rect.CenterY();
        ui::DrawLine(*ctx.canvas, cx + 5, cy - 9, cx - 4, cy, 3, theme.fg);
        ui::DrawLine(*ctx.canvas, cx - 4, cy, cx + 5, cy + 9, 3, theme.fg);
    }
};

std::function<void()> GoTo(const std::string& route, const std::string& current) {
    return [route, current] {
        if (route != current) {
            Svc().stage->PopToRoot();
            Svc().stage->Replace(route);
        }
    };
}

// A page built with the other rail (full, slim or bottom) than its size asks for is built again.
void FollowWidth() {
    ui::Page* page = Svc().stage ? Svc().stage->Top() : nullptr;
    ui::Node* rail = page ? page->Root().FindById("rail") : nullptr;
    if (!rail || rail->Parent() != &page->Root()) {
        return;
    }
    const float w = PageWidth();
    const float h = PageHeight();
    const bool bottom = BottomRail(w, h);
    const bool was_bottom = rail->axis == ui::Axis::Row;
    if (bottom != was_bottom || (!bottom && rail->width != RailWidth(w, h)) ||
        (bottom && rail->height != BOTTOM_RAIL_HEIGHT + SafeMargins().bottom)) {
        page->Rebuild();
    }
}

} // namespace

float PageWidth() {
    const Services& svc = Svc();
    return svc.canvas && svc.canvas->Width() > 0 ? svc.canvas->Width() : 1280.0f;
}

float PageHeight() {
    const Services& svc = Svc();
    return svc.canvas && svc.canvas->Height() > 0 ? svc.canvas->Height() : 720.0f;
}

Services::SafePadding SafeMargins() {
    return Svc().safe;
}

ui::Node* BuildRailFrame(ui::Page& page, const std::string& title) {
    const ui::Theme& theme = *Svc().theme;
    const float width = PageWidth();
    const float height = PageHeight();
    const Services::SafePadding safe = SafeMargins();
    const bool bottom = BottomRail(width, height);
    const bool slim = !bottom && SlimRail(width);

    ui::Node& root = page.Root();
    root.axis = bottom ? ui::Axis::Column : ui::Axis::Row;

    ui::Node* content = nullptr;
    auto add_content = [&] {
        content = root.Add(ui::MakeBox(ui::Axis::Column, 16));
        content->id = "content";
        content->grow = 1;
        content->pad_left = theme.margin + (bottom ? safe.left : 0.0f);
        content->pad_top = std::max(slim || bottom ? 20.0f : 28.0f, safe.top + 8.0f);
        if (!title.empty()) {
            content->Emplace<ui::Label>(title, slim || bottom ? theme.text_title - 4 : theme.text_title,
                                        gfx::FONT_BOLD);
        }
    };

    auto add_rail_entries = [&](ui::Node* rail) {
        const auto& entries = Registry::Get().Rail();
        for (const RailEntry& entry : entries) {
            ui::Node* item = nullptr;
            if (bottom || slim) {
                auto* icon = rail->Emplace<RailIcon>(entry.route, entry.label, bottom);
                icon->selected = entry.route == page.Route();
                if (bottom && entries.size() > 7) {
                    icon->grow = 0;
                    icon->width = 68;
                }
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
    };

    if (bottom) {
        add_content();
        ui::Node* rail = root.Add(ui::MakeBox(ui::Axis::Row, 4));
        rail->id = "rail";
        rail->chrome = true;
        rail->height = BOTTOM_RAIL_HEIGHT + safe.bottom;
        rail->background = theme.panel;
        rail->align = ui::Align::Center;
        rail->pad_left = 8.0f + safe.left;
        rail->pad_right = 8.0f + safe.right;
        rail->pad_top = 4.0f;
        rail->pad_bottom = 4.0f + safe.bottom;
        if (Registry::Get().Rail().size() > 7) {
            rail->scrollable = true;
        }
        add_rail_entries(rail);
        return content;
    }

    ui::Node* rail = root.Add(ui::MakeBox(ui::Axis::Column, slim ? 6 : 8));
    rail->id = "rail";
    rail->chrome = true;
    rail->width = slim ? SLIM_RAIL_WIDTH : FULL_RAIL_WIDTH;
    rail->background = theme.panel;
    if (slim) {
        // No room for the brand; the entries scroll when the page is short.
        rail->SetPadding(8 + safe.left * 0.4f, 14 + safe.top * 0.5f);
        rail->pad_bottom = 14 + safe.bottom;
        rail->scrollable = true;
        rail->scroll_margin = 14;
    } else {
        rail->pad_left = 16 + safe.left;
        rail->pad_right = 16;
        rail->pad_top = std::max(24.0f, safe.top + 8.0f);
        rail->pad_bottom = std::max(24.0f, safe.bottom + 8.0f);
        ui::Node* brand = rail->Add(ui::MakeBox(ui::Axis::Row, 8));
        brand->align = ui::Align::Center;
        brand->pad_left = 6;
        brand->pad_bottom = 18;
        brand->Emplace<BrandMark>(40.0f);
        ui::Node* word = brand->Add(ui::MakeBox(ui::Axis::Row, 0));
        word->Emplace<ui::Label>("Emu", 26.0f, gfx::FONT_REGULAR);
        word->Emplace<ui::Label>("DEN", 26.0f, gfx::FONT_BOLD);
    }
    add_rail_entries(rail);
    add_content();
    return content;
}

ui::Node* BuildPlainFrame(ui::Page& page, const std::string& title) {
    const ui::Theme& theme = *Svc().theme;
    const Services::SafePadding safe = SafeMargins();
    const bool narrow = SlimRail(PageWidth()) || BottomRail(PageWidth(), PageHeight());
    ui::Node& root = page.Root();
    root.axis = ui::Axis::Column;
    root.gap = 16;
    root.pad_left = theme.margin + safe.left;
    root.pad_right = theme.margin + safe.right;
    root.pad_top = std::max(28.0f, safe.top + 8.0f);
    root.pad_bottom = std::max(28.0f, safe.bottom + 8.0f);
    if (!title.empty()) {
        root.Emplace<ui::Label>(title, narrow ? theme.text_title - 4 : theme.text_title, gfx::FONT_BOLD);
    }
    return &root;
}

ui::Node* BuildLevelFrame(ui::Page& page, const std::string& title) {
    const ui::Theme& theme = *Svc().theme;
    const Services::SafePadding safe = SafeMargins();
    const bool narrow = SlimRail(PageWidth()) || BottomRail(PageWidth(), PageHeight());
    ui::Node& root = page.Root();
    root.axis = ui::Axis::Column;
    root.gap = 16;
    root.pad_left = theme.margin + safe.left;
    root.pad_right = theme.margin + safe.right;
    root.pad_top = std::max(narrow ? 20.0f : 28.0f, safe.top + 8.0f);
    root.pad_bottom = std::max(narrow ? 20.0f : 28.0f, safe.bottom + 8.0f);
    ui::Node* header = root.Add(ui::MakeBox(ui::Axis::Row, 12));
    header->align = ui::Align::Center;
    auto* back = header->Emplace<BackButton>();
    back->on_activate = [&page] {
        if (!page.OnNav(input::Nav::Back)) {
            page.GetStage().Pop();
        }
    };
    header->Emplace<ui::Label>(title, narrow ? theme.text_title - 4 : theme.text_title, gfx::FONT_BOLD);
    return &root;
}

DEN_REGISTER_FRAME(FollowWidth);

} // namespace den::app
