// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "page_widths.h"
#include "touch_spec.h"

namespace den::app {

namespace {

bool RectsOverlap(const gfx::Rect& a, const gfx::Rect& b) {
    return a.x < b.Right() - 0.5f && a.Right() > b.x + 0.5f &&
           a.y < b.Bottom() - 0.5f && a.Bottom() > b.y + 0.5f;
}

bool ContainsRect(const gfx::Rect& outer, const gfx::Rect& inner) {
    return inner.x >= outer.x - 0.5f && inner.y >= outer.y - 0.5f &&
           inner.Right() <= outer.Right() + 0.5f && inner.Bottom() <= outer.Bottom() + 0.5f;
}

} // namespace

TEST_CASE("portrait rail triggers only when height exceeds width") {
    CHECK(BottomRail(540.0f, 1200.0f));
    CHECK(BottomRail(533.0f, 1185.0f));
    CHECK_FALSE(BottomRail(1280.0f, 720.0f));
    CHECK_FALSE(BottomRail(1240.0f, 1080.0f));
    CHECK_FALSE(BottomRail(0.0f, 720.0f));
    CHECK(RailWidth(540.0f, 1200.0f) == 0.0f);
    CHECK(RailWidth(1240.0f, 1080.0f) == FULL_RAIL_WIDTH);
    CHECK(RailWidth(570.0f, 500.0f) == SLIM_RAIL_WIDTH);
}

TEST_CASE("all 37 bundled systems have valid non-overlapping portrait and landscape touch decks") {
    const auto systems = AllBundledTouchSystems();
    CHECK(systems.size() == 37);

    std::set<std::string> unique_ids;
    for (const std::string_view sys : systems) {
        CAPTURE(sys);
        const TouchConsoleSpec& spec = TouchSpecForSystem(sys);
        CHECK(std::string_view(spec.system_id) == sys);
        CHECK_FALSE(std::string_view(spec.display_badge).empty());
        CHECK_FALSE(spec.buttons.empty());
        unique_ids.insert(std::string(spec.system_id));

        const bool dual = (sys == "nds" || sys == "3ds");
        const TouchLayoutInput portrait_in{{0, 0, 533, 1185}, 0, 24, 0, 18, dual, true};
        const TouchDeckLayout p = LayoutTouchDeck(spec, portrait_in);
        CHECK(p.portrait);
        CHECK(p.game_room.w >= 300.0f);
        CHECK(p.game_room.h >= (dual ? 500.0f : 220.0f));
        CHECK(p.game_room.Bottom() <= p.deck_rect.y + 0.5f);
        CHECK(ContainsRect(portrait_in.area, p.deck_rect));

        for (const PlacedButton& btn : p.buttons) {
            CAPTURE(btn.id);
            CHECK(btn.rect.w > 10.0f);
            CHECK(btn.rect.h > 10.0f);
            CHECK(ContainsRect(portrait_in.area, btn.rect));
            CHECK_FALSE(RectsOverlap(p.game_room, btn.rect));
        }
        if (p.left_dpad.style != DPadStyle::None) {
            CHECK(ContainsRect(portrait_in.area, p.left_dpad.bounds));
            CHECK_FALSE(RectsOverlap(p.game_room, p.left_dpad.bounds));
        }
        if (p.left_stick.style != StickStyle::None) {
            CHECK(ContainsRect(portrait_in.area, p.left_stick.bounds));
            CHECK_FALSE(RectsOverlap(p.game_room, p.left_stick.bounds));
        }
        if (p.left_dpad.style != DPadStyle::None && p.left_stick.style != StickStyle::None) {
            CHECK_FALSE(RectsOverlap(p.left_dpad.bounds, p.left_stick.bounds));
        }

        const TouchLayoutInput landscape_in{{0, 0, 1185, 533}, 24, 0, 18, 12, dual, true};
        const TouchDeckLayout l = LayoutTouchDeck(spec, landscape_in);
        CHECK_FALSE(l.portrait);
        for (const PlacedButton& btn : l.buttons) {
            CAPTURE(btn.id);
            CHECK(ContainsRect(landscape_in.area, btn.rect));
            CHECK(btn.rect.x >= landscape_in.safe_left - 0.5f);
            CHECK(btn.rect.Right() <= landscape_in.area.w - landscape_in.safe_right + 0.5f);
        }
    }
    CHECK(unique_ids.size() == 37);
    CHECK(std::string_view(TouchSpecForSystem("n3ds").system_id) == "3ds");
}

TEST_CASE("multi-touch controller supports concurrent dpad, face slide-roll, sticks, and utilities") {
    const TouchDeckLayout gba =
        LayoutTouchDeck(TouchSpecForSystem("gba"), {{0, 0, 533, 1185}, 0, 24, 0, 18, false, true});
    TouchController ctrl;
    ctrl.SetLayout(gba);

    // Finger 1 holds D-pad Right.
    const auto r1 = ctrl.FingerDown(101, gba.left_dpad.cx + gba.left_dpad.radius * 0.6f, gba.left_dpad.cy);
    CHECK(r1.consumed);
    CHECK(r1.haptic);
    CHECK((ctrl.State().buttons & (1u << RETRO_RIGHT)) != 0);

    // Locate B and A centers; press B with Finger 2 while Finger 1 still holds Right.
    const PlacedButton* b_btn = nullptr;
    const PlacedButton* a_btn = nullptr;
    const PlacedButton* menu_btn = nullptr;
    const PlacedButton* ff_btn = nullptr;
    for (const PlacedButton& b : gba.buttons) {
        if (b.id == "b") b_btn = &b;
        if (b.id == "a") a_btn = &b;
        if (b.kind == TouchTargetKind::UtilityMenu) menu_btn = &b;
        if (b.kind == TouchTargetKind::UtilityFastForward) ff_btn = &b;
    }
    REQUIRE(b_btn != nullptr);
    REQUIRE(a_btn != nullptr);
    REQUIRE(menu_btn != nullptr);
    REQUIRE(ff_btn != nullptr);

    const auto r2 = ctrl.FingerDown(202, b_btn->rect.CenterX(), b_btn->rect.CenterY());
    CHECK(r2.consumed);
    CHECK((ctrl.State().buttons & (1u << RETRO_RIGHT)) != 0);
    CHECK((ctrl.State().buttons & (1u << RETRO_B)) != 0);

    // Slide Finger 2 smoothly over A: B clears, A engages, Finger 1 stays Right.
    ctrl.FingerMove(202, a_btn->rect.CenterX(), a_btn->rect.CenterY());
    CHECK((ctrl.State().buttons & (1u << RETRO_RIGHT)) != 0);
    CHECK((ctrl.State().buttons & (1u << RETRO_A)) != 0);
    CHECK((ctrl.State().buttons & (1u << RETRO_B)) == 0);

    // Release Finger 1; Finger 2 A remains pressed until Finger 2 lifts.
    ctrl.FingerUp(101, 0, 0);
    CHECK((ctrl.State().buttons & (1u << RETRO_RIGHT)) == 0);
    CHECK((ctrl.State().buttons & (1u << RETRO_A)) != 0);
    ctrl.FingerUp(202, 0, 0);
    CHECK(ctrl.State().buttons == 0);

    // Utility button taps emit UtilityHit events without setting game pad bits.
    const auto rm = ctrl.FingerDown(303, menu_btn->rect.CenterX(), menu_btn->rect.CenterY());
    CHECK(rm.consumed);
    CHECK(rm.utility == TouchController::UtilityHit::Menu);
    CHECK(ctrl.State().buttons == 0);
    ctrl.FingerUp(303, 0, 0);

    const auto rff = ctrl.FingerDown(304, ff_btn->rect.CenterX(), ff_btn->rect.CenterY());
    CHECK(rff.utility == TouchController::UtilityHit::FastForward);
    ctrl.FingerUp(304, 0, 0);

    // Finger in game_room is NOT consumed so touchscreen/stylus games receive it.
    const auto rg = ctrl.FingerDown(404, gba.game_room.CenterX(), gba.game_room.CenterY());
    CHECK_FALSE(rg.consumed);

    // Analog stick & N64 C-buttons drive lx/ly and rx/ry cleanly.
    const TouchDeckLayout n64 =
        LayoutTouchDeck(TouchSpecForSystem("n64"), {{0, 0, 533, 1185}, 0, 24, 0, 18, false, true});
    ctrl.SetLayout(n64);
    ctrl.FingerDown(501, n64.left_stick.cx + n64.left_stick.well_r * 0.8f, n64.left_stick.cy);
    CHECK(ctrl.State().lx > 0.8f);
    CHECK(std::fabs(ctrl.State().ly) < 0.1f);
    for (const PlacedButton& b : n64.buttons) {
        if (b.id == "cu") {
            ctrl.FingerDown(502, b.rect.CenterX(), b.rect.CenterY());
        }
    }
    CHECK(ctrl.State().ry < -0.5f);
    ctrl.ReleaseAll();
    CHECK(ctrl.ActiveFingerCount() == 0);
    CHECK(ctrl.State().buttons == 0);
    CHECK(ctrl.State().lx == 0.0f);
    CHECK(ctrl.State().ry == 0.0f);

    // PSP: split D-Pad sits above concave Left Stick, PlayStation vector glyphs, concurrent stick + Cross.
    const TouchDeckLayout psp =
        LayoutTouchDeck(TouchSpecForSystem("psp"), {{0, 0, 533, 1185}, 0, 24, 0, 18, false, true});
    CHECK(psp.left_dpad.style == DPadStyle::Split);
    CHECK(psp.left_stick.style == StickStyle::Concave);
    CHECK(psp.left_dpad.cy < psp.left_stick.cy);
    const PlacedButton* psp_cross = nullptr;
    int ps_glyphs = 0;
    for (const PlacedButton& b : psp.buttons) {
        if (b.id == "cross") psp_cross = &b;
        if (b.glyph == ButtonGlyph::PsCross || b.glyph == ButtonGlyph::PsCircle ||
            b.glyph == ButtonGlyph::PsSquare || b.glyph == ButtonGlyph::PsTriangle) {
            ++ps_glyphs;
        }
    }
    CHECK(ps_glyphs == 4);
    REQUIRE(psp_cross != nullptr);
    ctrl.SetLayout(psp);
    ctrl.FingerDown(601, psp.left_stick.cx + psp.left_stick.well_r * 0.7f, psp.left_stick.cy);
    ctrl.FingerDown(602, psp_cross->rect.CenterX(), psp_cross->rect.CenterY());
    CHECK(ctrl.State().lx > 0.7f);
    CHECK((ctrl.State().buttons & (1u << RETRO_B)) != 0);
    ctrl.ReleaseAll();

    // Virtual Boy: dual D-Pads + B/A where right D-Pad drives rx/ry.
    const TouchDeckLayout vb =
        LayoutTouchDeck(TouchSpecForSystem("virtualboy"), {{0, 0, 533, 1185}, 0, 24, 0, 18, false, true});
    CHECK(vb.left_dpad.style == DPadStyle::Cross);
    CHECK(vb.right_dpad.style == DPadStyle::Cross);
    ctrl.SetLayout(vb);
    ctrl.FingerDown(701, vb.left_dpad.cx, vb.left_dpad.cy - vb.left_dpad.radius * 0.6f);
    ctrl.FingerDown(702, vb.right_dpad.cx + vb.right_dpad.radius * 0.6f, vb.right_dpad.cy);
    CHECK((ctrl.State().buttons & (1u << RETRO_UP)) != 0);
    CHECK(ctrl.State().rx > 0.5f);
    ctrl.ReleaseAll();
}

} // namespace den::app
