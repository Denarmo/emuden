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
    Rect,       // rounded top and bottom corners
    StepBottom, // the sides step in near the bottom (the end that goes in)
    Wings,      // the top corners cut away at a slant (Nintendo 64)
    Chamfer,    // both top corners cut off at 45 degrees
    Advance,    // the top corners cut at a slant and the bottom a little narrower (Game Boy Advance)
};

struct Mould {
    std::string_view name, shape;
    float aspect, depth;
    gfx::Color shell;
    gfx::Rect label;
    std::string_view label_edge;
    Outline outline = Outline::Rect;
    float r_top = 0.03f, r_bottom = 0.02f;
    float cut_w = 0, cut_h = 0; // the step, the wings' or the chamfer's size
    int flutes = 0;   // vertical grooves beside the label
    int ribs = 0;     // horizontal ribs beside the label
    int grips = 0;    // horizontal grooves in the free part of the front
    bool slot = false;   // a long groove under the label
    bool screws = false; // two screws low on the front
    bool arrow = false;  // an arrow in the free part, pointing the way in
    float contacts = 0;  // gold contacts on a dark strip this tall: at the bottom (> 0) or top (< 0)
    float band = 0;      // a darker band this tall across the bottom
    bool handle = false; // a handle along the top
    bool ridge = false;  // a ridge along the top
    float top_band = 0;  // a recessed band this tall across the top, between the corners
};

// Proportions are the front's width over its height; depth and label in heights.
constexpr Mould MOULDS[] = {
    {"pocket_wide", "cartridge", 1.63f, 0.23f, {52, 53, 58, 255}, {0.14f, 0.23f, 0.72f, 0.64f}, "none",
     Outline::Advance, 0.02f, 0.03f, 0.045f, 0.14f, 0, 0, 0, false, false, true, 0, 0, false, false, 0.12f},
    {"nes", "cartridge", 0.89f, 0.15f, {128, 129, 135, 255}, {0.23f, 0.035f, 0.54f, 0.5f}, "none",
     Outline::StepBottom, 0.02f, 0.02f, 0.035f, 0.1f, 8, 0, 0, false, false, true},
    {"snes", "cartridge", 1.36f, 0.19f, {163, 164, 170, 255}, {0.08f, 0.05f, 0.84f, 0.42f}, "none",
     Outline::StepBottom, 0.06f, 0.03f, 0.028f, 0.3f, 0, 0, 0, true, true},
    {"n64", "cartridge", 1.28f, 0.24f, {122, 124, 134, 255}, {0.29f, 0.11f, 0.42f, 0.76f}, "none", Outline::Wings,
     0.05f, 0.03f, 0.13f, 0.28f, 0, 6},
    {"megadrive", "cartridge", 1.18f, 0.17f, {26, 26, 28, 255}, {0.06f, 0.06f, 0.88f, 0.68f}, "none", Outline::Rect,
     0.08f, 0.02f, 0, 0, 0, 0, 2},
    {"32x", "cartridge", 1.62f, 0.17f, {30, 30, 32, 255}, {0.07f, 0.13f, 0.86f, 0.66f}, "none", Outline::Rect, 0.03f,
     0.02f, 0, 0, 0, 0, 0, false, false, false, 0, 0, false, true},
    {"gamegear", "cartridge", 0.94f, 0.14f, {32, 32, 34, 255}, {0.08f, 0.13f, 0.84f, 0.56f}, "none", Outline::Rect,
     0.09f, 0.03f, 0, 0, 0, 0, 3, false, false, false, 0, 0, false, true},
    {"mastersystem", "cartridge", 0.72f, 0.16f, {24, 24, 26, 255}, {0.08f, 0.3f, 0.84f, 0.62f}, "none",
     Outline::Rect, 0.03f, 0.02f, 0, 0, 0, 0, 4},
    {"atari", "cartridge", 0.92f, 0.18f, {32, 32, 34, 255}, {0.1f, 0.05f, 0.8f, 0.62f}, "none", Outline::StepBottom,
     0.03f, 0.02f, 0.06f, 0.22f, 0, 0, 3},
    {"atari5200", "cartridge", 1.05f, 0.2f, {28, 28, 30, 255}, {0.1f, 0.07f, 0.8f, 0.6f}, "none", Outline::Chamfer,
     0.02f, 0.02f, 0.1f, 0.1f, 0, 0, 4},
    {"jaguar", "cartridge", 1.28f, 0.2f, {36, 37, 42, 255}, {0.04f, 0.27f, 0.92f, 0.58f}, "none", Outline::Rect,
     0.03f, 0.02f, 0, 0, 0, 0, 0, false, false, false, 0, 0, true},
    {"lynx", "card", 1.35f, 0.05f, {46, 47, 52, 255}, {0.05f, 0.18f, 0.9f, 0.62f}, "none", Outline::Rect, 0.04f,
     0.03f, 0, 0, 0, 0, 0, false, false, false, 0.11f},
    {"vectrex", "cartridge", 0.9f, 0.18f, {28, 28, 30, 255}, {0.1f, 0.46f, 0.8f, 0.42f}, "none", Outline::Rect,
     0.03f, 0.02f, 0, 0, 0, 0, 2, false, false, true},
    {"colecovision", "cartridge", 0.83f, 0.18f, {26, 26, 28, 255}, {0.08f, 0.07f, 0.84f, 0.68f}, "none",
     Outline::StepBottom, 0.03f, 0.02f, 0.05f, 0.14f, 0, 0, 2},
    {"intellivision", "cartridge", 0.72f, 0.16f, {24, 24, 26, 255}, {0.1f, 0.82f, 0.8f, 0.12f}, "none",
     Outline::Rect, 0.03f, 0.02f, 0, 0, 0, 0, 6},
    {"neogeopocket", "cartridge", 1.15f, 0.12f, {56, 57, 62, 255}, {0.09f, 0.16f, 0.82f, 0.52f}, "none",
     Outline::Chamfer, 0.02f, 0.03f, 0.07f, 0.07f, 0, 0, 2, false, false, true},
    {"wonderswan", "cartridge", 1.85f, 0.09f, {198, 199, 204, 255}, {0.03f, 0.05f, 0.94f, 0.64f}, "none",
     Outline::Rect, 0.03f, 0.02f, 0, 0, 0, 0, 0, false, false, false, 0.17f},
    {"pokemini", "cartridge", 1.1f, 0.13f, {232, 233, 238, 150}, {0.06f, 0.42f, 0.88f, 0.5f}, "none", Outline::Rect,
     0.05f, 0.3f, 0, 0, 0, 0, 3},
    {"virtualboy", "cartridge", 0.98f, 0.17f, {140, 146, 156, 255}, {0.1f, 0.06f, 0.8f, 0.6f}, "none", Outline::Rect,
     0.05f, 0.02f, 0, 0, 0, 0, 0, false, false, true, 0, 0.17f},
    {"hucard", "card", 0.64f, 0.025f, {236, 233, 226, 255}, {0.06f, 0.42f, 0.88f, 0.54f}, "none", Outline::Rect,
     0.04f, 0.04f, 0, 0, 0, 0, 0, false, false, false, -0.36f},
    {"mycard", "card", 0.66f, 0.025f, {236, 233, 226, 255}, {0.06f, 0.04f, 0.88f, 0.6f}, "none", Outline::Rect,
     0.04f, 0.04f, 0, 0, 0, 0, 0, false, false, false, 0.32f},
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
                        {x, sy + sx, 0.01f},
                        {x, 0.5f, rt},
                        {-x, 0.5f, rt},
                        {-x, sy + sx, 0.01f},
                        {-x + sx, sy, 0}});
    }
    case Outline::Wings:
        return Rounded({{-x, -0.5f, rb},
                        {x, -0.5f, rb},
                        {x, 0.5f - m.cut_h, 0.04f},
                        {x - m.cut_w, 0.5f, rt},
                        {-x + m.cut_w, 0.5f, rt},
                        {-x, 0.5f - m.cut_h, 0.04f}});
    case Outline::Chamfer:
        return Rounded({{-x, -0.5f, rb},
                        {x, -0.5f, rb},
                        {x, 0.5f - m.cut_h, 0.015f},
                        {x - m.cut_w, 0.5f, 0.015f},
                        {-x + m.cut_w, 0.5f, 0.015f},
                        {-x, 0.5f - m.cut_h, 0.015f}});
    case Outline::Advance: {
        // Straight sides up to a slant at each top corner; the strip along the bottom a hair
        // narrower than the body.
        const float sx = 0.018f, sy = -0.5f + 0.13f;
        return Rounded({{-x + sx, -0.5f, rb},
                        {x - sx, -0.5f, rb},
                        {x - sx, sy, 0},
                        {x, sy + sx, 0.006f},
                        {x, 0.5f - m.cut_h, 0.012f},
                        {x - m.cut_w, 0.5f, rt},
                        {-x + m.cut_w, 0.5f, rt},
                        {-x, 0.5f - m.cut_h, 0.012f},
                        {-x, sy + sx, 0.006f},
                        {-x + sx, sy, 0}});
    }
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

const std::vector<std::string>& MouldNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const Mould& m : MOULDS) {
            out.emplace_back(m.name);
        }
        return out;
    }();
    return names;
}

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
    const float a = o.aspect, d = o.depth, face = d * 0.5f, z = face + 0.002f;
    auto& shell = b.Shell(o);
    const Part part = ShellPart(o);
    gfx::AddSlab(shell, OutlineOf(*m, a), d, std::min(0.018f, d * 0.3f), All(part));
    if (m->handle) {
        // A rounded grip standing out along the top.
        gfx::AddSlab(shell, RoundedOutline(a * 0.9f, 0.17f, 0.08f, 0.02f), d * 1.3f, 0.02f, All(part), d * 0.1f, 0,
                     0.5f - 0.085f, 3);
    }
    if (m->top_band > 0) {
        // Recessed, with the console's name moulded in it on the real one.
        gfx::AddGroove(shell, RoundedOutline(a * 0.62f, m->top_band, 0.02f, 0.02f), 0.014f, z, 0,
                       0.5f - 0.03f - m->top_band * 0.5f, part);
    }
    if (m->ridge) {
        gfx::AddGroove(shell, RoundedOutline(a * 0.86f, 0.05f, 0.02f, 0.02f), 0.012f, z, 0, 0.5f - 0.055f, part, true);
    }
    // The label in a shallow well.
    const gfx::Rect& l = o.label;
    const Vec2 lc = Centre(o, l);
    gfx::AddGroove(shell, RoundedOutline(l.w * a + 0.04f, l.h + 0.04f, 0.02f, 0.02f), 0.011f, z, lc.x, lc.y, part);
    AddSticker(b.solid, o, face);

    const float label_top = 0.5f - l.y, label_bottom = 0.5f - (l.y + l.h);
    const float low = m->band > 0 ? -0.5f + m->band + 0.02f : -0.5f + 0.04f + std::max(0.0f, m->contacts);
    const float high = 0.5f - 0.04f - std::max(0.0f, -m->contacts) - (m->ridge || m->handle ? 0.12f : 0) -
                       (m->top_band > 0 ? m->top_band + 0.03f : 0);
    // The free part of the front: under the label, or over it when the label sits low.
    const bool below = label_bottom - low >= high - label_top;
    const float free_top = below ? label_bottom - 0.04f : high;
    float free_bottom = below ? low : label_top + 0.04f;
    const float side_left = -a * 0.5f + 0.05f, side_right = lc.x - l.w * a * 0.5f - 0.035f;
    if (m->flutes > 0 && side_right - side_left > 0.04f) {
        const float pitch = (side_right - side_left) / m->flutes;
        for (int i = 0; i < m->flutes; ++i) {
            for (const float side : {-1.0f, 1.0f}) {
                const float x = side * (side_left + pitch * (i + 0.5f));
                gfx::AddGroove(shell, RoundedOutline(pitch * 0.42f, l.h, 0.005f, 0.005f), pitch * 0.18f, z, x, lc.y,
                               part);
            }
        }
    }
    if (m->ribs > 0 && side_right - side_left > 0.04f) {
        const float pitch = l.h / m->ribs, w = side_right - side_left;
        for (int i = 0; i < m->ribs; ++i) {
            const float y = label_top - pitch * (i + 0.5f);
            for (const float side : {-1.0f, 1.0f}) {
                gfx::AddGroove(shell, RoundedOutline(w, pitch * 0.38f, 0.006f, 0.006f), pitch * 0.15f, z,
                               side * (side_left + w * 0.5f), y, part, true);
            }
        }
    }
    float free_y = free_top;
    if (m->slot) {
        gfx::AddGroove(shell, RoundedOutline(a * 0.62f, 0.035f, 0.017f, 0.017f), 0.016f, z, 0, free_y - 0.04f, part);
        free_y -= 0.1f;
    }
    if (m->arrow && free_y - free_bottom > 0.08f) {
        const float ay = below ? free_bottom + 0.05f : free_y - 0.05f;
        const float dir = below ? 1.0f : -1.0f; // points at the end that goes in
        gfx::AddGroove(shell, {{0, ay - 0.025f * dir}, {0.05f, ay + 0.02f * dir}, {-0.05f, ay + 0.02f * dir}}, 0.014f,
                       z, 0, 0, part);
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
                           top - gap * (i + 0.5f), part);
        }
    }
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
        gfx::AddBox(b.solid, a * 0.97f, m->band, 0.006f, Part::Paint, 0, -0.5f + m->band * 0.5f + 0.012f, face + 0.001f);
        PaintFrom(b.solid, from, gfx::Color{28, 30, 34, 255});
    }
    if (o.smoke) {
        AddBoard(b.solid, o, a > 1.2f);
    }
    if (o.shape == "cartridge") {
        AddBack(b.solid, o, true);
    }
    return true;
}

void BuildUmdShell(std::vector<gfx::MeshVertex>& solid, std::vector<gfx::MeshVertex>& clear) {
    // Round over the disc, square below it with a corner cut off, as the real one.
    constexpr float R = 0.54f, BOTTOM = -0.58f;
    std::vector<Vec2> outline;
    outline.push_back({-R + 0.07f, BOTTOM});
    outline.push_back({R - 0.13f, BOTTOM});
    outline.push_back({R, BOTTOM + 0.13f});
    for (int k = 0; k <= 24; ++k) {
        const float t = PI * static_cast<float>(k) / 24;
        outline.push_back({R * std::cos(t), R * std::sin(t)});
    }
    outline.push_back({-R, BOTTOM + 0.07f});
    gfx::AddSlab(solid, outline, 0.035f, 0.01f, All(Part::Shell), -0.03f, 0, 0, 3);
    // The part under the disc, which holds it, and the clear window over it.
    gfx::AddSlab(solid, RoundedOutline(2 * R - 0.02f, 0.13f, 0.0f, 0.06f), 0.05f, 0.01f, All(Part::Shell), 0.0f, 0,
                 BOTTOM + 0.075f, 3);
    gfx::AddSlab(clear, outline, 0.008f, 0.003f, All(Part::Clear), 0.024f, 0, 0, 2);
}

} // namespace den::app
