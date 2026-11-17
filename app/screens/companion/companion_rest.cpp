// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "companion_rest.h"

#include <algorithm>
#include <cmath>

#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/base/files.h"
#include "den/gfx/canvas.h"
#include "den/platform/pal.h"

namespace den::app {

namespace {

constexpr double IDLE_SECONDS = 10;
constexpr float FADE_OUT_SECONDS = 1.2f; // into rest: slow, so it is not noticed
constexpr float FADE_IN_SECONDS = 0.15f; // awake again: at once
constexpr float LOGO_ALPHA = 0.35f;
constexpr gfx::Color REST{4, 5, 8, 255};
const gfx::Color WHITE{255, 255, 255, 255};

bool Enabled() {
    return Svc().settings->GetBool(COMPANION_REST_KEY, true);
}

} // namespace

void CompanionRest::Update(double now, bool can_rest, std::vector<pal::SecondPointer>& pointer) {
    const float dt = static_cast<float>(std::clamp(now - last_frame_, 0.0, 0.1));
    last_frame_ = now;
    if (last_touch_ < 0 || !can_rest || !Enabled()) {
        last_touch_ = now;
    }
    if (!pointer.empty()) {
        last_touch_ = now;
    }
    // A press on a screen that has faded only wakes it: down to up, it reaches nothing.
    std::erase_if(pointer, [&](const pal::SecondPointer& p) {
        if (p.kind == pal::SecondPointer::Kind::Down && amount_ > 0.5f) {
            waking_ = true;
        }
        const bool taken = waking_;
        if (p.kind == pal::SecondPointer::Kind::Up) {
            waking_ = false;
        }
        return taken;
    });
    const bool rest = can_rest && Enabled() && now - last_touch_ >= IDLE_SECONDS;
    amount_ = rest ? std::min(1.0f, amount_ + dt / FADE_OUT_SECONDS)
                   : std::max(0.0f, amount_ - dt / FADE_IN_SECONDS);
}

void CompanionRest::Draw(ui::Context& ctx) const {
    if (amount_ <= 0.0f) {
        return;
    }
    gfx::Canvas& canvas = *ctx.canvas;
    const float W = canvas.Width(), H = canvas.Height();
    const float k = amount_ * amount_ * (3 - 2 * amount_);
    canvas.FillRect(gfx::Rect{0, 0, W, H}, REST.WithAlpha(0.94f * k));

    // The logo and its wordmark, faint, drifting a few points over minutes so nothing stays
    // lit in one place.
    const float alpha = LOGO_ALPHA * k;
    const float side = std::min({H * 0.2f, W * 0.18f, 120.0f});
    const double t = last_frame_ / 90.0;
    const float cx = W * 0.5f + static_cast<float>(std::sin(t)) * side * 0.25f;
    const float cy = H * 0.42f + static_cast<float>(std::sin(t * 0.7 + 1.3)) * side * 0.15f;
    const gfx::Rect icon{cx - side * 0.5f, cy - side * 0.5f, side, side};
    static const std::string path = PathToUtf8(pal::AssetsDir() / "branding" / "icon-256.png");
    if (const gfx::Texture texture = ctx.textures->Get(path, 256)) {
        canvas.DrawImage(icon, texture, WHITE.WithAlpha(alpha));
    }
    // The wordmark, centred on its D, as on the boot screen.
    gfx::Text& text = ctx.Text();
    const float size = side * 0.34f;
    const float emu = text.Measure("Emu", size, gfx::FONT_REGULAR);
    const float d = text.Measure("D", size, gfx::FONT_BOLD);
    const float x = cx - emu - d * 0.5f;
    const float top = icon.Bottom() + side * 0.2f;
    canvas.DrawText("Emu", x, top, size, WHITE.WithAlpha(alpha * 0.82f), gfx::FONT_REGULAR);
    canvas.DrawText("DEN", x + emu, top, size, WHITE.WithAlpha(alpha), gfx::FONT_BOLD);
}

ui::RowSpec CompanionRestRow() {
    ui::RowSpec row = ToggleRow(
        COMPANION_REST_KEY, "Dim when not in use", [] { return Enabled(); },
        [](bool on) { Svc().settings->SetBool(COMPANION_REST_KEY, on); });
    row.description = "While a game plays on the main screen only, the quick settings fade to "
                      "EmuDEN's logo after 10 seconds untouched. A touch brings them back";
    return row;
}

} // namespace den::app
