// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/base/files.h"
#include "den/app/player_view.h"

namespace den::app {

namespace {

namespace fs = std::filesystem;

constexpr int THUMBNAIL_SIDE = 320;

std::string Age(const fs::path& file) {
    std::error_code ec;
    const auto written = fs::last_write_time(file, ec);
    if (ec) {
        return "Saved";
    }
    const auto minutes =
        std::chrono::duration_cast<std::chrono::minutes>(fs::file_time_type::clock::now() - written).count();
    if (minutes < 1) {
        return "Saved just now";
    }
    if (minutes < 60) {
        return TrF("Saved {} min ago", minutes);
    }
    if (minutes < 60 * 48) {
        return TrF("Saved {} h ago", minutes / 60);
    }
    return TrF("Saved {} days ago", minutes / (60 * 24));
}

} // namespace

const std::vector<std::string>& ScreenSizeNames() {
    static const std::vector<std::string> names{"Fit", "Pixel perfect", "4:3", "16:9", "Stretch"};
    return names;
}

Placement Place(ScreenSize size, const gfx::Rect& window, int frame_width, int frame_height,
                float core_aspect, float scale) {
    Placement placement{window, 0};
    switch (size) {
    case ScreenSize::Fit:
        break;
    case ScreenSize::PixelPerfect: {
        if (frame_width <= 0 || frame_height <= 0 || scale <= 0) {
            break;
        }
        // Whole multiples of the core's lines; the width follows the core's aspect ratio.
        const float aspect = core_aspect > 0 ? core_aspect : static_cast<float>(frame_width) / frame_height;
        const float pixel_h = window.h * scale, pixel_w = window.w * scale;
        const float factor = std::floor(std::min(pixel_h / frame_height, pixel_w / (frame_height * aspect)));
        if (factor < 1.0f) {
            break; // smaller than the picture's own pixels: fitted, so it stays inside
        }
        const float h = frame_height * factor / scale, w = h * aspect;
        placement.area = gfx::Rect{window.x + (window.w - w) * 0.5f, window.y + (window.h - h) * 0.5f, w, h};
        placement.aspect = aspect;
        break;
    }
    case ScreenSize::FourThree:
        placement.aspect = 4.0f / 3.0f;
        break;
    case ScreenSize::SixteenNine:
        placement.aspect = 16.0f / 9.0f;
        break;
    case ScreenSize::Stretch:
        placement.aspect = window.h > 0 ? window.w / window.h : 0;
        break;
    }
    return placement;
}

MenuLayout PlaceMenu(const gfx::Rect& area, bool preview) {
    constexpr float MIN_PANEL = 320, MIN_ROOM = 240;
    MenuLayout layout;
    float width = area.w > 0 ? std::min(MENU_PANEL_WIDTH, area.w) : MENU_PANEL_WIDTH;
    layout.room = area;
    if (preview && area.w - width < MIN_ROOM) {
        if (area.w - MIN_PANEL >= MIN_ROOM) {
            width = area.w - MIN_ROOM; // a narrower menu leaves the picture its room
        } else {
            // No room beside the menu: the picture goes above it.
            layout.stacked = true;
            layout.room.h = std::round(std::min(area.w * 0.75f, area.h * 0.4f));
            layout.panel = gfx::Rect{area.x, area.y + layout.room.h, area.w, area.h - layout.room.h};
            return layout;
        }
    }
    layout.panel = gfx::Rect{area.x, area.y, width, area.h};
    if (preview) {
        layout.room = gfx::Rect{area.x + width, area.y, area.w - width, area.h};
    }
    return layout;
}

SlotRow::SlotRow(int slot, bool filled, const fs::path& thumbnail) : filled_(filled) {
    focusable = true;
    height = 76;
    id = std::format("slot.{}", slot);
    title_ = TrF("Slot {}", slot + 1);
    label = title_;
    if (filled && !thumbnail.empty() && IsRegularFile(thumbnail)) {
        thumbnail_ = PathToUtf8(thumbnail);
        detail_ = Age(thumbnail);
    } else {
        detail_ = filled ? "Saved" : "Empty";
    }
}

void SlotRow::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    canvas.FillRect(rect, focused ? theme.panel_strong : theme.panel, theme.radius);
    const gfx::Rect frame{rect.x + 10, rect.y + 8, 80, 60};
    canvas.FillRect(frame, theme.panel_strong, theme.radius_small);
    if (!thumbnail_.empty()) {
        if (const gfx::Texture texture = ctx.textures->Get(thumbnail_, THUMBNAIL_SIDE)) {
            canvas.DrawImage(frame, texture, gfx::Color{255, 255, 255, 255}, theme.radius_small,
                             gfx::CoverUv(texture.width, texture.height, frame));
        }
    }
    const float x = frame.Right() + 16;
    canvas.DrawText(title_, x, rect.y + 14, theme.text_body, theme.fg, gfx::FONT_BOLD);
    canvas.DrawText(detail_, x, rect.y + 42, theme.text_small, filled_ ? theme.fg : theme.text_dim);
}

} // namespace den::app
