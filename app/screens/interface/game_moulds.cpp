// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The cartridges and cards of particular consoles, shaped after photographs of the real ones:
// the outline of each mould and the relief on its front (ridges, ribs, a slot, contacts),
// and the UMD's shell. Plain shapes and the handheld moulds are in game_objects.cpp.

#include <algorithm>
#include <cmath>
#include <string_view>

#include "object_build.h"

namespace den::app {

namespace {

using gfx::Part;
using gfx::RoundedOutline;
using gfx::Vec2;

constexpr float PI = 3.14159265f;

enum class Outline {
    Rect,         // rounded top and bottom corners
    StepBottom,   // the sides step in near the bottom (the end that goes in)
    ArchTop,      // smooth curved arch across the top (Nintendo 64, Mega Drive)
    Chamfer,      // both top corners cut off at 45 degrees
    Advance,      // GBA: narrower bottom strip and outward-flared top grip lip
    Card,         // Nintendo DS card: the bottom narrower, the top right corner notched
    Card3ds,      // the same with the 3DS's tab standing out at the top right
    SwitchCard,   // Nintendo Switch / Switch 2 game card: rounded top, stepped bottom
    MasterSystem, // slight upward taper
};

struct Mould {
    std::string_view name, shape;
    float aspect, depth;
    gfx::Color shell;
    gfx::Rect label;
    std::string_view label_edge;
    Outline outline = Outline::Rect;
    float r_top = 0.03f, r_bottom = 0.02f;
    float cut_w = 0, cut_h = 0; // step, arch height, or chamfer's size
    int flutes = 0;             // vertical grooves beside the label
    int ribs = 0;               // horizontal ribs beside the label
    int grips = 0;              // horizontal grooves in the free part of the front
    bool slot = false;          // a long groove under the label
    bool lock_pockets = false;  // SNES twin bottom-front lock pockets
    bool screws = false;        // two screws low on the front
    bool arrow = false;         // an arrow in the free part, pointing the way in
    float contacts = 0;         // gold contacts on a dark strip this tall: bottom (> 0) or top (< 0)
    float band = 0;             // a darker band this tall across the bottom
    bool handle = false;        // a handle along the top
    bool ridge = false;         // a ridge along the top
    float top_band = 0;         // a recessed band this tall across the top, between the corners
    bool top_bulge = false;     // GBC raised convex oval along the top
    bool nes_front = false;     // asymmetric NES left fluted channel + thumb pocket
    bool md_notch = false;      // Mega Drive left-edge lock notch
    bool side_grips = false;    // N64/GBC side grip relief
};

// Proportions are the front's width over its height; depth and label in heights.
constexpr Mould MOULDS[] = {
    {"pocket_wide", "cartridge", 1.73f, 0.217f, {52, 54, 60, 255}, {0.11f, 0.24f, 0.78f, 0.61f}, "none",
     Outline::Advance, 0.03f, 0.02f, 0.026f, 0.16f, 0, 0, 0, false, false, false, true, 0, 0, false, false, 0.13f},
    {"gbc", "cartridge", 0.877f, 0.126f, {58, 61, 68, 168}, {0.105f, 0.28f, 0.79f, 0.57f}, "paper",
     Outline::Rect, 0.045f, 0.02f, 0, 0, 0, 0, 0, false, false, false, true, 0, 0, false, false, 0, true, false,
     false, true},
    // 33 x 35 x 3.8 mm: the bottom 7 mm (the end with the contacts) is 1 mm narrower each side.
    {"nds", "cartridge", 0.943f, 0.109f, {48, 50, 56, 255}, {0.09f, 0.08f, 0.82f, 0.68f}, "none", Outline::Card,
     0.035f, 0.03f, 0.03f, 0.2f, 0, 0, 0, false, false, false, true},
    {"n3ds", "cartridge", 0.943f, 0.109f, {222, 224, 228, 255}, {0.09f, 0.08f, 0.82f, 0.68f}, "none",
     Outline::Card3ds, 0.035f, 0.03f, 0.03f, 0.2f, 0, 0, 0, false, false, false, true},
    // 21 x 31 x 3.8 mm Switch / Switch 2 game card.
    {"switch", "card", 0.677f, 0.123f, {46, 48, 54, 255}, {0.09f, 0.07f, 0.82f, 0.75f}, "none", Outline::SwitchCard,
     0.065f, 0.035f, 0.018f, 0.14f, 0, 0, 0, false, false, false, true},
    // 120 x 133 x 20 mm 72-pin NES cartridge.
    {"nes", "cartridge", 0.902f, 0.150f, {134, 136, 140, 255}, {0.29f, 0.0f, 0.59f, 0.65f}, "none",
     Outline::StepBottom, 0.015f, 0.018f, 0.032f, 0.095f, 0, 0, 0, false, false, false, true, 0, 0, false, false, 0,
     false, true},
    // 136 x 85 x 20 mm NA SNES cartridge.
    {"snes", "cartridge", 1.58f, 0.23f, {166, 167, 173, 255}, {0.17f, 0.0f, 0.66f, 0.48f}, "none",
     Outline::StepBottom, 0.05f, 0.025f, 0.035f, 0.26f, 0, 0, 0, true, true},
    // 116 x 74 x 19 mm Nintendo 64 cartridge.
    {"n64", "cartridge", 1.568f, 0.25f, {132, 135, 142, 255}, {0.29f, 0.12f, 0.42f, 0.76f}, "none", Outline::ArchTop,
     0.06f, 0.03f, 0, 0.15f, 0, 0, 0, false, false, false, false, 0, 0, false, false, 0, false, false, false, true},
    // 109 x 70 x 16.5 mm Mega Drive / Genesis cartridge.
    {"megadrive", "cartridge", 1.55f, 0.23f, {24, 24, 27, 255}, {0.09f, 0.0f, 0.82f, 0.72f}, "none", Outline::ArchTop,
     0.05f, 0.02f, 0, 0.065f, 0, 0, 0, false, false, false, false, 0, 0, false, false, 0, false, false, true},
    {"32x", "cartridge", 1.54f, 0.24f, {30, 30, 34, 255}, {0.10f, 0.10f, 0.80f, 0.62f}, "none",
     Outline::StepBottom, 0.04f, 0.02f, 0.04f, 0.28f, 0, 0, 0, false, false, false, false, 0, 0, false, true},
    {"gamegear", "cartridge", 1.10f, 0.195f, {32, 32, 36, 255}, {0.09f, 0.22f, 0.82f, 0.62f}, "none", Outline::Rect,
     0.09f, 0.03f, 0, 0, 0, 0, 3, false, false, false, false, 0, 0, false, true},
    // 109 x 69 x 16 mm Sega Master System landscape cartridge.
    {"mastersystem", "cartridge", 1.56f, 0.22f, {22, 22, 25, 255}, {0.10f, 0.0f, 0.80f, 0.64f}, "none",
     Outline::MasterSystem, 0.035f, 0.02f, 0.028f, 0, 0, 0, 0, true},
    {"atari", "cartridge", 0.835f, 0.196f, {32, 32, 34, 255}, {0.09f, 0.08f, 0.82f, 0.72f}, "none",
     Outline::StepBottom, 0.025f, 0.02f, 0.055f, 0.18f, 0, 0, 3},
    {"atari5200", "cartridge", 1.05f, 0.20f, {28, 28, 30, 255}, {0.10f, 0.07f, 0.80f, 0.60f}, "none", Outline::Chamfer,
     0.02f, 0.02f, 0.10f, 0.10f, 0, 0, 4},
    {"jaguar", "cartridge", 1.30f, 0.24f, {36, 37, 42, 255}, {0.08f, 0.24f, 0.84f, 0.58f}, "none", Outline::Rect,
     0.04f, 0.02f, 0, 0, 0, 0, 0, false, false, false, false, 0, 0, true},
    {"lynx", "card", 1.35f, 0.05f, {46, 47, 52, 255}, {0.05f, 0.18f, 0.90f, 0.62f}, "none", Outline::Rect, 0.04f,
     0.03f, 0, 0, 0, 0, 0, false, false, false, false, 0.11f},
    {"vectrex", "cartridge", 0.90f, 0.18f, {28, 28, 30, 255}, {0.10f, 0.46f, 0.80f, 0.42f}, "none", Outline::Rect,
     0.03f, 0.02f, 0, 0, 0, 0, 2, false, false, false, true},
    {"colecovision", "cartridge", 0.83f, 0.18f, {26, 26, 28, 255}, {0.08f, 0.07f, 0.84f, 0.68f}, "none",
     Outline::StepBottom, 0.03f, 0.02f, 0.05f, 0.14f, 0, 0, 2},
    {"intellivision", "cartridge", 0.72f, 0.16f, {24, 24, 26, 255}, {0.10f, 0.82f, 0.80f, 0.12f}, "none",
     Outline::Rect, 0.03f, 0.02f, 0, 0, 0, 0, 6},
    {"neogeopocket", "cartridge", 1.15f, 0.12f, {56, 57, 62, 255}, {0.09f, 0.16f, 0.82f, 0.52f}, "none",
     Outline::Chamfer, 0.02f, 0.03f, 0.07f, 0.07f, 0, 0, 2, false, false, false, true},
    {"wonderswan", "cartridge", 1.62f, 0.18f, {198, 199, 204, 255}, {0.05f, 0.06f, 0.90f, 0.60f}, "none",
     Outline::Rect, 0.03f, 0.02f, 0, 0, 0, 0, 0, false, false, false, false, 0.15f},
    {"pokemini", "cartridge", 1.10f, 0.13f, {232, 233, 238, 150}, {0.06f, 0.42f, 0.88f, 0.50f}, "none", Outline::Rect,
     0.05f, 0.30f, 0, 0, 0, 0, 3},
    {"virtualboy", "cartridge", 0.98f, 0.17f, {140, 146, 156, 255}, {0.10f, 0.06f, 0.80f, 0.60f}, "none", Outline::Rect,
     0.05f, 0.02f, 0, 0, 0, 0, 0, false, false, false, true, 0, 0.17f},
    // 54 x 85.6 x 2.2 mm PC Engine HuCard: upper artwork, flush dark IC pad below.
    {"hucard", "card", 0.631f, 0.026f, {240, 238, 234, 255}, {0.06f, 0.05f, 0.88f, 0.60f}, "none", Outline::Rect,
     0.04f, 0.04f, 0, 0, 0, 0, 0, false, false, false, false, 0, 0.26f},
    {"mycard", "card", 0.66f, 0.025f, {236, 233, 226, 255}, {0.06f, 0.04f, 0.88f, 0.60f}, "none", Outline::Rect,
     0.04f, 0.04f, 0, 0, 0, 0, 0, false, false, false, false, 0.32f},
};

const Mould* Find(std::string_view style) {
    for (const Mould& m : MOULDS) {
        if (m.name == style) {
            return &m;
        }
    }
    return nullptr;
}

// A polygon (counter-clockwise) whose corners are rounded by their radius: x, y, r.
struct Corner {
    float x, y, r;
};
std::vector<Vec2> Rounded(const std::vector<Corner>& corners) {
    std::vector<Vec2> out;
    const std::size_t n = corners.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Corner& p = corners[(i + n - 1) % n];
        const Corner& c = corners[i];
        const Corner& q = corners[(i + 1) % n];
        if (c.r <= 0) {
            out.push_back({c.x, c.y});
            continue;
        }
        const float la = std::hypot(p.x - c.x, p.y - c.y), lb = std::hypot(q.x - c.x, q.y - c.y);
        const float d = std::min({c.r, la * 0.45f, lb * 0.45f});
        const Vec2 a{c.x + (p.x - c.x) / la * d, c.y + (p.y - c.y) / la * d};
        const Vec2 b{c.x + (q.x - c.x) / lb * d, c.y + (q.y - c.y) / lb * d};
        constexpr int STEPS = 5;
        for (int k = 0; k <= STEPS; ++k) {
            const float t = static_cast<float>(k) / STEPS, u = 1 - t;
            out.push_back({u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y});
        }
    }
    return out;
}

std::vector<Vec2> OutlineOf(const Mould& m, float a) {
    const float x = a * 0.5f, rt = m.r_top, rb = m.r_bottom;
    switch (m.outline) {
    case Outline::StepBottom: {
        const float sx = m.cut_w, sy = -0.5f + m.cut_h;
        return Rounded({{-x + sx, -0.5f, rb},
                        {x - sx, -0.5f, rb},
                        {x - sx, sy, 0},
                        {x, sy + sx, 0},
                        {x, 0.5f, rt},
                        {-x, 0.5f, rt},
                        {-x, sy + sx, 0},
                        {-x + sx, sy, 0}});
    }
    case Outline::ArchTop: {
        std::vector<Corner> c{{-x, -0.5f, rb}, {x, -0.5f, rb}};
        constexpr int ARCH_STEPS = 16;
        for (int k = 0; k <= ARCH_STEPS; ++k) {
            const float t = PI * static_cast<float>(k) / ARCH_STEPS;
            c.push_back({x * std::cos(t), (0.5f - m.cut_h) + m.cut_h * std::sin(t), 0});
        }
        if (m.md_notch) {
            c.push_back({-x, 0.06f, 0});
            c.push_back({-x + 0.025f, 0.04f, 0});
            c.push_back({-x + 0.025f, -0.12f, 0});
            c.push_back({-x, -0.14f, 0});
        }
        return Rounded(c);
    }
    case Outline::Chamfer:
        return Rounded({{-x, -0.5f, rb},
                        {x, -0.5f, rb},
                        {x, 0.5f - m.cut_h, 0.018f},
                        {x - m.cut_w, 0.5f, 0.018f},
                        {-x + m.cut_w, 0.5f, 0.018f},
                        {-x, 0.5f - m.cut_h, 0.018f}});
    case Outline::Advance: {
        // Narrower connector strip along the bottom, outward-flared grip bar along the top.
        const float sx = 0.018f, sy = -0.5f + 0.11f, gy = 0.5f - m.cut_h, flare = m.cut_w;
        return Rounded({{-x + sx, -0.5f, rb},
                        {x - sx, -0.5f, rb},
                        {x - sx, sy, 0},
                        {x, sy + sx, 0},
                        {x, gy, 0},
                        {x + flare, gy + flare, 0},
                        {x + flare, 0.5f, rt},
                        {-x - flare, 0.5f, rt},
                        {-x - flare, gy + flare, 0},
                        {-x, gy, 0},
                        {-x, sy + sx, 0},
                        {-x + sx, sy, 0}});
    }
    case Outline::Card:
    case Outline::Card3ds: {
        const float sx = m.cut_w, sy = -0.5f + m.cut_h, notch = 0.1f;
        std::vector<Corner> c{{-x + sx, -0.5f, rb}, {x - sx, -0.5f, rb}, {x - sx, sy, 0}, {x, sy + sx, 0}};
        if (m.outline == Outline::Card3ds) {
            // A tab flush with the top-right corner that keeps it out of a DS.
            const float tab = 0.035f;
            c.push_back({x, 0.30f, 0});
            c.push_back({x + tab, 0.34f, 0});
            c.push_back({x + tab, 0.5f, rt});
        } else {
            c.push_back({x, 0.5f - notch, 0});
            c.push_back({x - notch, 0.5f, 0});
        }
        c.push_back({-x, 0.5f, rt});
        c.push_back({-x, sy + sx, 0});
        c.push_back({-x + sx, sy, 0});
        return Rounded(c);
    }
    case Outline::SwitchCard: {
        const float sx = m.cut_w, sy = -0.5f + m.cut_h;
        return Rounded({{-x + sx, -0.5f, rb},
                        {x - sx, -0.5f, rb},
                        {x - sx, sy, 0},
                        {x, sy + sx, 0},
                        {x, 0.5f, rt},
                        {-x, 0.5f, rt},
                        {-x, sy + sx, 0},
                        {-x + sx, sy, 0}});
    }
    case Outline::MasterSystem:
        return Rounded({{-x, -0.5f, rb},
                        {x, -0.5f, rb},
                        {x - m.cut_w, 0.5f, rt},
                        {-x + m.cut_w, 0.5f, rt}});
    case Outline::Rect:
        break;
    }
    return RoundedOutline(a, 1, rt, rb);
}

// Gold contacts on a dark strip `h` tall, across the bottom (h > 0) or the top (h < 0).
void AddContacts(std::vector<gfx::MeshVertex>& out, const GameObject& o, float h, float z) {
    const float a = o.aspect, tall = std::fabs(h);
    const float cy = h > 0 ? -0.5f + tall * 0.5f + 0.015f : 0.5f - tall * 0.5f - 0.015f;
    const std::size_t from = out.size();
    gfx::AddBox(out, a * 0.88f, tall, 0.004f, Part::Paint, 0, cy, z + 0.002f);
    PaintFrom(out, from, gfx::Color{20, 20, 22, 255});
    const int count = std::clamp(static_cast<int>(a * 22), 10, 40);
    const float pitch = a * 0.8f / count;
    for (int i = 0; i < count; ++i) {
        gfx::AddBox(out, pitch * 0.55f, tall * 0.62f, 0.003f, Part::Metal, -a * 0.4f + pitch * (i + 0.5f),
                    cy + (h > 0 ? -tall * 0.1f : tall * 0.1f), z + 0.005f);
    }
}

} // namespace

std::optional<gfx::Color> ApplyMould(GameObject& o, const std::string& style) {
    const Mould* m = Find(style);
    if (m == nullptr) {
        return std::nullopt;
    }
    o.shape = std::string(m->shape);
    o.style = style;
    o.aspect = m->aspect;
    o.depth = m->depth;
    o.label = m->label;
    o.label_edge = std::string(m->label_edge);
    o.grip = 0;
    return m->shell;
}

std::optional<std::vector<Vec2>> MouldOutline(const GameObject& o) {
    const Mould* m = Find(o.style);
    if (m == nullptr) {
        return std::nullopt;
    }
    return OutlineOf(*m, o.aspect);
}

bool BuildMould(Built& b, const GameObject& o) {
    const Mould* m = Find(o.style);
    if (m == nullptr) {
        return false;
    }
    const float a = o.aspect, d = o.depth, face = d * 0.5f;
    const float z = face + (o.photo ? 0.0012f : 0.002f);
    auto& shell = b.Shell(o);
    const Part part = ShellPart(o);
    const Part relief = o.photo ? Part::Photo : part;
    const float photo_w = o.photo ? a : 0.0f;
    gfx::SlabParts slab = All(part);
    if (o.photo) {
        slab.front = Part::Photo;
        slab.bevel = Part::Photo;
        slab.back = Part::Back;
    }
    gfx::AddSlab(shell, OutlineOf(*m, a), d, std::min(0.012f, d * 0.22f), slab);
    if (m->handle) {
        // A rounded grip standing out along the top.
        gfx::AddSlab(shell, RoundedOutline(a * 0.94f, 0.17f, 0.08f, 0.02f), d * 1.3f, 0.02f, All(part), d * 0.1f, 0,
                     0.5f - 0.085f, 3);
    }
    if (m->top_band > 0) {
        // Recessed, with the console's name moulded in it on the real one.
        gfx::AddGroove(shell, RoundedOutline(a * 0.7f, m->top_band, 0.02f, 0.02f), 0.014f, z, 0,
                       0.5f - 0.03f - m->top_band * 0.5f, relief, false, photo_w);
    }
    if (m->top_bulge) {
        // GBC raised convex oval header.
        gfx::AddGroove(shell, RoundedOutline(a * 0.66f, 0.13f, 0.06f, 0.06f), 0.014f, z, 0, 0.5f - 0.10f, relief,
                       true, photo_w);
    }
    if (m->ridge) {
        gfx::AddGroove(shell, RoundedOutline(a * 0.86f, 0.05f, 0.02f, 0.02f), 0.012f, z, 0, 0.5f - 0.055f, relief,
                       true, photo_w);
    }
    // The label in a shallow well, kept inside the front bevel.
    const gfx::Rect& l = o.label;
    const Vec2 lc = Centre(o, l);
    const float well_top = std::min(0.485f, 0.5f - l.y + 0.02f);
    const float well_bottom = std::max(-0.485f, 0.5f - (l.y + l.h) - 0.02f);
    const float well_w = std::min(a - 0.04f, l.w * a + 0.04f);
    gfx::AddGroove(shell, RoundedOutline(well_w, well_top - well_bottom, 0.02f, 0.02f), 0.011f, z, lc.x,
                   (well_top + well_bottom) * 0.5f, relief, false, photo_w);
    if (!o.photo) {
        AddSticker(b.solid, o, face);
    }

    const float label_top = 0.5f - l.y, label_bottom = 0.5f - (l.y + l.h);
    const float low = m->band > 0 ? -0.5f + m->band + 0.02f : -0.5f + 0.04f + std::max(0.0f, m->contacts);
    const float high = 0.5f - 0.04f - std::max(0.0f, -m->contacts) - (m->ridge || m->handle ? 0.12f : 0) -
                       (m->top_band > 0 ? m->top_band + 0.03f : 0) - (m->top_bulge ? 0.14f : 0);
    // The free part of the front: under the label, or over it when the label sits low.
    const bool below = label_bottom - low >= high - label_top;
    const float free_top = below ? label_bottom - 0.04f : high;
    float free_bottom = below ? low : label_top + 0.04f;
    const float side_left = -a * 0.5f + 0.05f, side_right = lc.x - l.w * a * 0.5f - 0.035f;
    if (m->nes_front) {
        const float ch_x = -a * 0.5f + 0.145f;
        gfx::AddGroove(shell, RoundedOutline(0.15f, 0.21f, 0.015f, 0.015f), 0.012f, z, ch_x, 0.335f, relief, false,
                       photo_w);
        for (int i = 0; i < 9; ++i) {
            gfx::AddGroove(shell, RoundedOutline(0.14f, 0.022f, 0.005f, 0.005f), 0.008f, z, ch_x,
                           0.16f - static_cast<float>(i) * 0.058f, relief, false, photo_w);
        }
    }
    if (m->lock_pockets) {
        for (const float side : {-1.0f, 1.0f}) {
            gfx::AddGroove(shell, RoundedOutline(0.11f, 0.78f, 0.02f, 0.02f), 0.012f, z, side * (a * 0.5f - 0.085f),
                           0.07f, relief, true, photo_w);
            gfx::AddGroove(shell, RoundedOutline(0.12f, 0.09f, 0.012f, 0.012f), 0.010f, z, side * a * 0.28f, -0.36f,
                           relief, false, photo_w);
        }
    }
    if (m->side_grips) {
        if (m->outline == Outline::ArchTop) {
            for (const float side : {-1.0f, 1.0f}) {
                gfx::AddGroove(shell, RoundedOutline(a * 0.17f, 0.68f, 0.07f, 0.03f), 0.012f, z, side * a * 0.355f,
                               -0.02f, relief, false, photo_w);
                for (int i = 0; i < 5; ++i) {
                    gfx::AddGroove(shell, RoundedOutline(0.09f, 0.012f, 0.004f, 0.004f), 0.005f, z, side * a * 0.43f,
                                   -0.40f + static_cast<float>(i) * 0.032f, relief, false, photo_w);
                }
            }
        } else {
            for (const float side : {-1.0f, 1.0f}) {
                for (int i = 0; i < 5; ++i) {
                    gfx::AddGroove(shell, RoundedOutline(0.11f, 0.012f, 0.004f, 0.004f), 0.006f, z, side * a * 0.415f,
                                   0.27f + static_cast<float>(i) * 0.032f, relief, false, photo_w);
                }
            }
        }
    }
    if (m->flutes > 0 && side_right - side_left > 0.04f) {
        const float pitch = (side_right - side_left) / m->flutes;
        for (int i = 0; i < m->flutes; ++i) {
            for (const float side : {-1.0f, 1.0f}) {
                const float x = side * (side_left + pitch * (i + 0.5f));
                gfx::AddGroove(shell, RoundedOutline(pitch * 0.42f, l.h, 0.005f, 0.005f), pitch * 0.18f, z, x, lc.y,
                               relief, false, photo_w);
            }
        }
    }
    if (m->ribs > 0 && side_right - side_left > 0.04f) {
        const float pitch = l.h / m->ribs, w = side_right - side_left;
        for (int i = 0; i < m->ribs; ++i) {
            const float y = label_top - pitch * (i + 0.5f);
            for (const float side : {-1.0f, 1.0f}) {
                gfx::AddGroove(shell, RoundedOutline(w, pitch * 0.38f, 0.006f, 0.006f), pitch * 0.15f, z,
                               side * (side_left + w * 0.5f), y, relief, true, photo_w);
            }
        }
    }
    float free_y = free_top;
    if (m->slot) {
        gfx::AddGroove(shell, RoundedOutline(a * 0.62f, 0.035f, 0.017f, 0.017f), 0.016f, z, 0, free_y - 0.04f, relief,
                       false, photo_w);
        free_y -= 0.1f;
    }
    if (m->arrow && free_y - free_bottom > 0.08f) {
        const float ay = below ? free_bottom + 0.05f : free_y - 0.05f;
        const float dir = below ? 1.0f : -1.0f; // points at the end that goes in
        const float ax = m->nes_front ? lc.x : 0.0f;
        gfx::AddGroove(shell, {{ax, ay - 0.025f * dir}, {ax + 0.05f, ay + 0.02f * dir}, {ax - 0.05f, ay + 0.02f * dir}},
                       0.014f, z, 0, 0, relief, false, photo_w);
        if (below) {
            free_bottom += 0.1f;
        } else {
            free_y -= 0.1f;
        }
    }
    if (m->grips > 0 && free_y - free_bottom > 0.06f) {
        const float span = std::min(free_y - free_bottom, 0.05f * m->grips + 0.02f);
        const float top = below ? free_y : free_bottom + span, gap = span / m->grips;
        for (int i = 0; i < m->grips; ++i) {
            const float h = std::min(0.018f, gap * 0.45f);
            gfx::AddGroove(shell, RoundedOutline(a * 0.5f, h, h * 0.45f, h * 0.45f), h * 0.5f, z, 0,
                           top - gap * (i + 0.5f), relief, false, photo_w);
        }
    }
    if (!o.photo) {
        if (m->screws) {
            for (const float side : {-1.0f, 1.0f}) {
                const std::size_t from = b.solid.size();
                gfx::AddRing(b.solid, 0.006f, 0.022f, 0.004f, Part::Dark, Part::Dark, z, 16);
                for (std::size_t i = from; i < b.solid.size(); ++i) {
                    b.solid[i].x += side * a * 0.38f;
                    b.solid[i].y += -0.5f + m->cut_h * 0.5f + 0.04f;
                }
            }
        }
        if (m->contacts != 0) {
            AddContacts(b.solid, o, m->contacts, face);
        }
        if (m->band > 0) {
            const std::size_t from = b.solid.size();
            gfx::AddBox(b.solid, a * 0.97f, m->band, 0.006f, Part::Paint, 0, -0.5f + m->band * 0.5f + 0.012f,
                        face + 0.001f);
            PaintFrom(b.solid, from, gfx::Color{28, 30, 34, 255});
        }
    }
    if (o.smoke) {
        AddBoard(b.solid, o, a > 1.2f);
    }
    if (!o.photo && (o.shape == "cartridge" || (o.shape == "card" && m->contacts == 0))) {
        AddBack(b.solid, o, o.shape == "cartridge");
    }
    return true;
}

void BuildUmdShell(std::vector<gfx::MeshVertex>& solid, std::vector<gfx::MeshVertex>& clear) {
    // 65 x 64 x 4.2 mm UMD caddy around a 1.0-unit disc: circular upper half, gently curved
    // lower corners with a bottom-right chamfer.
    constexpr float R = 0.542f, BOTTOM = -0.525f;
    std::vector<Vec2> outline;
    outline.push_back({-R + 0.09f, BOTTOM});
    outline.push_back({R - 0.12f, BOTTOM});
    outline.push_back({R, BOTTOM + 0.12f});
    for (int k = 0; k <= 24; ++k) {
        const float t = PI * static_cast<float>(k) / 24;
        outline.push_back({R * std::cos(t), R * std::sin(t)});
    }
    outline.push_back({-R, BOTTOM + 0.09f});
    gfx::AddSlab(solid, outline, 0.028f, 0.008f, All(Part::Shell), -0.024f, 0, 0, 3);
    // Outer retaining bezel ring around the disc perimeter.
    gfx::AddRing(solid, 0.492f, 0.540f, 0.034f, Part::Shell, Part::Shell, 0.0f, 48);
    // Lower retaining plate and central white spindle hub cap over the disc.
    gfx::AddSlab(solid, RoundedOutline(2 * R - 0.06f, 0.14f, 0.01f, 0.06f), 0.044f, 0.01f, All(Part::Shell), 0.0f, 0,
                 BOTTOM + 0.08f, 3);
    gfx::AddRing(solid, 0.0f, 0.092f, 0.010f, Part::Paper, Part::Paper, 0.014f, 28);
    gfx::AddRing(solid, 0.0f, 0.032f, 0.004f, Part::Steel, Part::Steel, 0.019f, 20);
    // Clear polycarbonate protective window across the front.
    gfx::AddSlab(clear, outline, 0.006f, 0.003f, All(Part::Clear), 0.022f, 0, 0, 2);
}

} // namespace den::app
