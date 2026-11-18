// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Hardware-inspired touch deck specifications for all bundled Nintendo systems.

#include "touch_catalog_internal.h"

namespace den::app::touch_detail {

void RegisterNintendoSpecs(std::vector<TouchConsoleSpec>& out) {
    // DMG-01 Game Boy (1989): warm putty grey housing, dark olive bezel, magenta A/B.
    {
        TouchConsoleSpec s;
        s.system_id = "gb";
        s.display_badge = "GAME BOY";
        s.shell_bg = gfx::Color::Rgb(0xC8C4B8);
        s.bezel_bg = gfx::Color::Rgb(0x5B605A);
        s.shell_trim = gfx::Color::Rgb(0x8D3B62);
        s.dpad_fill = gfx::Color::Rgb(0x25272B);
        s.accent = ShellAccent::GbSpeakerSlots;
        s.power_led = true;
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("b", "B", 0x9B245B, 0xF7EFF4, RETRO_B, 0.36f, 0.56f, 1.08f, 1.08f),
            Face("a", "A", 0x9B245B, 0xF7EFF4, RETRO_A, 0.72f, 0.38f, 1.08f, 1.08f),
            Pill("select", "SELECT", 0x797C82, 0xF4F4F6, RETRO_SELECT, 0.37f, 0.5f, 1.1f, 0.46f,
                 ButtonShape::AngledPill),
            Pill("start", "START", 0x797C82, 0xF4F4F6, RETRO_START, 0.63f, 0.5f, 1.1f, 0.46f,
                 ButtonShape::AngledPill),
        };
        out.push_back(std::move(s));
    }

    // CGB-001 Game Boy Color (1998, Grape): purple shell, charcoal controls, black lens bezel.
    {
        TouchConsoleSpec s;
        s.system_id = "gbc";
        s.display_badge = "GAME BOY COLOR";
        s.shell_bg = gfx::Color::Rgb(0x5A3E9E);
        s.bezel_bg = gfx::Color::Rgb(0x1E2028);
        s.shell_trim = gfx::Color::Rgb(0x3D2970);
        s.dpad_fill = gfx::Color::Rgb(0x23252B);
        s.accent = ShellAccent::GbSpeakerSlots;
        s.power_led = true;
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("b", "B", 0x24262C, 0xE6E7EE, RETRO_B, 0.36f, 0.56f, 1.08f, 1.08f),
            Face("a", "A", 0x24262C, 0xE6E7EE, RETRO_A, 0.72f, 0.38f, 1.08f, 1.08f),
            Pill("select", "SELECT", 0x332B52, 0xDCDDE8, RETRO_SELECT, 0.37f, 0.5f, 1.05f, 0.48f),
            Pill("start", "START", 0x332B52, 0xDCDDE8, RETRO_START, 0.63f, 0.5f, 1.05f, 0.48f),
        };
        out.push_back(std::move(s));
    }

    // AGB-001 Game Boy Advance (2001, Indigo): indigo body, light cool-grey buttons & L/R shoulders.
    {
        TouchConsoleSpec s;
        s.system_id = "gba";
        s.display_badge = "GAME BOY ADVANCE";
        s.shell_bg = gfx::Color::Rgb(0x493E9C);
        s.bezel_bg = gfx::Color::Rgb(0x1D1F26);
        s.shell_trim = gfx::Color::Rgb(0x342B78);
        s.dpad_fill = gfx::Color::Rgb(0xBEC3CC);
        s.accent = ShellAccent::DsSpeakerDots;
        s.power_led = true;
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.41f;
        s.buttons = {
            Shoulder("l", "L", 0xB8BEC8, 0x232732, RETRO_L, true),
            Shoulder("r", "R", 0xB8BEC8, 0x232732, RETRO_R, false),
            Face("b", "B", 0xC2C7D0, 0x262936, RETRO_B, 0.36f, 0.57f, 1.08f, 1.08f),
            Face("a", "A", 0xC2C7D0, 0x262936, RETRO_A, 0.72f, 0.39f, 1.08f, 1.08f),
            Pill("select", "SELECT", 0x99A0AC, 0x1E2028, RETRO_SELECT, 0.37f, 0.5f, 1.0f, 0.46f),
            Pill("start", "START", 0x99A0AC, 0x1E2028, RETRO_START, 0.63f, 0.5f, 1.0f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Nintendo DS Lite (USG-001, 2006): glossy white deck, pearl ABXY, speaker dots.
    {
        TouchConsoleSpec s;
        s.system_id = "nds";
        s.display_badge = "NINTENDO DS";
        s.shell_bg = gfx::Color::Rgb(0xECEFF4);
        s.bezel_bg = gfx::Color::Rgb(0x20232B);
        s.shell_trim = gfx::Color::Rgb(0xC6CCD6);
        s.dpad_fill = gfx::Color::Rgb(0xD2D7E0);
        s.accent = ShellAccent::DsSpeakerDots;
        s.power_led = true;
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.38f;
        s.buttons.push_back(Shoulder("l", "L", 0xD2D7E0, 0x222630, RETRO_L, true));
        s.buttons.push_back(Shoulder("r", "R", 0xD2D7E0, 0x222630, RETRO_R, false));
        AddDiamond(s.buttons, 0.52f, 0.48f, 0.21f, 0.23f, "Y", "B", "X", "A", 0xDDE1EA, 0xDDE1EA, 0xDDE1EA,
                   0xDDE1EA, 0x2A303C, 0.92f);
        s.buttons.push_back(Pill("select", "SELECT", 0xC8CED8, 0x2A303C, RETRO_SELECT, 0.37f, 0.5f, 1.0f, 0.46f));
        s.buttons.push_back(Pill("start", "START", 0xC8CED8, 0x2A303C, RETRO_START, 0.63f, 0.5f, 1.0f, 0.46f));
        out.push_back(std::move(s));
    }

    // New Nintendo 3DS (KTR-001, 2014): matte black housing, Circle Pad + D-pad, C-Stick, 4-color ABXY, L/ZL R/ZR.
    {
        TouchConsoleSpec s;
        s.system_id = "3ds";
        s.display_badge = "NINTENDO 3DS";
        s.shell_bg = gfx::Color::Rgb(0x1E2128);
        s.bezel_bg = gfx::Color::Rgb(0x111317);
        s.shell_trim = gfx::Color::Rgb(0x323742);
        s.dpad_fill = gfx::Color::Rgb(0x343844);
        s.stick_fill = gfx::Color::Rgb(0x888E99);
        s.cstick_fill = gfx::Color::Rgb(0x6D727D);
        s.accent = ShellAccent::DsSpeakerDots;
        s.power_led = true;
        s.dpad = DPadStyle::Cross;
        s.dpad_primary_left = false; // Circle Pad upper-left, D-Pad underneath
        s.left_stick = StickStyle::Concave;
        s.right_stick = StickStyle::Round;
        s.right_stick_rx = 0.16f;
        s.right_stick_ry = 0.20f;
        s.right_stick_scale = 0.48f; // compact C-stick nub above/left of ABXY
        s.deck_ratio = 0.40f;
        s.buttons.push_back(Shoulder("l", "L", 0x353944, 0xEDEFF4, RETRO_L, true, 0.30f, 0.5f, 1.2f, 0.68f));
        s.buttons.push_back(Shoulder("zl", "ZL", 0x454B5A, 0xEDEFF4, RETRO_L2, true, 0.72f, 0.5f, 1.1f, 0.64f));
        s.buttons.push_back(Shoulder("r", "R", 0x353944, 0xEDEFF4, RETRO_R, false, 0.70f, 0.5f, 1.2f, 0.68f));
        s.buttons.push_back(Shoulder("zr", "ZR", 0x454B5A, 0xEDEFF4, RETRO_R2, false, 0.28f, 0.5f, 1.1f, 0.64f));
        AddDiamond(s.buttons, 0.60f, 0.56f, 0.21f, 0.23f, "Y", "B", "X", "A", 0x3BA65A, 0xE2B62E, 0x3878D8,
                   0xDC4842, 0xFFFFFF, 0.88f);
        s.buttons.push_back(Pill("select", "SELECT", 0x2F3440, 0xE2E6F0, RETRO_SELECT, 0.28f, 0.5f, 0.92f, 0.46f));
        s.buttons.push_back(Pill("home", "HOME", 0x383E4C, 0xFFFFFF, RETRO_L3, 0.50f, 0.5f, 0.86f, 0.46f));
        s.buttons.push_back(Pill("start", "START", 0x2F3440, 0xE2E6F0, RETRO_START, 0.72f, 0.5f, 0.92f, 0.46f));
        out.push_back(std::move(s));
    }

    // Pokemon mini (2001): compact teal-blue shell, yellow-orange C shoulder, white B/A.
    {
        TouchConsoleSpec s;
        s.system_id = "pokemini";
        s.display_badge = "POKEMON MINI";
        s.shell_bg = gfx::Color::Rgb(0x287EA8);
        s.bezel_bg = gfx::Color::Rgb(0x1E2630);
        s.shell_trim = gfx::Color::Rgb(0xF0A824);
        s.dpad_fill = gfx::Color::Rgb(0x232B36);
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Shoulder("shake", "SHAKE", 0xD98E24, 0x1E1A12, RETRO_L, true),
            Shoulder("c", "C", 0xF0AC28, 0x1E1A12, RETRO_R, false),
            Face("b", "B", 0xEAEFF4, 0x1F2732, RETRO_B, 0.36f, 0.56f, 1.04f, 1.04f),
            Face("a", "A", 0xEAEFF4, 0x1F2732, RETRO_A, 0.72f, 0.38f, 1.04f, 1.04f),
            Pill("power", "PWR", 0x205A78, 0xF0F4F8, RETRO_SELECT, 0.50f, 0.5f, 1.05f, 0.48f),
        };
        out.push_back(std::move(s));
    }

    // NES (NES-004, 1985): warm light-grey shell, dark faceplate, iconic red square-framed B & A.
    {
        TouchConsoleSpec s;
        s.system_id = "nes";
        s.display_badge = "NINTENDO";
        s.shell_bg = gfx::Color::Rgb(0xCFCFC8);
        s.bezel_bg = gfx::Color::Rgb(0x212226);
        s.shell_trim = gfx::Color::Rgb(0x2A2C32);
        s.dpad_fill = gfx::Color::Rgb(0x1D1E22);
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("b", "B", 0xD92B28, 0xFFFFFF, RETRO_B, 0.35f, 0.52f, 1.06f, 1.06f, ButtonShape::SquareFramed),
            Face("a", "A", 0xD92B28, 0xFFFFFF, RETRO_A, 0.70f, 0.52f, 1.06f, 1.06f, ButtonShape::SquareFramed),
            Pill("select", "SELECT", 0x2A2B30, 0xEAEAE6, RETRO_SELECT, 0.37f, 0.5f, 1.08f, 0.46f),
            Pill("start", "START", 0x2A2B30, 0xEAEAE6, RETRO_START, 0.63f, 0.5f, 1.08f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Super Nintendo (SNS-005): cool grey body, purple/lavender X/Y/A/B diamond, L/R shoulders.
    {
        TouchConsoleSpec s;
        s.system_id = "snes";
        s.display_badge = "SUPER NINTENDO";
        s.shell_bg = gfx::Color::Rgb(0xC8C6CE);
        s.bezel_bg = gfx::Color::Rgb(0x22232A);
        s.shell_trim = gfx::Color::Rgb(0x7C7A86);
        s.dpad_fill = gfx::Color::Rgb(0x4E4F58);
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.41f;
        s.buttons.push_back(Shoulder("l", "L", 0xA8A6B2, 0x23242C, RETRO_L, true));
        s.buttons.push_back(Shoulder("r", "R", 0xA8A6B2, 0x23242C, RETRO_R, false));
        AddDiamond(s.buttons, 0.52f, 0.49f, 0.21f, 0.23f, "Y", "B", "X", "A", 0x9C8ECE, 0x5E45A0, 0x9C8ECE,
                   0x5E45A0, 0xFFFFFF, 0.94f);
        s.buttons.push_back(Pill("select", "SELECT", 0x5A5B64, 0xF0EFF6, RETRO_SELECT, 0.37f, 0.5f, 1.08f, 0.46f,
                                 ButtonShape::AngledPill));
        s.buttons.push_back(Pill("start", "START", 0x5A5B64, 0xF0EFF6, RETRO_START, 0.63f, 0.5f, 1.08f, 0.46f,
                                 ButtonShape::AngledPill));
        out.push_back(std::move(s));
    }

    // Virtual Boy (VUE-005, 1995): matte black + crimson red accents, Left & Right dual D-Pads + B/A.
    {
        TouchConsoleSpec s;
        s.system_id = "virtualboy";
        s.display_badge = "VIRTUAL BOY";
        s.shell_bg = gfx::Color::Rgb(0x181316);
        s.bezel_bg = gfx::Color::Rgb(0x120406);
        s.shell_trim = gfx::Color::Rgb(0xD82828);
        s.dpad_fill = gfx::Color::Rgb(0x2E2428);
        s.dpad = DPadStyle::Cross;
        s.right_dpad = true;
        s.right_dpad_drives_stick = true;
        s.deck_ratio = 0.44f;
        s.buttons = {
            Shoulder("l", "L", 0x382026, 0xFFD0D0, RETRO_L, true),
            Shoulder("r", "R", 0x382026, 0xFFD0D0, RETRO_R, false),
            Face("b", "B", 0xC82424, 0xFFFFFF, RETRO_B, 0.72f, 0.84f, 0.84f, 0.84f),
            Face("a", "A", 0xD82C2C, 0xFFFFFF, RETRO_A, 0.88f, 0.62f, 0.84f, 0.84f),
            Pill("select", "SELECT", 0x3A2026, 0xFFD6D6, RETRO_SELECT, 0.37f, 0.5f, 1.05f, 0.46f),
            Pill("start", "START", 0x3A2026, 0xFFD6D6, RETRO_START, 0.63f, 0.5f, 1.05f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Nintendo 64 (NUS-005, 1996): dark charcoal trident deck, central octagonal stick, D-pad, A/B, Z/L/R, 4 C-buttons, red Start.
    {
        TouchConsoleSpec s;
        s.system_id = "n64";
        s.display_badge = "NINTENDO 64";
        s.shell_bg = gfx::Color::Rgb(0x292B32);
        s.bezel_bg = gfx::Color::Rgb(0x13151A);
        s.shell_trim = gfx::Color::Rgb(0x464B58);
        s.dpad_fill = gfx::Color::Rgb(0x464A54);
        s.stick_fill = gfx::Color::Rgb(0xC2C6CE);
        s.dpad = DPadStyle::Cross;
        s.dpad_primary_left = false; // 3D analog stick primary, D-pad tucked cleanly under it
        s.left_stick = StickStyle::Octagon;
        s.deck_ratio = 0.44f;
        s.buttons = {
            Shoulder("l", "L", 0x4E5360, 0xF2F4F8, RETRO_L, true, 0.28f, 0.5f, 1.15f, 0.68f),
            Shoulder("z", "Z", 0x626878, 0xFFFFFF, RETRO_L2, true, 0.72f, 0.5f, 1.15f, 0.68f),
            Shoulder("r", "R", 0x4E5360, 0xF2F4F8, RETRO_R, false),
            Face("b", "B", 0x269E4A, 0xFFFFFF, RETRO_Y, 0.22f, 0.54f, 0.92f, 0.92f),
            Face("a", "A", 0x2868D8, 0xFFFFFF, RETRO_B, 0.36f, 0.78f, 1.05f, 1.05f),
            StickDir("cu", "", 0xE6B824, 0x241B04, true, 0, 0.72f, 0.17f, 0.74f, ButtonGlyph::ArrowUp),
            StickDir("cd", "", 0xE6B824, 0x241B04, true, 1, 0.72f, 0.49f, 0.74f, ButtonGlyph::ArrowDown),
            StickDir("cl", "", 0xE6B824, 0x241B04, true, 2, 0.55f, 0.33f, 0.74f, ButtonGlyph::ArrowLeft),
            StickDir("cr", "", 0xE6B824, 0x241B04, true, 3, 0.89f, 0.33f, 0.74f, ButtonGlyph::ArrowRight),
            Pill("start", "START", 0xD82C28, 0xFFFFFF, RETRO_START, 0.50f, 0.5f, 0.95f, 0.56f),
        };
        out.push_back(std::move(s));
    }

    // Nintendo GameCube (DOL-003, 2001): indigo shell, grey main stick, yellow C-stick, green A, red B, X/Y, purple Z.
    {
        TouchConsoleSpec s;
        s.system_id = "gamecube";
        s.display_badge = "GAMECUBE";
        s.shell_bg = gfx::Color::Rgb(0x4E429E);
        s.bezel_bg = gfx::Color::Rgb(0x18152C);
        s.shell_trim = gfx::Color::Rgb(0x6D62BA);
        s.dpad_fill = gfx::Color::Rgb(0xC2C6CE);
        s.stick_fill = gfx::Color::Rgb(0xC8CCD4);
        s.cstick_fill = gfx::Color::Rgb(0xE8BA26);
        s.dpad = DPadStyle::Cross;
        s.dpad_primary_left = false;
        s.left_stick = StickStyle::Octagon;
        s.right_stick = StickStyle::Octagon;
        s.right_stick_rx = 0.24f;
        s.right_stick_ry = 0.77f;
        s.right_stick_scale = 0.78f;
        s.deck_ratio = 0.45f;
        s.buttons = {
            Shoulder("l", "L", 0xB8BCC6, 0x222038, RETRO_L2, true),
            Shoulder("z", "Z", 0x6A5EC8, 0xFFFFFF, RETRO_R, false, 0.28f, 0.5f, 1.05f, 0.64f),
            Shoulder("r", "R", 0xB8BCC6, 0x222038, RETRO_R2, false, 0.72f, 0.5f, 1.25f, 0.68f),
            Face("a", "A", 0x24A860, 0xFFFFFF, RETRO_A, 0.60f, 0.44f, 1.15f, 1.15f),
            Face("b", "B", 0xDA3434, 0xFFFFFF, RETRO_B, 0.34f, 0.50f, 0.78f, 0.78f),
            Face("y", "Y", 0xC2C6D0, 0x222434, RETRO_Y, 0.58f, 0.16f, 0.86f, 0.70f),
            Face("x", "X", 0xC2C6D0, 0x222434, RETRO_X, 0.86f, 0.42f, 0.70f, 0.86f),
            Pill("start", "START", 0xC2C6D0, 0x222434, RETRO_START, 0.50f, 0.5f, 0.88f, 0.48f),
        };
        out.push_back(std::move(s));
    }

    // Wii (RVL-003 + Nunchuk, 2006): glossy white with ice-blue accents, Nunchuk stick + C/Z, A/B, 1/2, -/+, Shake.
    {
        TouchConsoleSpec s;
        s.system_id = "wii";
        s.display_badge = "WII";
        s.shell_bg = gfx::Color::Rgb(0xEDEFF4);
        s.bezel_bg = gfx::Color::Rgb(0x1D2026);
        s.shell_trim = gfx::Color::Rgb(0x4AA6EA);
        s.dpad_fill = gfx::Color::Rgb(0xD8DCE4);
        s.stick_fill = gfx::Color::Rgb(0xF6F8FC);
        s.dpad = DPadStyle::Cross;
        s.dpad_primary_left = false;
        s.left_stick = StickStyle::Octagon;
        s.deck_ratio = 0.44f;
        s.buttons = {
            Shoulder("c", "C", 0xD8DCE4, 0x252A34, RETRO_L, true, 0.30f, 0.5f, 1.05f, 0.64f),
            Shoulder("z", "Z", 0xCBD0DA, 0x252A34, RETRO_R, true, 0.72f, 0.5f, 1.15f, 0.68f),
            Shoulder("shake", "SHAKE", 0x4AA6EA, 0xFFFFFF, RETRO_R2, false, 0.5f, 0.5f, 1.55f, 0.68f),
            Face("a", "A", 0xFFFFFF, 0x222834, RETRO_A, 0.44f, 0.34f, 1.12f, 1.12f),
            Face("b", "B", 0xDCE0E8, 0x222834, RETRO_B, 0.78f, 0.38f, 0.92f, 0.92f),
            Face("1", "1", 0xD8DCE4, 0x222834, RETRO_X, 0.34f, 0.74f, 0.82f, 0.82f),
            Face("2", "2", 0xD8DCE4, 0x222834, RETRO_Y, 0.66f, 0.74f, 0.82f, 0.82f),
            Pill("minus", "-", 0xCBD0DA, 0x222834, RETRO_SELECT, 0.37f, 0.5f, 0.86f, 0.46f),
            Pill("plus", "+", 0xCBD0DA, 0x222834, RETRO_START, 0.63f, 0.5f, 0.86f, 0.46f),
        };
        out.push_back(std::move(s));
    }
}

} // namespace den::app::touch_detail
