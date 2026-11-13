// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "companion_pictures.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "den/app/app.h"
#include "den/gfx/canvas.h"
#include "den/gfx/text.h"
#include "den/platform/pal.h"
#include "den/ui/theme.h"

namespace den::app {

namespace {

constexpr float CAPTION = 44;
constexpr double RENDER_GAP = 0.05; // seconds between two previews, so the game keeps moving

struct Previews {
    std::string game;
    std::map<std::string, gfx::Texture> textures;
    std::vector<std::string> stale; // presets to make again
    bool drop_all = false;
    double last = 0;
};

Previews& Store() {
    static Previews previews;
    return previews;
}

// Lets go of what is no longer wanted; the canvas's device owns the textures.
void Tidy(gfx::Canvas& canvas) {
    Previews& p = Store();
    if (!p.drop_all && p.stale.empty()) {
        return;
    }
    canvas.Flush();
    for (auto it = p.textures.begin(); it != p.textures.end();) {
        const bool stale = std::find(p.stale.begin(), p.stale.end(), it->first) != p.stale.end();
        if (p.drop_all || stale) {
            canvas.GetDevice().DestroyTexture(it->second);
            it = p.textures.erase(it);
        } else {
            ++it;
        }
    }
    p.stale.clear();
    p.drop_all = false;
    canvas.RestoreState();
}

const gfx::Texture* Preview(gfx::Canvas& canvas, const std::string& preset, int width, int height) {
    Tidy(canvas);
    Previews& p = Store();
    if (const auto it = p.textures.find(preset); it != p.textures.end()) {
        return &it->second;
    }
    const double now = pal::NowSeconds();
    if (now - p.last < RENDER_GAP || !Svc().player) {
        return nullptr;
    }
    gfx::Texture texture = Svc().player->PicturePreview(canvas, preset, width, height);
    p.last = pal::NowSeconds();
    if (!texture) {
        return nullptr;
    }
    return &(p.textures[preset] = texture);
}

void Centred(ui::Context& ctx, const gfx::Rect& rect, const std::string& text, float size, int font,
             gfx::Color color) {
    gfx::Text& t = ctx.Text();
    const float room = rect.w - 20;
    if (const float full = t.Measure(text, size, font); full > room && full > 0) {
        size *= std::max(0.8f, room / full);
    }
    const std::string shown = t.Elide(text, size, room, font);
    const float width = t.Measure(shown, size, font);
    ctx.canvas->DrawText(shown, rect.x + (rect.w - width) * 0.5f,
                         rect.CenterY() - t.Metrics(size, font).line_height * 0.5f, size, color, font);
}

} // namespace

void KeepPicturePreviews(const std::string& game) {
    Previews& p = Store();
    if (p.game != game) {
        p.game = game;
        p.drop_all = true;
    }
}

void ForgetPicturePreview(const std::string& preset) {
    Store().stale.push_back(preset);
}

PictureCard::PictureCard(std::string preset, std::string name, bool in_use, std::function<void()> action)
    : preset_(std::move(preset)), name_(std::move(name)), in_use_(in_use) {
    focusable = true;
    label = name_;
    on_activate = std::move(action);
}

void PictureCard::DrawContent(ui::Context& ctx) {
    const ui::Theme& theme = *ctx.theme;
    ctx.canvas->FillRect(rect, in_use_ || focused ? theme.panel_strong : theme.panel, theme.radius);
    const gfx::Rect area{rect.x + 8, rect.y + 8, rect.w - 16, std::max(0.0f, rect.h - CAPTION - 8)};
    const float s = ctx.canvas->Scale();
    const int width = static_cast<int>(std::lround(area.w * s));
    const int height = static_cast<int>(std::lround(area.h * s));
    const gfx::Texture* texture = width > 0 && height > 0 ? Preview(*ctx.canvas, preset_, width, height) : nullptr;
    if (texture) {
        // Made upside down: a framebuffer's first row is its bottom.
        ctx.canvas->DrawImage(area, *texture, gfx::Color{255, 255, 255, 255}, theme.radius * 0.6f,
                              gfx::Rect{0, 1, 1, -1});
    } else {
        ctx.canvas->FillRect(area, theme.panel, theme.radius * 0.6f);
        Centred(ctx, area, "…", theme.text_body, gfx::FONT_REGULAR, theme.text_dim);
    }
    const gfx::Rect caption{rect.x, area.Bottom(), rect.w, CAPTION};
    Centred(ctx, caption, name_, theme.text_body, in_use_ ? gfx::FONT_BOLD : gfx::FONT_REGULAR, theme.fg);
}

} // namespace den::app
