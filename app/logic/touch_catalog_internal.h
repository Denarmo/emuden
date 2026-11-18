// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Internal helpers for building the per-console touch deck catalog across translation units.

#pragma once

#include <vector>

#include "touch_spec.h"

namespace den::app::touch_detail {

inline TouchButtonDef Face(const char* id, const char* label, std::uint32_t fill, std::uint32_t txt,
                           int bit, float rx, float ry, float w_scale = 1.0f, float h_scale = 1.0f,
                           ButtonShape shape = ButtonShape::Circle,
                           ButtonGlyph glyph = ButtonGlyph::None) {
    return TouchButtonDef{
        id,
        label,
        shape,
        glyph,
        gfx::Color::Rgb(fill),
        gfx::Color::Rgb(txt),
        TouchTargetKind::ButtonBit,
        bit,
        TouchZone::RightSide,
        rx,
        ry,
        w_scale,
        h_scale,
        shape == ButtonShape::Circle || shape == ButtonShape::SquareFramed,
    };
}

inline TouchButtonDef Pill(const char* id, const char* label, std::uint32_t fill, std::uint32_t txt,
                           int bit, float rx, float ry, float w_scale = 1.15f, float h_scale = 0.52f,
                           ButtonShape shape = ButtonShape::Pill) {
    return TouchButtonDef{
        id,
        label,
        shape,
        ButtonGlyph::None,
        gfx::Color::Rgb(fill),
        gfx::Color::Rgb(txt),
        TouchTargetKind::ButtonBit,
        bit,
        TouchZone::CenterBottom,
        rx,
        ry,
        w_scale,
        h_scale,
        false,
    };
}

inline TouchButtonDef Shoulder(const char* id, const char* label, std::uint32_t fill, std::uint32_t txt,
                               int bit, bool left, float rx = 0.5f, float ry = 0.5f,
                               float w_scale = 1.55f, float h_scale = 0.72f) {
    return TouchButtonDef{
        id,
        label,
        ButtonShape::Bumper,
        ButtonGlyph::None,
        gfx::Color::Rgb(fill),
        gfx::Color::Rgb(txt),
        TouchTargetKind::ButtonBit,
        bit,
        left ? TouchZone::LeftShoulder : TouchZone::RightShoulder,
        rx,
        ry,
        w_scale,
        h_scale,
        false,
    };
}

inline TouchButtonDef StickDir(const char* id, const char* label, std::uint32_t fill, std::uint32_t txt,
                               bool right_stick, int dir, float rx, float ry, float scale = 0.74f,
                               ButtonGlyph glyph = ButtonGlyph::None) {
    return TouchButtonDef{
        id,
        label,
        ButtonShape::Circle,
        glyph,
        gfx::Color::Rgb(fill),
        gfx::Color::Rgb(txt),
        right_stick ? TouchTargetKind::RightStickDir : TouchTargetKind::LeftStickDir,
        dir,
        TouchZone::RightSide,
        rx,
        ry,
        scale,
        scale,
        true,
    };
}

inline void AddDiamond(std::vector<TouchButtonDef>& out, float cx, float cy, float dx, float dy,
                       const char* l_y, const char* l_b, const char* l_x, const char* l_a,
                       std::uint32_t c_y, std::uint32_t c_b, std::uint32_t c_x, std::uint32_t c_a,
                       std::uint32_t txt, float scale = 0.94f) {
    out.push_back(Face("y", l_y, c_y, txt, RETRO_Y, cx - dx, cy, scale, scale));
    out.push_back(Face("b", l_b, c_b, txt, RETRO_B, cx, cy + dy, scale, scale));
    out.push_back(Face("x", l_x, c_x, txt, RETRO_X, cx, cy - dy, scale, scale));
    out.push_back(Face("a", l_a, c_a, txt, RETRO_A, cx + dx, cy, scale, scale));
}

void RegisterNintendoSpecs(std::vector<TouchConsoleSpec>& out);
void RegisterSegaSonySpecs(std::vector<TouchConsoleSpec>& out);
void RegisterOtherSpecs(std::vector<TouchConsoleSpec>& out);

} // namespace den::app::touch_detail
