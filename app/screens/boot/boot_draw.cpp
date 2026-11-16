// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "boot_draw.h"
#include "den/app/shelf_parts.h"
#include "boot_media.h"
#include "den/app/app.h"
#include "den/base/files.h"
#include "den/gfx/canvas.h"

namespace den::app {

namespace {

constexpr float PI = 3.14159265f;
constexpr gfx::Color BACKGROUND{11, 13, 20, 255};
constexpr gfx::Color PLATE{44, 48, 59, 255};
constexpr gfx::Color PLATE_EDGE{86, 92, 110, 255};
constexpr gfx::Color SLIT{4, 5, 7, 255};
constexpr gfx::Color BRAND{0, 0, 255, 255};
constexpr gfx::Color WHITE{255, 255, 255, 255};

constexpr BootTimes ANIMATED{0.28f, 0.52f, 0.68f, 0.88f, 1.21f, 1.36f, 1.85f, 1.42f};
constexpr BootTimes PLAIN{0, 0, 0, 0.18f, 1e9f, 0.18f, 0.18f, 0.18f};

// 0 before `a`, 1 after `b`, eased between.
float Ease(float t, float a, float b) {
    const float x = std::clamp((t - a) / std::max(0.001f, b - a), 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Past the end a little, then back: a landing.
float EaseOutBack(float s) {
    constexpr float c1 = 1.25f, c3 = c1 + 1;
    const float x = std::clamp(s, 0.0f, 1.0f) - 1;
    return 1 + c3 * x * x * x + c1 * x * x;
}

} // namespace

void BootDraw::Begin(const library::Entry& entry, bool animated, std::optional<gfx::Rect> from, float) {
    game = entry;
    animate = animated;
    t = 0;
    from_ = from;
    media_ = MediaFor(game);
    faces_ = std::make_unique<FaceLoader>();
    shaped_ = false;
}

const BootTimes& BootDraw::Times() const {
    return animate ? ANIMATED : PLAIN;
}

float BootDraw::Cover() const {
    const BootTimes& times = Times();
    if (!animate) {
        return Ease(t, 0, times.plain);
    }
    if (t < times.closed) {
        return 0.8f * Ease(t, 0, times.lifted);
    }
    return Lerp(0.8f, 1.0f, Ease(t, times.closed, times.plain));
}

void BootDraw::Main(ui::Context& ctx, float out) {
    gfx::Canvas& canvas = *ctx.canvas;
    const float shown = 1 - Ease(out, 0, 1);
    canvas.FillRect(gfx::Rect{0, 0, canvas.Width(), canvas.Height()}, BACKGROUND.WithAlpha(Cover() * shown));
    const BootTimes& times = Times();
    if (animate && t < times.plain && out <= 0) {
        DrawDrop(ctx);
    }
    if (animate) {
        DrawLogo(ctx, shown);
    }
    // A start that takes long says what it is waiting for.
    if (const float wait = Ease(t, times.logo_end + 3.5f, times.logo_end + 4.0f) * shown; wait > 0) {
        const std::string text = "Starting " + game.name + "\xE2\x80\xA6";
        const float size = ctx.theme->text_body;
        const float w = ctx.Text().Measure(text, size);
        canvas.DrawText(text, canvas.Width() * 0.5f - w * 0.5f, canvas.Height() * 0.82f, size,
                        ctx.theme->text_dim.WithAlpha(wait));
    }
}

void BootDraw::Second(ui::Context& ctx, float out) const {
    gfx::Canvas& canvas = *ctx.canvas;
    const float shown = 1 - Ease(out, 0, 1);
    canvas.FillRect(gfx::Rect{0, 0, canvas.Width(), canvas.Height()}, BACKGROUND.WithAlpha(Cover() * shown));
}

void BootDraw::DrawDrop(ui::Context& ctx) {
    gfx::Canvas& canvas = *ctx.canvas;
    const BootTimes& times = Times();
    const float W = canvas.Width(), H = canvas.Height();
    const float aspect = MediaAspect(media_);
    const float media_h = std::min(H * 0.4f, W * 0.32f / std::max(0.2f, aspect));
    const float cx = W * 0.5f;
    const float bump = t > times.dropped && t < times.dropped + 0.12f ? std::sin(PI * (t - times.dropped) / 0.12f) * 3 : 0;
    const float slot_y = H * 0.7f + bump;
    const float slot_w = media_h * aspect * 1.12f;

    // The slot: a plate with a slit, coming up under the media.
    const float plate = Ease(t, 0.06f, times.lifted) * (1 - Ease(t, times.closed, times.plain));
    const gfx::Rect body{cx - slot_w * 0.5f - 46, slot_y - 26, slot_w + 92, 74};
    canvas.FillRect(body, PLATE.WithAlpha(plate), 22);
    canvas.StrokeRect(body, PLATE_EDGE.WithAlpha(plate), 1.5f, 22);
    const gfx::Rect slit{cx - slot_w * 0.5f, slot_y - 5, slot_w, 10};
    canvas.FillRect(slit, SLIT.WithAlpha(plate), 5);

    // The media: up from where the game was, over the slot, then down into it.
    if (t < times.dropped) {
        const float hover = slot_y - media_h * 0.06f;
        float x0 = cx, b0 = slot_y - media_h * 0.4f, h0 = media_h * 0.6f;
        if (from_) {
            x0 = from_->CenterX();
            b0 = from_->Bottom();
            h0 = std::max(20.0f, from_->h);
        }
        const float u = Ease(t, 0, times.lifted);
        const float fall = std::clamp((t - times.lifted) / (times.dropped - times.lifted), 0.0f, 1.0f);
        MediaPose pose;
        pose.x = Lerp(x0, cx, u);
        pose.height = Lerp(h0, media_h, u);
        pose.bottom = Lerp(b0, hover, u) - std::sin(PI * u) * H * 0.08f + fall * fall * (media_h * 1.12f);
        pose.yaw = from_ ? 0.45f * (1 - u) : 0;
        pose.spin = -t * (5 + 14 * fall);
        pose.cut = slot_y;
        pose.alpha = from_ ? 1 : Ease(t, 0, 0.12f);
        const LoadedFaces* faces = faces_->Get(game.id);
        if (faces != nullptr && !shaped_) {
            // Its own photographed cartridge, or a hack's, once the pictures are in.
            shaped_ = true;
            if (std::optional<GameObject> own = OwnObject(media_, *faces)) {
                media_ = std::move(*own);
            }
        }
        DrawMedia(ctx, game, media_, faces, pose);
    }

    // The slit closes over it, and the light beside it comes on.
    const float close = Ease(t, times.dropped, times.closed);
    if (close > 0) {
        const float half = slot_w * 0.5f * close;
        canvas.FillRect(gfx::Rect{slit.x, slit.y, half, slit.h}, PLATE.WithAlpha(plate), 5);
        canvas.FillRect(gfx::Rect{slit.Right() - half, slit.y, half, slit.h}, PLATE.WithAlpha(plate), 5);
        canvas.FillRect(gfx::Rect{cx - 0.75f, slit.y + 1, 1.5f, slit.h - 2},
                        PLATE_EDGE.WithAlpha(plate * Ease(t, times.closed - 0.04f, times.closed)));
    }
    const float led = Ease(t, times.closed - 0.06f, times.closed + 0.02f) * plate;
    canvas.FillRect(gfx::Rect{body.Right() - 30, slot_y - 3, 6, 6}, BRAND.WithAlpha(led), 3);
}

void BootDraw::DrawLogo(ui::Context& ctx, float alpha) {
    const BootTimes& times = Times();
    const float start = times.plain - 0.04f;
    if (t < start || alpha <= 0.01f) {
        return;
    }
    gfx::Canvas& canvas = *ctx.canvas;
    const float W = canvas.Width(), H = canvas.Height();
    const float side = std::min({H * 0.24f, W * 0.2f, 200.0f});
    const float cx = W * 0.5f;
    const float y = Lerp(-side * 0.6f, H * 0.42f, EaseOutBack((t - start) / (times.landed - start)));
    const gfx::Rect icon{cx - side * 0.5f, y - side * 0.5f, side, side};
    static const std::string path = PathToUtf8(pal::AssetsDir() / "branding" / "icon-256.png");
    if (const gfx::Texture texture = ctx.textures->Get(path, 256)) {
        canvas.DrawImage(icon, texture, WHITE.WithAlpha(alpha));
    } else {
        canvas.FillRect(icon, BRAND.WithAlpha(alpha), side * 0.22f);
    }
    // A glint crosses it as it lands.
    if (const float k = (t - times.landed) / 0.45f; k > 0 && k < 1) {
        canvas.PushClip(gfx::Rect{icon.x + side * 0.06f, icon.y + side * 0.06f, side * 0.88f, side * 0.88f});
        const float band = side * 0.16f;
        canvas.FillRect(gfx::Rect{icon.x - band + k * (side + band), icon.y, band, side},
                        WHITE.WithAlpha(0.3f * std::sin(PI * k) * alpha), band * 0.5f);
        canvas.PopClip();
    }

    // The wordmark, centred on its D: the letter the logo is.
    const float shown = Ease(t, times.landed - 0.05f, times.landed + 0.25f) * alpha;
    if (shown <= 0.01f) {
        return;
    }
    gfx::Text& text = ctx.Text();
    const float size = side * 0.34f;
    const float emu = text.Measure("Emu", size, gfx::FONT_REGULAR);
    const float d = text.Measure("D", size, gfx::FONT_BOLD);
    const float x = cx - emu - d * 0.5f;
    const float top = icon.Bottom() + side * 0.2f + (1 - shown) * 10;
    canvas.DrawText("Emu", x, top, size, WHITE.WithAlpha(shown * 0.82f), gfx::FONT_REGULAR);
    canvas.DrawText("DEN", x + emu, top, size, WHITE.WithAlpha(shown), gfx::FONT_BOLD);
}

} // namespace den::app
