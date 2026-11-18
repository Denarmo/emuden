// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Portrait deck-below-screen and landscape translucent-overlay layout solver for TouchConsoleSpec.

#include <algorithm>
#include <cmath>

#include "touch_spec.h"

namespace den::app {

namespace {

int Luminance(gfx::Color c) {
    return (static_cast<int>(c.r) * 299 + static_cast<int>(c.g) * 587 + static_cast<int>(c.b) * 114) / 1000;
}

gfx::Color UtilityFill(const TouchConsoleSpec& spec, bool portrait) {
    if (!portrait) {
        return gfx::Color::Rgb(0x161920);
    }
    return Luminance(spec.shell_bg) > 140 ? gfx::Color::Rgb(0x363A44) : gfx::Color::Rgb(0x2F3440);
}

gfx::Rect ClampInside(gfx::Rect r, const gfx::Rect& bounds) {
    r.w = std::min(r.w, bounds.w);
    r.h = std::min(r.h, bounds.h);
    r.x = std::clamp(r.x, bounds.x, bounds.Right() - r.w);
    r.y = std::clamp(r.y, bounds.y, bounds.Bottom() - r.h);
    return r;
}

void PlaceDPad(PlacedDPad& out, float cx, float cy, float radius, DPadStyle style, gfx::Color fill,
               bool drives_right_stick) {
    out.style = style;
    out.cx = cx;
    out.cy = cy;
    out.radius = radius;
    out.fill = fill;
    out.drives_right_stick = drives_right_stick;
    out.bounds = {cx - radius, cy - radius, radius * 2.0f, radius * 2.0f};
}

void PlaceStick(PlacedStick& out, float cx, float cy, float well_r, StickStyle style, gfx::Color fill,
                bool right) {
    out.style = style;
    out.cx = cx;
    out.cy = cy;
    out.well_r = well_r;
    out.cap_r = well_r * (style == StickStyle::ArcadeBall ? 0.56f : 0.62f);
    out.fill = fill;
    out.right = right;
    out.bounds = {cx - well_r, cy - well_r, well_r * 2.0f, well_r * 2.0f};
}

void AddUtilityCluster(TouchDeckLayout& out, const TouchConsoleSpec& spec, const TouchLayoutInput& in,
                       float x_right, float y, bool centered, float center_x) {
    const gfx::Color fill = UtilityFill(spec, out.portrait);
    const gfx::Color ink = gfx::Color::Rgb(0xF5F7FB);

    struct Item {
        const char* id;
        const char* label;
        TouchTargetKind kind;
        float w;
    };
    std::vector<Item> items;
    if (in.dual_screen) {
        items.push_back({"util.swap", "SWAP", TouchTargetKind::UtilitySwap, 54.0f});
    }
    if (in.can_fast_forward) {
        items.push_back({"util.ff", ">> FF", TouchTargetKind::UtilityFastForward, 50.0f});
    }
    items.push_back({"util.menu", "|| MENU", TouchTargetKind::UtilityMenu, 64.0f});

    const float gap = 6.0f;
    float total_w = 0.0f;
    for (std::size_t i = 0; i < items.size(); ++i) {
        total_w += items[i].w + (i > 0 ? gap : 0.0f);
    }
    float cursor = centered ? center_x - total_w * 0.5f : x_right - total_w;
    for (const Item& item : items) {
        PlacedButton btn;
        btn.id = item.id;
        btn.label = item.label;
        btn.shape = ButtonShape::Pill;
        btn.fill = fill;
        btn.label_color = ink;
        btn.kind = item.kind;
        btn.rect = {cursor, y, item.w, 26.0f};
        out.buttons.push_back(std::move(btn));
        cursor += item.w + gap;
    }
}

void LayoutControls(TouchDeckLayout& out, const TouchConsoleSpec& spec, const TouchLayoutInput& in,
                    const gfx::Rect& left_zone, const gfx::Rect& right_zone, const gfx::Rect& foot_zone,
                    const gfx::Rect& lsh_zone, const gfx::Rect& rsh_zone, const gfx::Rect& clamp_bounds) {
    // 1. Left D-Pad and/or Left Analog Stick.
    const bool has_ldpad = spec.dpad != DPadStyle::None;
    const bool has_lstick = spec.left_stick != StickStyle::None;
    if (has_ldpad && has_lstick) {
        const float uy = left_zone.y + left_zone.h * 0.28f;
        const float ly = left_zone.y + left_zone.h * 0.75f;
        const float max_r = std::min((ly - uy) * 0.44f, std::min(left_zone.w * 0.25f, left_zone.h * 0.22f));
        const float r = std::clamp(max_r, 30.0f, 52.0f);
        const float ux = left_zone.x + left_zone.w * 0.42f;
        const float lx = left_zone.x + left_zone.w * 0.50f;
        if (spec.dpad_primary_left) {
            PlaceDPad(out.left_dpad, ux, uy, r, spec.dpad, spec.dpad_fill, false);
            PlaceStick(out.left_stick, lx, ly, r * 0.92f, spec.left_stick, spec.stick_fill, false);
        } else {
            PlaceStick(out.left_stick, ux, uy, r * 0.96f, spec.left_stick, spec.stick_fill, false);
            PlaceDPad(out.left_dpad, lx, ly, r * 0.92f, spec.dpad, spec.dpad_fill, false);
        }
    } else if (has_ldpad) {
        const float r = std::clamp(std::min(left_zone.w * 0.27f, left_zone.h * 0.30f), 42.0f, 66.0f);
        PlaceDPad(out.left_dpad, left_zone.x + left_zone.w * 0.46f, left_zone.y + left_zone.h * 0.49f, r,
                  spec.dpad, spec.dpad_fill, false);
    } else if (has_lstick) {
        const float r = std::clamp(std::min(left_zone.w * 0.27f, left_zone.h * 0.30f), 42.0f, 64.0f);
        PlaceStick(out.left_stick, left_zone.x + left_zone.w * 0.46f, left_zone.y + left_zone.h * 0.49f, r,
                   spec.left_stick, spec.stick_fill, false);
    }

    // 2. Optional Right Stick or Right D-Pad.
    if (spec.right_stick != StickStyle::None) {
        const float base_r = std::clamp(std::min(right_zone.w * 0.22f, right_zone.h * 0.22f), 28.0f, 48.0f);
        const float r = std::max(20.0f, base_r * spec.right_stick_scale);
        PlaceStick(out.right_stick, right_zone.x + right_zone.w * spec.right_stick_rx,
                   right_zone.y + right_zone.h * spec.right_stick_ry, r, spec.right_stick, spec.cstick_fill,
                   true);
    }
    if (spec.right_dpad) {
        const float r = std::clamp(std::min(right_zone.w * 0.21f, right_zone.h * 0.21f), 32.0f, 50.0f);
        PlaceDPad(out.right_dpad, right_zone.x + right_zone.w * 0.56f, right_zone.y + right_zone.h * 0.29f, r,
                  spec.dpad, spec.dpad_fill, spec.right_dpad_drives_stick);
    }

    // 3. Defined console buttons across zones.
    const float face_unit = std::clamp(std::min(right_zone.w, right_zone.h) * 0.125f, 18.0f, 29.0f);
    const float pill_unit = std::clamp(foot_zone.w * 0.135f, 24.0f, 44.0f);
    for (const TouchButtonDef& def : spec.buttons) {
        PlacedButton b;
        b.id = def.id;
        b.label = def.label;
        b.shape = def.shape;
        b.glyph = def.glyph;
        b.fill = def.fill;
        b.label_color = def.label_color;
        b.kind = def.kind;
        b.bit_or_dir = def.bit_or_dir;
        b.face_roll = def.face_roll;

        float cx = 0.0f;
        float cy = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        switch (def.zone) {
        case TouchZone::LeftSide:
            cx = left_zone.x + left_zone.w * def.rx;
            cy = left_zone.y + left_zone.h * def.ry;
            w = face_unit * 2.0f * def.w_scale;
            h = face_unit * 2.0f * def.h_scale;
            break;
        case TouchZone::RightSide:
            cx = right_zone.x + right_zone.w * def.rx;
            cy = right_zone.y + right_zone.h * def.ry;
            w = face_unit * 2.0f * def.w_scale;
            h = face_unit * 2.0f * def.h_scale;
            break;
        case TouchZone::CenterBottom:
            cx = foot_zone.x + foot_zone.w * def.rx;
            cy = foot_zone.y + foot_zone.h * def.ry;
            w = std::min(foot_zone.w * 0.42f, pill_unit * 1.45f * def.w_scale);
            h = std::min(foot_zone.h, 52.0f * def.h_scale);
            break;
        case TouchZone::LeftShoulder:
            cx = lsh_zone.x + lsh_zone.w * def.rx;
            cy = lsh_zone.y + lsh_zone.h * def.ry;
            w = std::min(lsh_zone.w * 0.56f, 48.0f * def.w_scale);
            h = std::min(lsh_zone.h, 42.0f * def.h_scale);
            break;
        case TouchZone::RightShoulder:
            cx = rsh_zone.x + rsh_zone.w * def.rx;
            cy = rsh_zone.y + rsh_zone.h * def.ry;
            w = std::min(rsh_zone.w * 0.56f, 48.0f * def.w_scale);
            h = std::min(rsh_zone.h, 42.0f * def.h_scale);
            break;
        }
        b.rect = ClampInside({cx - w * 0.5f, cy - h * 0.5f, w, h}, clamp_bounds);
        out.buttons.push_back(std::move(b));
    }

    (void)in;
}

} // namespace

TouchDeckLayout LayoutTouchDeck(const TouchConsoleSpec& spec, const TouchLayoutInput& in) {
    TouchDeckLayout out;
    out.system_id = spec.system_id;
    out.display_badge = spec.display_badge;
    out.shell_bg = spec.shell_bg;
    out.bezel_bg = spec.bezel_bg;
    out.shell_trim = spec.shell_trim;
    out.accent = spec.accent;
    out.power_led = spec.power_led;
    out.area = in.area;
    out.portrait = in.area.h > in.area.w;

    const bool has_shoulders = std::any_of(spec.buttons.begin(), spec.buttons.end(), [](const TouchButtonDef& b) {
        return b.zone == TouchZone::LeftShoulder || b.zone == TouchZone::RightShoulder;
    });

    if (out.portrait) {
        const float pad_l = std::max(10.0f, in.safe_left + 6.0f);
        const float pad_r = std::max(10.0f, in.safe_right + 6.0f);
        const float pad_t = std::max(8.0f, in.safe_top + 4.0f);
        const float pad_b = std::max(10.0f, in.safe_bottom + 6.0f);
        const float usable_h = std::max(220.0f, in.area.h - pad_t - pad_b);
        const float max_deck = in.dual_screen ? usable_h * 0.41f : usable_h * 0.47f;
        const float min_deck = std::min(220.0f, usable_h * 0.36f);
        const float deck_body_h = std::clamp(usable_h * spec.deck_ratio, min_deck, max_deck);

        out.deck_rect = {in.area.x, in.area.Bottom() - deck_body_h - pad_b, in.area.w, deck_body_h + pad_b};
        out.bezel_rect = {in.area.x, in.area.y, in.area.w, out.deck_rect.y - in.area.y};
        out.game_room = {
            in.area.x + pad_l,
            in.area.y + pad_t,
            std::max(100.0f, in.area.w - pad_l - pad_r),
            std::max(100.0f, out.deck_rect.y - in.area.y - pad_t - 6.0f),
        };

        const float ctrl_x = in.area.x + pad_l;
        const float ctrl_w = std::max(240.0f, in.area.w - pad_l - pad_r);
        const float util_y = out.deck_rect.y + 8.0f;
        AddUtilityCluster(out, spec, in, ctrl_x + ctrl_w, util_y, false, 0.0f);

        const float sh_y = util_y + 32.0f;
        const float sh_h = has_shoulders ? 28.0f : 0.0f;
        const gfx::Rect lsh_zone{ctrl_x, sh_y, ctrl_w * 0.42f, sh_h};
        const gfx::Rect rsh_zone{ctrl_x + ctrl_w * 0.58f, sh_y, ctrl_w * 0.42f, sh_h};

        const float foot_h = 30.0f;
        const float foot_y = in.area.Bottom() - pad_b - foot_h;
        const gfx::Rect foot_zone{ctrl_x + ctrl_w * 0.14f, foot_y, ctrl_w * 0.72f, foot_h};

        const float body_top = sh_y + (has_shoulders ? sh_h + 8.0f : 2.0f);
        const float body_h = std::max(110.0f, foot_y - 8.0f - body_top);
        const gfx::Rect left_zone{ctrl_x, body_top, ctrl_w * 0.47f, body_h};
        const gfx::Rect right_zone{ctrl_x + ctrl_w * 0.51f, body_top, ctrl_w * 0.49f, body_h};
        const gfx::Rect clamp_bounds{ctrl_x, util_y, ctrl_w, in.area.Bottom() - pad_b - util_y};

        LayoutControls(out, spec, in, left_zone, right_zone, foot_zone, lsh_zone, rsh_zone, clamp_bounds);
        return out;
    }

    // Landscape: fullscreen gameplay with translucent peripheral controls respecting safe cutouts.
    out.game_room = in.area;
    const float pad_l = std::max(14.0f, in.safe_left + 8.0f);
    const float pad_r = std::max(14.0f, in.safe_right + 8.0f);
    const float pad_t = std::max(10.0f, in.safe_top + 6.0f);
    const float pad_b = std::max(12.0f, in.safe_bottom + 6.0f);

    AddUtilityCluster(out, spec, in, 0.0f, in.area.y + pad_t, true, in.area.CenterX());

    const float band_w = std::clamp(in.area.w * 0.24f, std::min(165.0f, in.area.w * 0.30f), 255.0f);
    const float band_h = std::clamp(in.area.h * 0.58f, 185.0f, 290.0f);
    const float band_y = in.area.Bottom() - pad_b - band_h;
    const gfx::Rect left_zone{in.area.x + pad_l, band_y, band_w, band_h};
    const gfx::Rect right_zone{in.area.Right() - pad_r - band_w, band_y, band_w, band_h};
    const gfx::Rect lsh_zone{in.area.x + pad_l, in.area.y + pad_t, std::min(band_w * 0.85f, 185.0f), 30.0f};
    const gfx::Rect rsh_zone{in.area.Right() - pad_r - std::min(band_w * 0.85f, 185.0f), in.area.y + pad_t,
                             std::min(band_w * 0.85f, 185.0f), 30.0f};
    const float raw_gap = right_zone.x - left_zone.Right() - 12.0f;
    const float gap_w = std::clamp(raw_gap, 80.0f, 260.0f);
    const float foot_b = std::max(6.0f, in.safe_bottom + 2.0f);
    const gfx::Rect foot_zone{in.area.CenterX() - gap_w * 0.5f, in.area.Bottom() - foot_b - 24.0f, gap_w, 24.0f};
    const gfx::Rect clamp_bounds{in.area.x + pad_l, in.area.y + pad_t,
                                 std::max(100.0f, in.area.w - pad_l - pad_r),
                                 std::max(100.0f, in.area.h - pad_t - pad_b)};

    LayoutControls(out, spec, in, left_zone, right_zone, foot_zone, lsh_zone, rsh_zone, clamp_bounds);
    return out;
}

} // namespace den::app
