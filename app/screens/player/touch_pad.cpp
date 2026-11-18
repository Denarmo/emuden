// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Renders console-specific touch housings & controls, dispatches multi-touch events, and
// provides settings rows and automation inspection JSON.

#include "touch_pad.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/touch_settings.h"
#include "den/platform/lifecycle.h"
#include "den/system/device.h"
#include "den/ui/glyphs.h"

namespace den::app {

namespace {

constexpr const char* TOUCH_MODE_KEY = "player.touch_controls"; // "auto" | "on" | "off"
constexpr const char* TOUCH_OPACITY_KEY = "player.touch_opacity";
constexpr const char* TOUCH_HAPTICS_KEY = "player.touch_haptics";
constexpr float PI = 3.14159265f;

TouchPad* g_touch_pad = nullptr;

int ModeIndex(std::string_view mode) {
    if (mode == "on") return 1;
    if (mode == "off") return 2;
    return 0;
}

gfx::Color ScaleRgb(gfx::Color c, float factor, float alpha) {
    const auto scale_ch = [&](std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<float>(v) * factor, 0.0f, 255.0f));
    };
    return gfx::Color{scale_ch(c.r), scale_ch(c.g), scale_ch(c.b),
                      static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)};
}

gfx::Color LiftRgb(gfx::Color c, int delta, float alpha) {
    const auto shift = [&](std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(v) + delta, 0, 255));
    };
    return gfx::Color{shift(c.r), shift(c.g), shift(c.b),
                      static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)};
}

int Luminance(gfx::Color c) {
    return (static_cast<int>(c.r) * 299 + static_cast<int>(c.g) * 587 + static_cast<int>(c.b) * 114) / 1000;
}

void DrawOctagon(gfx::Canvas& canvas, float cx, float cy, float r, float thickness, gfx::Color color) {
    for (int i = 0; i < 8; ++i) {
        const float a0 = (static_cast<float>(i) + 0.5f) * (PI * 0.25f);
        const float a1 = (static_cast<float>(i) + 1.5f) * (PI * 0.25f);
        ui::DrawLine(canvas, cx + std::cos(a0) * r, cy + std::sin(a0) * r,
                     cx + std::cos(a1) * r, cy + std::sin(a1) * r, thickness, color);
    }
}

void DrawButtonSymbol(gfx::Canvas& canvas, ButtonGlyph glyph, const gfx::Rect& r, gfx::Color color) {
    const float cx = r.CenterX();
    const float cy = r.CenterY();
    const float rad = std::min(r.w, r.h) * 0.24f;
    const float stroke = std::max(2.0f, rad * 0.30f);
    switch (glyph) {
    case ButtonGlyph::PsCross:
        ui::DrawLine(canvas, cx - rad, cy - rad, cx + rad, cy + rad, stroke, color);
        ui::DrawLine(canvas, cx - rad, cy + rad, cx + rad, cy - rad, stroke, color);
        break;
    case ButtonGlyph::PsCircle:
        canvas.StrokeRect({cx - rad, cy - rad, rad * 2.0f, rad * 2.0f}, color, stroke, rad);
        break;
    case ButtonGlyph::PsSquare:
        canvas.StrokeRect({cx - rad * 0.88f, cy - rad * 0.88f, rad * 1.76f, rad * 1.76f}, color, stroke, 2.0f);
        break;
    case ButtonGlyph::PsTriangle: {
        const float top_y = cy - rad * 0.92f;
        const float bot_y = cy + rad * 0.78f;
        const float half_w = rad * 0.96f;
        ui::DrawLine(canvas, cx, top_y, cx + half_w, bot_y, stroke, color);
        ui::DrawLine(canvas, cx + half_w, bot_y, cx - half_w, bot_y, stroke, color);
        ui::DrawLine(canvas, cx - half_w, bot_y, cx, top_y, stroke, color);
        break;
    }
    case ButtonGlyph::ArrowUp:
        ui::DrawLine(canvas, cx - rad * 0.7f, cy + rad * 0.35f, cx, cy - rad * 0.45f, stroke, color);
        ui::DrawLine(canvas, cx, cy - rad * 0.45f, cx + rad * 0.7f, cy + rad * 0.35f, stroke, color);
        break;
    case ButtonGlyph::ArrowDown:
        ui::DrawLine(canvas, cx - rad * 0.7f, cy - rad * 0.35f, cx, cy + rad * 0.45f, stroke, color);
        ui::DrawLine(canvas, cx, cy + rad * 0.45f, cx + rad * 0.7f, cy - rad * 0.35f, stroke, color);
        break;
    case ButtonGlyph::ArrowLeft:
        ui::DrawLine(canvas, cx + rad * 0.35f, cy - rad * 0.7f, cx - rad * 0.45f, cy, stroke, color);
        ui::DrawLine(canvas, cx - rad * 0.45f, cy, cx + rad * 0.35f, cy + rad * 0.7f, stroke, color);
        break;
    case ButtonGlyph::ArrowRight:
        ui::DrawLine(canvas, cx - rad * 0.35f, cy - rad * 0.7f, cx + rad * 0.45f, cy, stroke, color);
        ui::DrawLine(canvas, cx + rad * 0.45f, cy, cx - rad * 0.35f, cy + rad * 0.7f, stroke, color);
        break;
    case ButtonGlyph::None:
        break;
    }
}

void DrawDPadControl(gfx::Canvas& canvas, const PlacedDPad& pad, std::uint8_t mask, float alpha) {
    if (pad.style == DPadStyle::None || pad.radius <= 0.0f) {
        return;
    }
    const float cx = pad.cx;
    const float cy = pad.cy;
    const float r = pad.radius;
    const gfx::Color base = ScaleRgb(pad.fill, 1.0f, alpha);
    const gfx::Color dark = ScaleRgb(pad.fill, 0.58f, alpha * 0.90f);
    const gfx::Color rim = LiftRgb(pad.fill, 26, alpha * 0.72f);
    const gfx::Color lit = LiftRgb(pad.fill, 70, alpha);

    // Subtle circular recess well.
    canvas.FillRect({cx - r * 0.98f, cy - r * 0.98f, r * 1.96f, r * 1.96f},
                    ScaleRgb(pad.fill, 0.36f, alpha * 0.38f), r * 0.98f);

    if (pad.style == DPadStyle::Disc) {
        canvas.FillRect({cx - r * 0.86f, cy - r * 0.86f, r * 1.72f, r * 1.72f}, base, r * 0.86f);
        canvas.StrokeRect({cx - r * 0.86f, cy - r * 0.86f, r * 1.72f, r * 1.72f}, rim, 2.0f, r * 0.86f);
        const float aw = r * 0.34f;
        canvas.FillRect({cx - aw * 0.5f, cy - r * 0.66f, aw, r * 1.32f}, dark, aw * 0.3f);
        canvas.FillRect({cx - r * 0.66f, cy - aw * 0.5f, r * 1.32f, aw}, dark, aw * 0.3f);
    } else if (pad.style == DPadStyle::Split) {
        const float kw = r * 0.44f;
        const float kh = r * 0.48f;
        const float off = r * 0.50f;
        canvas.FillRect({cx - kw * 0.5f, cy - off - kh * 0.5f, kw, kh}, (mask & 1u) ? lit : base, 5.0f);
        canvas.FillRect({cx - kw * 0.5f, cy + off - kh * 0.5f, kw, kh}, (mask & 2u) ? lit : base, 5.0f);
        canvas.FillRect({cx - off - kh * 0.5f, cy - kw * 0.5f, kh, kw}, (mask & 4u) ? lit : base, 5.0f);
        canvas.FillRect({cx + off - kh * 0.5f, cy - kw * 0.5f, kh, kw}, (mask & 8u) ? lit : base, 5.0f);
        return;
    } else {
        const float arm = r * 0.54f;
        const float span = r * 1.76f;
        canvas.FillRect({cx - arm * 0.5f, cy - span * 0.5f + 2.0f, arm, span}, dark, 7.0f);
        canvas.FillRect({cx - span * 0.5f, cy - arm * 0.5f + 2.0f, span, arm}, dark, 7.0f);
        canvas.FillRect({cx - arm * 0.5f, cy - span * 0.5f, arm, span}, base, 7.0f);
        canvas.FillRect({cx - span * 0.5f, cy - arm * 0.5f, span, arm}, base, 7.0f);
    }

    const float pad_w = r * 0.38f;
    const float shift = r * 0.56f;
    if (mask & 1u) canvas.FillRect({cx - pad_w * 0.5f, cy - shift - pad_w * 0.5f, pad_w, pad_w}, lit, 4.0f);
    if (mask & 2u) canvas.FillRect({cx - pad_w * 0.5f, cy + shift - pad_w * 0.5f, pad_w, pad_w}, lit, 4.0f);
    if (mask & 4u) canvas.FillRect({cx - shift - pad_w * 0.5f, cy - pad_w * 0.5f, pad_w, pad_w}, lit, 4.0f);
    if (mask & 8u) canvas.FillRect({cx + shift - pad_w * 0.5f, cy - pad_w * 0.5f, pad_w, pad_w}, lit, 4.0f);

    // Center pivot concave dish.
    const float dish = r * 0.32f;
    canvas.FillRect({cx - dish * 0.5f, cy - dish * 0.5f, dish, dish}, dark, dish * 0.5f);
}

void DrawStickControl(gfx::Canvas& canvas, const PlacedStick& stick, float ox, float oy, float alpha) {
    if (stick.style == StickStyle::None || stick.well_r <= 0.0f) {
        return;
    }
    const float cx = stick.cx;
    const float cy = stick.cy;
    const float wr = stick.well_r;
    const gfx::Color well = ScaleRgb(stick.fill, 0.42f, alpha * 0.58f);
    const gfx::Color ring = LiftRgb(stick.fill, 30, alpha * 0.75f);

    canvas.FillRect({cx - wr, cy - wr, wr * 2.0f, wr * 2.0f}, well, wr);
    if (stick.style == StickStyle::Octagon) {
        DrawOctagon(canvas, cx, cy, wr * 0.94f, 2.0f, ring);
    } else {
        canvas.StrokeRect({cx - wr, cy - wr, wr * 2.0f, wr * 2.0f}, ring, 2.0f, wr);
    }

    const float travel = wr * 0.62f;
    const float px = cx + ox * travel;
    const float py = cy + oy * travel;
    const float cr = stick.cap_r;
    const bool active = std::hypot(ox, oy) > 0.08f;

    canvas.FillRect({px - cr, py - cr + 2.0f, cr * 2.0f, cr * 2.0f},
                    ScaleRgb(stick.fill, 0.38f, alpha * 0.85f), cr);
    canvas.FillRect({px - cr, py - cr, cr * 2.0f, cr * 2.0f},
                    LiftRgb(stick.fill, active ? 32 : 8, alpha), cr);
    canvas.StrokeRect({px - cr, py - cr, cr * 2.0f, cr * 2.0f},
                      LiftRgb(stick.fill, 54, alpha * 0.8f), 1.5f, cr);
    if (stick.style == StickStyle::ArcadeBall) {
        const float shine = cr * 0.30f;
        canvas.FillRect({px - cr * 0.45f, py - cr * 0.45f, shine, shine},
                        gfx::Color{255, 255, 255, static_cast<std::uint8_t>(145 * alpha)}, shine * 0.5f);
    } else {
        const float inner = cr * 0.64f;
        canvas.StrokeRect({px - inner, py - inner, inner * 2.0f, inner * 2.0f},
                          ScaleRgb(stick.fill, 0.72f, alpha * 0.65f), 1.5f, inner);
    }
}

void DrawActionButton(ui::Context& ctx, const PlacedButton& b, bool down, bool highlighted, float alpha) {
    gfx::Canvas& canvas = *ctx.canvas;
    const gfx::Rect& r = b.rect;

    if (b.shape == ButtonShape::SquareFramed) {
        const gfx::Rect frame = r.Inset(-3.0f);
        canvas.FillRect(frame, ScaleRgb(gfx::Color::Rgb(0xD2D2CC), 0.92f, alpha), 6.0f);
    }

    const float press_y = down ? 1.5f : 0.0f;
    const gfx::Rect face{r.x, r.y + press_y, r.w, r.h};
    const float radius = b.shape == ButtonShape::Circle || b.shape == ButtonShape::SquareFramed
                             ? std::min(r.w, r.h) * 0.5f
                             : (b.shape == ButtonShape::Bumper ? 8.0f : r.h * 0.5f);

    const gfx::Color base = highlighted ? gfx::Color::Rgb(0x3B78D8) : b.fill;
    canvas.FillRect({r.x, r.y + 2.5f, r.w, r.h}, ScaleRgb(base, 0.45f, alpha * 0.9f), radius);
    canvas.FillRect(face, LiftRgb(base, down ? 40 : 0, alpha), radius);
    canvas.StrokeRect(face, LiftRgb(base, down ? 75 : 28, alpha * 0.75f), 1.5f, radius);

    const gfx::Color ink = LiftRgb(b.label_color, down ? 24 : 0, alpha);
    if (b.glyph != ButtonGlyph::None) {
        DrawButtonSymbol(canvas, b.glyph, face, ink);
    } else if (!b.label.empty()) {
        const float font_sz = std::clamp(std::min(r.h * 0.48f, r.w * 0.36f), 10.0f, 19.0f);
        const float tw = ctx.Text().Measure(b.label, font_sz, gfx::FONT_BOLD);
        canvas.DrawText(b.label, face.CenterX() - tw * 0.5f, face.CenterY() - font_sz * 0.62f, font_sz, ink,
                        gfx::FONT_BOLD);
    }
}

} // namespace

TouchPad::TouchPad() {
    g_touch_pad = this;
}

TouchPad::~TouchPad() {
    if (g_touch_pad == this) {
        g_touch_pad = nullptr;
    }
}

bool TouchPad::ShouldShow() const {
    const std::string mode = Svc().settings->GetString(TOUCH_MODE_KEY, "auto");
    if (mode == "on") return true;
    if (mode == "off") return false;
    if (system::Device().info.builtin_pad && Svc().input->PadCount() > 0) return false;
    if (Svc().input->TouchOnly()) return true;
    return system::Device().info.touch && touch_woken_;
}

void TouchPad::EnsureLayout(std::string_view system_id, const gfx::Rect& area, bool dual_screen,
                            bool can_fast_forward) {
    const Services::SafePadding safe = SafeMargins();
    const bool same = system_id == system_id_ && area.x == last_area_.x && area.y == last_area_.y &&
                      area.w == last_area_.w && area.h == last_area_.h && safe.left == safe_l_ &&
                      safe.top == safe_t_ && safe.right == safe_r_ && safe.bottom == safe_b_ &&
                      dual_screen == last_dual_ && can_fast_forward == last_ff_cap_;
    if (same) {
        return;
    }
    system_id_ = std::string(system_id);
    last_area_ = area;
    safe_l_ = safe.left;
    safe_t_ = safe.top;
    safe_r_ = safe.right;
    safe_b_ = safe.bottom;
    last_dual_ = dual_screen;
    last_ff_cap_ = can_fast_forward;

    const TouchConsoleSpec& spec = TouchSpecForSystem(system_id_);
    layout_ = LayoutTouchDeck(spec, {area, safe.left, safe.top, safe.right, safe.bottom, dual_screen,
                                     can_fast_forward});
    controller_.SetLayout(layout_);
}

void TouchPad::Sync(std::string_view system_id, const gfx::Rect& area, bool dual_screen, bool can_fast_forward,
                    bool fast_forward, bool playing) {
    playing_ = playing;
    fast_forward_ = fast_forward;
    if (!Svc().input->TouchOnly() && Svc().input->PadCount() > 0 && Svc().input->Pad(0).buttons != 0) {
        touch_woken_ = false;
    }
    visible_ = playing && ShouldShow();
    EnsureLayout(system_id, area, dual_screen, can_fast_forward);
    if (!visible_) {
        controller_.ReleaseAll();
    }
}

void TouchPad::Stop() {
    controller_.ReleaseAll();
    touch_woken_ = false;
    visible_ = false;
}

player::TouchState TouchPad::State() const {
    if (!visible_ || !playing_) {
        return {};
    }
    const TouchPadState s = controller_.State();
    return {s.buttons, s.lx, s.ly, s.rx, s.ry, s.lt, s.rt};
}

TouchPad::InputAction TouchPad::HandleEvent(const input::Event& event, float scale) {
    using Type = input::Event::Type;
    InputAction out;
    if (!playing_) {
        return out;
    }
    if (event.type == Type::FingerDown) {
        touch_woken_ = true;
        visible_ = ShouldShow();
    }
    if (!visible_) {
        return out;
    }

    const float x = event.x / scale;
    const float y = event.y / scale;
    const std::uint64_t fid =
        (event.type == Type::FingerDown || event.type == Type::FingerMove || event.type == Type::FingerUp)
            ? event.finger
            : 0xFFFFFFFFFFFFFFFDULL;

    TouchController::StepResult step;
    if (event.type == Type::FingerDown || event.type == Type::PointerDown) {
        step = controller_.FingerDown(fid, x, y);
    } else if (event.type == Type::FingerMove || event.type == Type::PointerMove) {
        step = controller_.FingerMove(fid, x, y);
    } else if (event.type == Type::FingerUp || event.type == Type::PointerUp) {
        step = controller_.FingerUp(fid, x, y);
    } else {
        return out;
    }

    if (step.haptic && Svc().settings->GetBool(TOUCH_HAPTICS_KEY, true)) {
        pal::HapticTick();
    }
    out.consumed = step.consumed || (PortraitActive(last_area_) && layout_.deck_rect.Contains(x, y));
    out.open_menu = step.utility == TouchController::UtilityHit::Menu;
    out.toggle_fast = step.utility == TouchController::UtilityHit::FastForward;
    out.swap_screens = step.utility == TouchController::UtilityHit::Swap;
    return out;
}

void TouchPad::DrawShell(ui::Context& ctx) const {
    if (!visible_ || !layout_.portrait) {
        return;
    }
    gfx::Canvas& canvas = *ctx.canvas;
    const gfx::Rect& bezel = layout_.bezel_rect;
    const gfx::Rect& deck = layout_.deck_rect;

    canvas.FillRect(bezel, layout_.bezel_bg);
    canvas.StrokeRect(layout_.game_room.Inset(-3.0f), gfx::Color::Rgb(0x0A0B0E), 3.0f, 8.0f);
    canvas.FillRect(deck, layout_.shell_bg);
    canvas.FillRect({deck.x, deck.y, deck.w, 3.0f}, LiftRgb(layout_.shell_bg, 22, 1.0f));

    if (layout_.accent == ShellAccent::WoodgrainStrip) {
        canvas.FillRect({deck.x, deck.y + 3.0f, deck.w, 10.0f}, layout_.shell_trim);
    } else if (layout_.accent == ShellAccent::MetalStrip) {
        canvas.FillRect({deck.x, deck.y + 3.0f, deck.w, 6.0f}, layout_.shell_trim);
    } else if (layout_.accent == ShellAccent::PinstripeRed) {
        canvas.FillRect({deck.x, deck.y + 4.0f, deck.w, 3.0f}, layout_.shell_trim);
    } else if (layout_.accent == ShellAccent::PspRing) {
        canvas.StrokeRect({deck.x + 8.0f, deck.y + 5.0f, deck.w - 16.0f, deck.h - 10.0f},
                          layout_.shell_trim.WithAlpha(0.35f), 2.0f, 16.0f);
    } else if (layout_.accent == ShellAccent::GbSpeakerSlots) {
        for (int i = 0; i < 5; ++i) {
            const float sx = deck.Right() - 62.0f + static_cast<float>(i) * 9.0f;
            const float sy = deck.Bottom() - 26.0f;
            ui::DrawLine(canvas, sx, sy + 12.0f, sx + 7.0f, sy, 3.0f,
                         ScaleRgb(layout_.shell_bg, 0.55f, 0.85f));
        }
    } else if (layout_.accent == ShellAccent::DsSpeakerDots) {
        for (int side = 0; side < 2; ++side) {
            const float bx = side == 0 ? deck.x + 18.0f : deck.Right() - 40.0f;
            const float by = deck.y + 42.0f;
            for (int r = 0; r < 2; ++r) {
                for (int c = 0; c < 3; ++c) {
                    canvas.FillRect({bx + c * 6.0f, by + r * 6.0f, 3.0f, 3.0f},
                                    ScaleRgb(layout_.shell_bg, 0.52f, 0.7f), 1.5f);
                }
            }
        }
    } else if (layout_.accent == ShellAccent::SwirlDot) {
        canvas.FillRect({deck.x + 14.0f, deck.y + 15.0f, 8.0f, 8.0f}, layout_.shell_trim, 4.0f);
    }

    const float pad_x = std::max(14.0f, safe_l_ + 8.0f) +
                        (layout_.accent == ShellAccent::SwirlDot ? 14.0f : 0.0f);
    float text_x = deck.x + pad_x;
    if (layout_.power_led) {
        canvas.FillRect({text_x, deck.y + 17.0f, 7.0f, 7.0f}, gfx::Color::Rgb(0x34D462), 3.5f);
        text_x += 13.0f;
    }
    const gfx::Color badge_ink = Luminance(layout_.shell_bg) > 140
                                     ? gfx::Color::Rgb(0x3A3E48)
                                     : gfx::Color::Rgb(0xC8CED8);
    canvas.DrawText(layout_.display_badge, text_x, deck.y + 13.0f, 12.0f, badge_ink, gfx::FONT_BOLD);
}

void TouchPad::DrawControls(ui::Context& ctx, bool black_frame) const {
    if (!visible_ || black_frame) {
        return;
    }
    gfx::Canvas& canvas = *ctx.canvas;
    const float setting_alpha =
        std::clamp(static_cast<float>(Svc().settings->GetNumber(TOUCH_OPACITY_KEY, 0.78)), 0.2f, 1.0f);
    const float alpha = layout_.portrait ? std::max(0.88f, setting_alpha) : setting_alpha;

    DrawDPadControl(canvas, layout_.left_dpad, controller_.DPadMask(false), alpha);
    DrawDPadControl(canvas, layout_.right_dpad, controller_.DPadMask(true), alpha);

    const auto [lx, ly] = controller_.StickOffset(false);
    DrawStickControl(canvas, layout_.left_stick, lx, ly, alpha);
    const auto [rx, ry] = controller_.StickOffset(true);
    DrawStickControl(canvas, layout_.right_stick, rx, ry, alpha);

    for (std::size_t i = 0; i < layout_.buttons.size(); ++i) {
        const PlacedButton& b = layout_.buttons[i];
        const bool hl = fast_forward_ && b.kind == TouchTargetKind::UtilityFastForward;
        DrawActionButton(ctx, b, controller_.ButtonDown(i), hl, alpha);
    }
}

std::string TouchPad::Json() const {
    std::string btns;
    for (std::size_t i = 0; i < layout_.buttons.size(); ++i) {
        const PlacedButton& b = layout_.buttons[i];
        btns += std::format("{}{{\"id\":\"{}\",\"x\":{:.1f},\"y\":{:.1f},\"w\":{:.1f},\"h\":{:.1f},\"down\":{}}}",
                            i ? "," : "", b.id, b.rect.x, b.rect.y, b.rect.w, b.rect.h,
                            controller_.ButtonDown(i));
    }
    const auto box_json = [](const char* key, const gfx::Rect& r) {
        return std::format("\"{}\":{{\"x\":{:.1f},\"y\":{:.1f},\"w\":{:.1f},\"h\":{:.1f}}}",
                           key, r.x, r.y, r.w, r.h);
    };
    return std::format("{{\"visible\":{},\"portrait\":{},\"system\":\"{}\",{},{},\"fingers\":{},\"buttons\":[{}]}}",
                       visible_, layout_.portrait, layout_.system_id, box_json("game_room", layout_.game_room),
                       box_json("left_dpad", layout_.left_dpad.bounds), controller_.ActiveFingerCount(), btns);
}

ui::RowSpec TouchModeRow() {
    static constexpr const char* MODES[] = {"auto", "on", "off"};
    return ChoiceRow(
        TOUCH_MODE_KEY, "On-screen controls", {"Auto", "Always on", "Off"},
        [] { return ModeIndex(Svc().settings->GetString(TOUCH_MODE_KEY, "auto")); },
        [](int index) { Svc().settings->SetString(TOUCH_MODE_KEY, MODES[std::clamp(index, 0, 2)]); });
}

std::vector<ui::RowSpec> TouchSettingRows() {
    std::vector<ui::RowSpec> rows;
    rows.push_back(TouchModeRow());
    rows.push_back(SliderRow(
        TOUCH_OPACITY_KEY, "Touch control opacity", 20.0f, 100.0f, 5.0f,
        [] { return static_cast<float>(Svc().settings->GetNumber(TOUCH_OPACITY_KEY, 0.78)) * 100.0f; },
        [](float pct) { Svc().settings->SetNumber(TOUCH_OPACITY_KEY, pct / 100.0f); },
        [](float pct) { return std::format("{}%", static_cast<int>(pct + 0.5f)); }));
    rows.push_back(ToggleRow(
        TOUCH_HAPTICS_KEY, "Touch vibration",
        [] { return Svc().settings->GetBool(TOUCH_HAPTICS_KEY, true); },
        [](bool on) { Svc().settings->SetBool(TOUCH_HAPTICS_KEY, on); }));
    return rows;
}

std::string TouchPadJson() {
    return g_touch_pad ? g_touch_pad->Json() : "null";
}

} // namespace den::app
