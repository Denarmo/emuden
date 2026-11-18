// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Hardware-inspired touch deck specifications for all bundled Sega and Sony systems.

#include "touch_catalog_internal.h"

namespace den::app::touch_detail {

namespace {

void AddPlayStationFace(std::vector<TouchButtonDef>& out, float cx, float cy, float dx, float dy,
                        std::uint32_t btn_fill, float scale = 0.94f) {
    out.push_back(Face("square", "", btn_fill, 0xE27CC6, RETRO_Y, cx - dx, cy, scale, scale,
                       ButtonShape::Circle, ButtonGlyph::PsSquare));
    out.push_back(Face("cross", "", btn_fill, 0x76A2F0, RETRO_B, cx, cy + dy, scale, scale,
                       ButtonShape::Circle, ButtonGlyph::PsCross));
    out.push_back(Face("triangle", "", btn_fill, 0x45CCA4, RETRO_X, cx, cy - dy, scale, scale,
                       ButtonShape::Circle, ButtonGlyph::PsTriangle));
    out.push_back(Face("circle", "", btn_fill, 0xEE6464, RETRO_A, cx + dx, cy, scale, scale,
                       ButtonShape::Circle, ButtonGlyph::PsCircle));
}

void AddSixButtonSega(std::vector<TouchButtonDef>& out, const char* b_y, const char* b_b, const char* b_a,
                      const char* t_l, const char* t_x, const char* t_r,
                      int bit_bot0, int bit_bot1, int bit_bot2,
                      int bit_top0, int bit_top1, int bit_top2) {
    out.push_back(Face("top0", t_l, 0x2F323A, 0xD8DCE4, bit_top0, 0.22f, 0.30f, 0.80f, 0.80f));
    out.push_back(Face("top1", t_x, 0x2F323A, 0xD8DCE4, bit_top1, 0.50f, 0.23f, 0.80f, 0.80f));
    out.push_back(Face("top2", t_r, 0x2F323A, 0xD8DCE4, bit_top2, 0.78f, 0.30f, 0.80f, 0.80f));
    out.push_back(Face("bot0", b_y, 0x22252C, 0xF0F2F6, bit_bot0, 0.20f, 0.67f, 0.94f, 0.94f));
    out.push_back(Face("bot1", b_b, 0x22252C, 0xF0F2F6, bit_bot1, 0.48f, 0.60f, 0.94f, 0.94f));
    out.push_back(Face("bot2", b_a, 0x22252C, 0xF0F2F6, bit_bot2, 0.76f, 0.67f, 0.94f, 0.94f));
}

} // namespace

void RegisterSegaSonySpecs(std::vector<TouchConsoleSpec>& out) {
    // SG-1000 & Master System: matte black faceplate with crisp red pinstripe, boxy D-pad, 1 & 2 buttons.
    for (const auto& [id, badge] : {std::pair{"sg1000", "SG-1000"}, std::pair{"sms", "MASTER SYSTEM"}}) {
        TouchConsoleSpec s;
        s.system_id = id;
        s.display_badge = badge;
        s.shell_bg = gfx::Color::Rgb(0x1B1D22);
        s.bezel_bg = gfx::Color::Rgb(0x121418);
        s.shell_trim = gfx::Color::Rgb(0xDC3028);
        s.dpad_fill = gfx::Color::Rgb(0x2F323B);
        s.accent = ShellAccent::PinstripeRed;
        s.dpad = DPadStyle::Disc;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("1", "1", 0x2D303A, 0xF4F6FA, RETRO_B, 0.36f, 0.56f, 1.08f, 1.08f),
            Face("2", "2", 0x2D303A, 0xF4F6FA, RETRO_A, 0.72f, 0.40f, 1.08f, 1.08f),
            Pill("pause", "PAUSE", 0xDC3028, 0xFFFFFF, RETRO_START, 0.50f, 0.5f, 1.15f, 0.48f),
        };
        out.push_back(std::move(s));
    }

    // Sega Game Gear: slate charcoal handheld housing, royal blue Start oval, 1 & 2 buttons.
    {
        TouchConsoleSpec s;
        s.system_id = "gg";
        s.display_badge = "GAME GEAR";
        s.shell_bg = gfx::Color::Rgb(0x252830);
        s.bezel_bg = gfx::Color::Rgb(0x14161C);
        s.shell_trim = gfx::Color::Rgb(0x2D64DC);
        s.dpad_fill = gfx::Color::Rgb(0x363A46);
        s.accent = ShellAccent::DsSpeakerDots;
        s.power_led = true;
        s.dpad = DPadStyle::Disc;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("1", "1", 0x353946, 0xF2F4F8, RETRO_B, 0.36f, 0.56f, 1.08f, 1.08f),
            Face("2", "2", 0x353946, 0xF2F4F8, RETRO_A, 0.72f, 0.40f, 1.08f, 1.08f),
            Pill("start", "START", 0x2D64DC, 0xFFFFFF, RETRO_START, 0.50f, 0.5f, 1.2f, 0.48f),
        };
        out.push_back(std::move(s));
    }

    // Genesis / Mega Drive & Sega 32X: gloss black 6-button arc (Y/B/A -> A/B/C, L/X/R -> X/Y/Z), Mode + Start.
    for (const auto& [id, badge, trim] : {
             std::tuple{"genesis", "MEGA DRIVE", 0xD83228U},
             std::tuple{"32x", "SEGA 32X", 0xE2B424U},
         }) {
        TouchConsoleSpec s;
        s.system_id = id;
        s.display_badge = badge;
        s.shell_bg = gfx::Color::Rgb(0x16181E);
        s.bezel_bg = gfx::Color::Rgb(0x0E1014);
        s.shell_trim = gfx::Color::Rgb(trim);
        s.dpad_fill = gfx::Color::Rgb(0x2C303A);
        s.dpad = DPadStyle::Disc;
        s.deck_ratio = 0.43f;
        AddSixButtonSega(s.buttons, "A", "B", "C", "X", "Y", "Z", RETRO_Y, RETRO_B, RETRO_A, RETRO_L, RETRO_X,
                         RETRO_R);
        s.buttons.push_back(Pill("mode", "MODE", 0x323642, 0xE6E8EE, RETRO_SELECT, 0.36f, 0.5f, 0.98f, 0.46f));
        s.buttons.push_back(Pill("start", "START", 0x3A3F4E, 0xFFFFFF, RETRO_START, 0.64f, 0.5f, 1.15f, 0.48f));
        out.push_back(std::move(s));
    }

    // Sega Saturn: dark slate Saturn 6-button pad with L & R shoulder bumpers.
    {
        TouchConsoleSpec s;
        s.system_id = "saturn";
        s.display_badge = "SEGA SATURN";
        s.shell_bg = gfx::Color::Rgb(0x1E2128);
        s.bezel_bg = gfx::Color::Rgb(0x111318);
        s.shell_trim = gfx::Color::Rgb(0x4678C8);
        s.dpad_fill = gfx::Color::Rgb(0x323642);
        s.dpad = DPadStyle::Disc;
        s.deck_ratio = 0.44f;
        s.buttons.push_back(Shoulder("l", "L", 0x323642, 0xECEFF6, RETRO_L2, true));
        s.buttons.push_back(Shoulder("r", "R", 0x323642, 0xECEFF6, RETRO_R2, false));
        AddSixButtonSega(s.buttons, "A", "B", "C", "X", "Y", "Z", RETRO_B, RETRO_A, RETRO_R, RETRO_Y, RETRO_X,
                         RETRO_L);
        s.buttons.push_back(Pill("start", "START", 0x353A48, 0xFFFFFF, RETRO_START, 0.50f, 0.5f, 1.15f, 0.48f));
        out.push_back(std::move(s));
    }

    // Dreamcast: off-white housing with swirl blue/orange accent, thumbstick over D-pad, colored ABXY, L/R.
    {
        TouchConsoleSpec s;
        s.system_id = "dreamcast";
        s.display_badge = "DREAMCAST";
        s.shell_bg = gfx::Color::Rgb(0xE6E8EE);
        s.bezel_bg = gfx::Color::Rgb(0x1C2028);
        s.shell_trim = gfx::Color::Rgb(0x2F6FD8);
        s.dpad_fill = gfx::Color::Rgb(0xCDD2DC);
        s.stick_fill = gfx::Color::Rgb(0xD4D9E2);
        s.accent = ShellAccent::SwirlDot;
        s.dpad = DPadStyle::Cross;
        s.dpad_primary_left = false;
        s.left_stick = StickStyle::Round;
        s.deck_ratio = 0.44f;
        s.buttons.push_back(Shoulder("l", "L", 0xCFD4DF, 0x222632, RETRO_L2, true));
        s.buttons.push_back(Shoulder("r", "R", 0xCFD4DF, 0x222632, RETRO_R2, false));
        AddDiamond(s.buttons, 0.52f, 0.48f, 0.21f, 0.23f, "X", "A", "Y", "B", 0x3276DC, 0x2DA458, 0xE2B624,
                   0xDA3838, 0xFFFFFF, 0.94f);
        s.buttons.push_back(Pill("start", "START", 0xCBD0DC, 0x222632, RETRO_START, 0.50f, 0.5f, 1.0f, 0.52f,
                                 ButtonShape::TriangleStart));
        out.push_back(std::move(s));
    }

    // PlayStation (SCPH-1010, 1994): classic cool grey housing, segmented D-pad, PS glyphs, L1/L2/R1/R2.
    {
        TouchConsoleSpec s;
        s.system_id = "psx";
        s.display_badge = "PLAYSTATION";
        s.shell_bg = gfx::Color::Rgb(0xA6A9B0);
        s.bezel_bg = gfx::Color::Rgb(0x1D1F24);
        s.shell_trim = gfx::Color::Rgb(0x6D717C);
        s.dpad_fill = gfx::Color::Rgb(0x4B4E56);
        s.dpad = DPadStyle::Split;
        s.deck_ratio = 0.42f;
        s.buttons.push_back(Shoulder("l2", "L2", 0x626670, 0xFFFFFF, RETRO_L2, true, 0.28f, 0.5f, 1.1f, 0.66f));
        s.buttons.push_back(Shoulder("l1", "L1", 0x767A86, 0xFFFFFF, RETRO_L, true, 0.72f, 0.5f, 1.15f, 0.68f));
        s.buttons.push_back(Shoulder("r1", "R1", 0x767A86, 0xFFFFFF, RETRO_R, false, 0.28f, 0.5f, 1.15f, 0.68f));
        s.buttons.push_back(Shoulder("r2", "R2", 0x626670, 0xFFFFFF, RETRO_R2, false, 0.72f, 0.5f, 1.1f, 0.66f));
        AddPlayStationFace(s.buttons, 0.52f, 0.49f, 0.21f, 0.23f, 0x3B3E46, 0.94f);
        s.buttons.push_back(Pill("select", "SELECT", 0x6A6E78, 0xFFFFFF, RETRO_SELECT, 0.37f, 0.5f, 1.0f, 0.44f));
        s.buttons.push_back(Pill("start", "START", 0x6A6E78, 0xFFFFFF, RETRO_START, 0.63f, 0.5f, 1.0f, 0.50f,
                                 ButtonShape::TriangleStart));
        out.push_back(std::move(s));
    }

    // PlayStation 2 (DualShock 2): obsidian black, Split D-pad, twin analog sticks, L1/L2/R1/R2, PS glyphs.
    {
        TouchConsoleSpec s;
        s.system_id = "ps2";
        s.display_badge = "PLAYSTATION 2";
        s.shell_bg = gfx::Color::Rgb(0x16181E);
        s.bezel_bg = gfx::Color::Rgb(0x0D0E12);
        s.shell_trim = gfx::Color::Rgb(0x3664C8);
        s.dpad_fill = gfx::Color::Rgb(0x2D313B);
        s.stick_fill = gfx::Color::Rgb(0x2B2E36);
        s.cstick_fill = gfx::Color::Rgb(0x2B2E36);
        s.dpad = DPadStyle::Split;
        s.dpad_primary_left = true;
        s.left_stick = StickStyle::Round;
        s.right_stick = StickStyle::Round;
        s.right_stick_rx = 0.24f;
        s.right_stick_ry = 0.78f;
        s.right_stick_scale = 0.80f;
        s.deck_ratio = 0.45f;
        s.buttons.push_back(Shoulder("l2", "L2", 0x2A2E38, 0xEAECEF, RETRO_L2, true, 0.28f, 0.5f, 1.1f, 0.66f));
        s.buttons.push_back(Shoulder("l1", "L1", 0x353A46, 0xFFFFFF, RETRO_L, true, 0.72f, 0.5f, 1.15f, 0.68f));
        s.buttons.push_back(Shoulder("r1", "R1", 0x353A46, 0xFFFFFF, RETRO_R, false, 0.28f, 0.5f, 1.15f, 0.68f));
        s.buttons.push_back(Shoulder("r2", "R2", 0x2A2E38, 0xEAECEF, RETRO_R2, false, 0.72f, 0.5f, 1.1f, 0.66f));
        AddPlayStationFace(s.buttons, 0.62f, 0.36f, 0.19f, 0.20f, 0x242830, 0.84f);
        s.buttons.push_back(Pill("select", "SELECT", 0x2E323C, 0xE2E6F0, RETRO_SELECT, 0.37f, 0.5f, 1.0f, 0.44f));
        s.buttons.push_back(Pill("start", "START", 0x2E323C, 0xE2E6F0, RETRO_START, 0.63f, 0.5f, 1.0f, 0.50f,
                                 ButtonShape::TriangleStart));
        out.push_back(std::move(s));
    }

    // Sony PSP (PSP-1000/3000): piano black shell, silver trim ring, D-pad above concave stick, PS glyphs, L/R.
    {
        TouchConsoleSpec s;
        s.system_id = "psp";
        s.display_badge = "PSP";
        s.shell_bg = gfx::Color::Rgb(0x16181D);
        s.bezel_bg = gfx::Color::Rgb(0x0B0C0F);
        s.shell_trim = gfx::Color::Rgb(0xB8BDC8);
        s.dpad_fill = gfx::Color::Rgb(0x2D313B);
        s.stick_fill = gfx::Color::Rgb(0x2F333D);
        s.accent = ShellAccent::PspRing;
        s.power_led = true;
        s.dpad = DPadStyle::Split;
        s.dpad_primary_left = true; // PSP keeps D-pad above the micro-stick
        s.left_stick = StickStyle::Concave;
        s.deck_ratio = 0.44f;
        s.buttons.push_back(Shoulder("l", "L", 0x2C303A, 0xF0F2F6, RETRO_L, true));
        s.buttons.push_back(Shoulder("r", "R", 0x2C303A, 0xF0F2F6, RETRO_R, false));
        AddPlayStationFace(s.buttons, 0.52f, 0.48f, 0.21f, 0.23f, 0x252830, 0.92f);
        s.buttons.push_back(Pill("select", "SELECT", 0x2B2F39, 0xE4E7F0, RETRO_SELECT, 0.36f, 0.5f, 1.05f, 0.44f));
        s.buttons.push_back(Pill("start", "START", 0x2B2F39, 0xE4E7F0, RETRO_START, 0.64f, 0.5f, 1.05f, 0.44f));
        out.push_back(std::move(s));
    }
}

} // namespace den::app::touch_detail
