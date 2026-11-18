// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Hardware-inspired touch deck specifications for NEC, SNK, Bandai, Atari, Arcade, PICO-8, and
// universal fallback systems, plus the unified system registry lookup.

#include <array>

#include "touch_catalog_internal.h"

namespace den::app::touch_detail {

void RegisterOtherSpecs(std::vector<TouchConsoleSpec>& out) {
    // PC Engine / TurboGrafx-16: warm off-white body, orange-red I/II + Avenue Pad III-VI & Select/Run.
    {
        TouchConsoleSpec s;
        s.system_id = "pce";
        s.display_badge = "PC ENGINE";
        s.shell_bg = gfx::Color::Rgb(0xE8E5DC);
        s.bezel_bg = gfx::Color::Rgb(0x222328);
        s.shell_trim = gfx::Color::Rgb(0xD84228);
        s.dpad_fill = gfx::Color::Rgb(0x282A30);
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.42f;
        s.buttons = {
            Face("iii", "III", 0xC6C2B8, 0x24262C, RETRO_Y, 0.24f, 0.26f, 0.72f, 0.72f),
            Face("iv", "IV", 0xC6C2B8, 0x24262C, RETRO_X, 0.52f, 0.22f, 0.72f, 0.72f),
            Face("vi", "VI", 0xC6C2B8, 0x24262C, RETRO_R, 0.80f, 0.26f, 0.72f, 0.72f),
            Face("ii", "II", 0xD84228, 0xFFFFFF, RETRO_B, 0.36f, 0.66f, 1.04f, 1.04f),
            Face("i", "I", 0xD84228, 0xFFFFFF, RETRO_A, 0.72f, 0.54f, 1.04f, 1.04f),
            Pill("select", "SELECT", 0x52555E, 0xFFFFFF, RETRO_SELECT, 0.37f, 0.5f, 1.08f, 0.46f),
            Pill("run", "RUN", 0x52555E, 0xFFFFFF, RETRO_START, 0.63f, 0.5f, 1.08f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Neo Geo Pocket & Neo Geo Pocket Color: microswitched clicky thumbstick + B/A + Option.
    for (const auto& [id, badge, shell] : {
             std::tuple{"ngp", "NEO GEO POCKET", 0x242830U},
             std::tuple{"ngpc", "NEO GEO POCKET COLOR", 0x263A6CU},
         }) {
        TouchConsoleSpec s;
        s.system_id = id;
        s.display_badge = badge;
        s.shell_bg = gfx::Color::Rgb(shell);
        s.bezel_bg = gfx::Color::Rgb(0x161820);
        s.shell_trim = gfx::Color::Rgb(0xD83A32);
        s.stick_fill = gfx::Color::Rgb(0x1D2028);
        s.power_led = true;
        s.dpad = DPadStyle::None;
        s.left_stick = StickStyle::Clicky;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("b", "B", 0x1D2028, 0xF0F2F6, RETRO_B, 0.36f, 0.56f, 1.08f, 1.08f),
            Face("a", "A", 0x1D2028, 0xF0F2F6, RETRO_A, 0.72f, 0.38f, 1.08f, 1.08f),
            Pill("option", "OPTION", 0x3D4250, 0xFFFFFF, RETRO_START, 0.50f, 0.5f, 1.2f, 0.48f),
        };
        out.push_back(std::move(s));
    }

    // WonderSwan & WonderSwan Color: cool silver/blue shell, X-cursor + Y-cursor, B/A, Start/Rotate.
    for (const auto& [id, badge, shell] : {
             std::tuple{"wonderswan", "WONDERSWAN", 0xC8CED8U},
             std::tuple{"wsc", "WONDERSWAN COLOR", 0x30589EU},
         }) {
        TouchConsoleSpec s;
        s.system_id = id;
        s.display_badge = badge;
        s.shell_bg = gfx::Color::Rgb(shell);
        s.bezel_bg = gfx::Color::Rgb(0x1D222C);
        s.shell_trim = gfx::Color::Rgb(0x526480);
        s.dpad_fill = gfx::Color::Rgb(0x2E3440);
        s.dpad = DPadStyle::Cross;
        s.right_dpad = true;
        s.right_dpad_drives_stick = false; // Y-buttons live cleanly next to A/B; Rotate mapped to Select
        s.deck_ratio = 0.43f;
        const std::uint32_t btn_bg = id[2] == 'c' ? 0x223868U : 0x4A5260U;
        s.buttons = {
            Face("b", "B", btn_bg, 0xFFFFFF, RETRO_B, 0.68f, 0.84f, 0.86f, 0.86f),
            Face("a", "A", btn_bg, 0xFFFFFF, RETRO_A, 0.88f, 0.64f, 0.86f, 0.86f),
            Pill("rotate", "ROTATE", 0x3E4654, 0xFFFFFF, RETRO_SELECT, 0.36f, 0.5f, 1.08f, 0.46f),
            Pill("start", "START", 0x3E4654, 0xFFFFFF, RETRO_START, 0.64f, 0.5f, 1.08f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Atari 2600: iconic walnut woodgrain trim + matte black deck, joystick, single big red Fire button.
    {
        TouchConsoleSpec s;
        s.system_id = "atari2600";
        s.display_badge = "ATARI 2600";
        s.shell_bg = gfx::Color::Rgb(0x1B1918);
        s.bezel_bg = gfx::Color::Rgb(0x12100F);
        s.shell_trim = gfx::Color::Rgb(0x704324);
        s.stick_fill = gfx::Color::Rgb(0x2E2C2A);
        s.accent = ShellAccent::WoodgrainStrip;
        s.dpad = DPadStyle::None;
        s.left_stick = StickStyle::Clicky;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("fire", "FIRE", 0xDA3228, 0xFFFFFF, RETRO_B, 0.54f, 0.48f, 1.32f, 1.32f),
            Pill("diff_l", "DIF L", 0x4A443E, 0xEDEAE4, RETRO_L, 0.22f, 0.5f, 0.90f, 0.46f),
            Pill("select", "SELECT", 0x625B54, 0xFFFFFF, RETRO_SELECT, 0.46f, 0.5f, 0.95f, 0.46f),
            Pill("reset", "RESET", 0x625B54, 0xFFFFFF, RETRO_START, 0.72f, 0.5f, 0.95f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Atari 5200: brushed black shell, analog stick, dual red Fire buttons, Start/Pause.
    {
        TouchConsoleSpec s;
        s.system_id = "atari5200";
        s.display_badge = "ATARI 5200";
        s.shell_bg = gfx::Color::Rgb(0x202226);
        s.bezel_bg = gfx::Color::Rgb(0x131518);
        s.shell_trim = gfx::Color::Rgb(0x888E98);
        s.stick_fill = gfx::Color::Rgb(0x32363E);
        s.accent = ShellAccent::MetalStrip;
        s.dpad = DPadStyle::None;
        s.left_stick = StickStyle::Round;
        s.deck_ratio = 0.41f;
        s.buttons = {
            Face("fire1", "FIRE 1", 0xD8322C, 0xFFFFFF, RETRO_B, 0.36f, 0.58f, 1.05f, 1.05f),
            Face("fire2", "FIRE 2", 0xD8322C, 0xFFFFFF, RETRO_A, 0.72f, 0.38f, 1.05f, 1.05f),
            Pill("pause", "PAUSE", 0x464B56, 0xF0F2F6, RETRO_SELECT, 0.37f, 0.5f, 1.05f, 0.46f),
            Pill("start", "START", 0x464B56, 0xF0F2F6, RETRO_START, 0.63f, 0.5f, 1.05f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Atari 7800: matte black with brushed aluminum band, cross D-pad, red 1 & 2 buttons, Select/Pause/Reset.
    {
        TouchConsoleSpec s;
        s.system_id = "atari7800";
        s.display_badge = "ATARI 7800";
        s.shell_bg = gfx::Color::Rgb(0x1D1F24);
        s.bezel_bg = gfx::Color::Rgb(0x121317);
        s.shell_trim = gfx::Color::Rgb(0x989EA8);
        s.dpad_fill = gfx::Color::Rgb(0x30343C);
        s.accent = ShellAccent::MetalStrip;
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.41f;
        s.buttons = {
            Face("1", "1", 0xD8342E, 0xFFFFFF, RETRO_B, 0.36f, 0.56f, 1.06f, 1.06f),
            Face("2", "2", 0xD8342E, 0xFFFFFF, RETRO_A, 0.72f, 0.40f, 1.06f, 1.06f),
            Pill("select", "SELECT", 0x4E535E, 0xFFFFFF, RETRO_SELECT, 0.28f, 0.5f, 0.92f, 0.46f),
            Pill("pause", "PAUSE", 0x4E535E, 0xFFFFFF, RETRO_START, 0.52f, 0.5f, 0.92f, 0.46f),
            Pill("reset", "RESET", 0x4E535E, 0xFFFFFF, RETRO_X, 0.76f, 0.5f, 0.92f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Atari Lynx: industrial slate grey handheld with B/A and Opt 1 / Pause / Opt 2.
    {
        TouchConsoleSpec s;
        s.system_id = "lynx";
        s.display_badge = "ATARI LYNX";
        s.shell_bg = gfx::Color::Rgb(0x2C3038);
        s.bezel_bg = gfx::Color::Rgb(0x181A1F);
        s.shell_trim = gfx::Color::Rgb(0x6E7482);
        s.dpad_fill = gfx::Color::Rgb(0x3D424D);
        s.power_led = true;
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.41f;
        s.buttons = {
            Face("b", "B", 0x454B58, 0xFFFFFF, RETRO_B, 0.36f, 0.56f, 1.06f, 1.06f),
            Face("a", "A", 0x454B58, 0xFFFFFF, RETRO_A, 0.72f, 0.38f, 1.06f, 1.06f),
            Pill("opt1", "OPT 1", 0x4D5360, 0xF0F2F6, RETRO_L, 0.28f, 0.5f, 0.92f, 0.46f),
            Pill("pause", "PAUSE", 0xD84830, 0xFFFFFF, RETRO_START, 0.52f, 0.5f, 0.96f, 0.46f),
            Pill("opt2", "OPT 2", 0x4D5360, 0xF0F2F6, RETRO_R, 0.76f, 0.5f, 0.92f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Atari Jaguar: deep charcoal body, sloped crimson A/B/C buttons, Pause/Option.
    {
        TouchConsoleSpec s;
        s.system_id = "jaguar";
        s.display_badge = "ATARI JAGUAR";
        s.shell_bg = gfx::Color::Rgb(0x212328);
        s.bezel_bg = gfx::Color::Rgb(0x131518);
        s.shell_trim = gfx::Color::Rgb(0xDC302C);
        s.dpad_fill = gfx::Color::Rgb(0x343842);
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.41f;
        s.buttons = {
            Face("a", "A", 0xD6302E, 0xFFFFFF, RETRO_Y, 0.20f, 0.62f, 0.92f, 0.92f),
            Face("b", "B", 0xD6302E, 0xFFFFFF, RETRO_B, 0.50f, 0.48f, 0.92f, 0.92f),
            Face("c", "C", 0xD6302E, 0xFFFFFF, RETRO_A, 0.80f, 0.34f, 0.92f, 0.92f),
            Pill("pause", "PAUSE", 0xD6302E, 0xFFFFFF, RETRO_SELECT, 0.37f, 0.5f, 1.05f, 0.46f),
            Pill("option", "OPTION", 0x424752, 0xFFFFFF, RETRO_START, 0.63f, 0.5f, 1.05f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Vectrex: pitch black vector panel with glowing ice-blue trim, stick + 1 2 3 4 row.
    {
        TouchConsoleSpec s;
        s.system_id = "vectrex";
        s.display_badge = "VECTREX";
        s.shell_bg = gfx::Color::Rgb(0x12151B);
        s.bezel_bg = gfx::Color::Rgb(0x090B0E);
        s.shell_trim = gfx::Color::Rgb(0x6ED4FF);
        s.stick_fill = gfx::Color::Rgb(0x252B36);
        s.dpad = DPadStyle::None;
        s.left_stick = StickStyle::Round;
        s.deck_ratio = 0.41f;
        s.buttons = {
            Face("1", "1", 0x263244, 0x9EE8FF, RETRO_A, 0.16f, 0.54f, 0.78f, 0.78f),
            Face("2", "2", 0x263244, 0x9EE8FF, RETRO_B, 0.39f, 0.48f, 0.78f, 0.78f),
            Face("3", "3", 0x263244, 0x9EE8FF, RETRO_X, 0.62f, 0.42f, 0.78f, 0.78f),
            Face("4", "4", 0x263244, 0x9EE8FF, RETRO_Y, 0.85f, 0.36f, 0.78f, 0.78f),
            Pill("select", "SELECT", 0x232A36, 0xD2EEFF, RETRO_SELECT, 0.37f, 0.5f, 1.05f, 0.46f),
            Pill("start", "START", 0x232A36, 0xD2EEFF, RETRO_START, 0.63f, 0.5f, 1.05f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // MSX: deep charcoal-navy home computer pad with A/B & F-keys / Start / Select.
    {
        TouchConsoleSpec s;
        s.system_id = "msx";
        s.display_badge = "MSX";
        s.shell_bg = gfx::Color::Rgb(0x1D232E);
        s.bezel_bg = gfx::Color::Rgb(0x11151C);
        s.shell_trim = gfx::Color::Rgb(0x528CE0);
        s.dpad_fill = gfx::Color::Rgb(0x31394A);
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("a", "A / 1", 0x364258, 0xF2F5FA, RETRO_B, 0.36f, 0.56f, 1.02f, 1.02f),
            Face("b", "B / 2", 0x364258, 0xF2F5FA, RETRO_A, 0.72f, 0.38f, 1.02f, 1.02f),
            Pill("select", "SELECT", 0x31394A, 0xE6EBF5, RETRO_SELECT, 0.37f, 0.5f, 1.05f, 0.46f),
            Pill("start", "START", 0x31394A, 0xE6EBF5, RETRO_START, 0.63f, 0.5f, 1.05f, 0.46f),
        };
        out.push_back(std::move(s));
    }

    // Arcade: Japanese candy-cabinet control deck, red ball-top joystick + 6 Sanwa buttons + Coin & 1P.
    {
        TouchConsoleSpec s;
        s.system_id = "arcade";
        s.display_badge = "ARCADE";
        s.shell_bg = gfx::Color::Rgb(0x232730);
        s.bezel_bg = gfx::Color::Rgb(0x121419);
        s.shell_trim = gfx::Color::Rgb(0xE24038);
        s.stick_fill = gfx::Color::Rgb(0xE23C34);
        s.accent = ShellAccent::MetalStrip;
        s.dpad = DPadStyle::None;
        s.left_stick = StickStyle::ArcadeBall;
        s.deck_ratio = 0.44f;
        s.buttons = {
            Face("y", "1", 0xE24038, 0xFFFFFF, RETRO_Y, 0.22f, 0.30f, 0.84f, 0.84f),
            Face("x", "2", 0xE8B828, 0x201804, RETRO_X, 0.50f, 0.25f, 0.84f, 0.84f),
            Face("l", "3", 0x3680DC, 0xFFFFFF, RETRO_L, 0.78f, 0.30f, 0.84f, 0.84f),
            Face("b", "4", 0xE24038, 0xFFFFFF, RETRO_B, 0.20f, 0.68f, 0.90f, 0.90f),
            Face("a", "5", 0xE8B828, 0x201804, RETRO_A, 0.48f, 0.63f, 0.90f, 0.90f),
            Face("r", "6", 0x3680DC, 0xFFFFFF, RETRO_R, 0.76f, 0.68f, 0.90f, 0.90f),
            Pill("coin", "COIN", 0x4A4E5A, 0xFFFFFF, RETRO_SELECT, 0.37f, 0.5f, 1.05f, 0.48f),
            Pill("start", "1P", 0xF0F2F6, 0x1E2028, RETRO_START, 0.63f, 0.5f, 1.05f, 0.48f),
        };
        out.push_back(std::move(s));
    }

    // PICO-8: dark PICO-8 navy housing with cream D-pad and signature O (#FF004D) + X (#29ADFF) buttons.
    {
        TouchConsoleSpec s;
        s.system_id = "pico8";
        s.display_badge = "PICO-8";
        s.shell_bg = gfx::Color::Rgb(0x1D2B53);
        s.bezel_bg = gfx::Color::Rgb(0x0D1428);
        s.shell_trim = gfx::Color::Rgb(0xFF004D);
        s.dpad_fill = gfx::Color::Rgb(0xFFF1E8);
        s.dpad = DPadStyle::Cross;
        s.deck_ratio = 0.40f;
        s.buttons = {
            Face("o", "O", 0xFF004D, 0xFFF1E8, RETRO_B, 0.36f, 0.56f, 1.08f, 1.08f),
            Face("x", "X", 0x29ADFF, 0xFFF1E8, RETRO_A, 0.72f, 0.38f, 1.08f, 1.08f),
            Pill("pause", "PAUSE", 0x5F574F, 0xFFF1E8, RETRO_START, 0.50f, 0.5f, 1.15f, 0.48f),
        };
        out.push_back(std::move(s));
    }

    // Universal Modern Pro / Steam Deck-inspired layout (used for "steam" and any custom core fallback).
    {
        TouchConsoleSpec s;
        s.system_id = "steam";
        s.display_badge = "CONTROLLER";
        s.shell_bg = gfx::Color::Rgb(0x1A1D24);
        s.bezel_bg = gfx::Color::Rgb(0x0E1014);
        s.shell_trim = gfx::Color::Rgb(0x4A9EFF);
        s.dpad_fill = gfx::Color::Rgb(0x303440);
        s.stick_fill = gfx::Color::Rgb(0x2E323E);
        s.cstick_fill = gfx::Color::Rgb(0x2E323E);
        s.dpad = DPadStyle::Cross;
        s.dpad_primary_left = false;
        s.left_stick = StickStyle::Concave;
        s.right_stick = StickStyle::Concave;
        s.right_stick_rx = 0.24f;
        s.right_stick_ry = 0.78f;
        s.right_stick_scale = 0.78f;
        s.deck_ratio = 0.44f;
        s.buttons.push_back(Shoulder("l2", "L2", 0x2A2F3A, 0xEAECEF, RETRO_L2, true, 0.28f, 0.5f, 1.1f, 0.66f));
        s.buttons.push_back(Shoulder("l1", "L1", 0x353B48, 0xFFFFFF, RETRO_L, true, 0.72f, 0.5f, 1.15f, 0.68f));
        s.buttons.push_back(Shoulder("r1", "R1", 0x353B48, 0xFFFFFF, RETRO_R, false, 0.28f, 0.5f, 1.15f, 0.68f));
        s.buttons.push_back(Shoulder("r2", "R2", 0x2A2F3A, 0xEAECEF, RETRO_R2, false, 0.72f, 0.5f, 1.1f, 0.66f));
        AddDiamond(s.buttons, 0.60f, 0.36f, 0.19f, 0.21f, "X", "A", "Y", "B", 0x3478D8, 0x30A658, 0xE2B624,
                   0xDC4842, 0xFFFFFF, 0.84f);
        s.buttons.push_back(Pill("select", "VIEW", 0x2E333E, 0xE4E8F0, RETRO_SELECT, 0.37f, 0.5f, 1.0f, 0.46f));
        s.buttons.push_back(Pill("start", "MENU", 0x2E333E, 0xE4E8F0, RETRO_START, 0.63f, 0.5f, 1.0f, 0.46f));
        out.push_back(std::move(s));
    }
}

} // namespace den::app::touch_detail

namespace den::app {

namespace {

const std::vector<TouchConsoleSpec>& Catalog() {
    static const std::vector<TouchConsoleSpec> specs = [] {
        std::vector<TouchConsoleSpec> list;
        list.reserve(38);
        touch_detail::RegisterNintendoSpecs(list);
        touch_detail::RegisterSegaSonySpecs(list);
        touch_detail::RegisterOtherSpecs(list);
        return list;
    }();
    return specs;
}

} // namespace

const TouchConsoleSpec& TouchSpecForSystem(std::string_view system_id) {
    const std::string_view key = system_id == "n3ds" ? std::string_view("3ds") : system_id;
    const auto& list = Catalog();
    for (const TouchConsoleSpec& spec : list) {
        if (spec.system_id == key) {
            return spec;
        }
    }
    return list.back(); // "steam" universal controller fallback
}

std::span<const std::string_view> AllBundledTouchSystems() {
    static const std::array<std::string_view, 37> SYSTEMS = {
        "32x",      "3ds",        "arcade",     "atari2600", "atari5200", "atari7800", "dreamcast", "gamecube",
        "gb",       "gba",        "gbc",        "genesis",   "gg",        "jaguar",    "lynx",      "msx",
        "n64",      "nds",        "nes",        "ngp",       "ngpc",      "pce",       "pico8",     "pokemini",
        "ps2",      "psp",        "psx",        "saturn",    "sg1000",    "sms",       "snes",      "steam",
        "vectrex",  "virtualboy", "wii",        "wonderswan", "wsc",
    };
    return SYSTEMS;
}

} // namespace den::app
