// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Handhelds modelled on the real ones: their sizes in millimetres, where their screens,
// pads, buttons and speakers sit, and the colours of the parts that keep theirs whatever the
// shell's colour. Built in millimetres, then scaled to one unit high.

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>

#include "console_mesh.h"

namespace den::app {

namespace {

using gfx::Color;
using gfx::Part;
using gfx::SlabParts;
using gfx::Vec2;
using Vertices = std::vector<gfx::MeshVertex>;
using Outline = std::vector<Vec2>;

constexpr float PI = 3.14159265f;

SlabParts All(Part part) {
    return SlabParts{part, part, part, part, part};
}

// A `w` x `h` rectangle centred on the origin with each corner rounded on its own:
// top left, top right, bottom right, bottom left. Counter-clockwise.
Outline Corners(float w, float h, float tl, float tr, float br, float bl, int segments = 8) {
    Outline out;
    const auto corner = [&](float cx, float cy, float r, float from) {
        if (r <= 0) {
            out.push_back({cx, cy});
            return;
        }
        for (int i = 0; i <= segments; ++i) {
            const float a = from + PI * 0.5f * static_cast<float>(i) / static_cast<float>(segments);
            out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
        }
    };
    const float x = w * 0.5f, y = h * 0.5f;
    corner(x - br, -y + br, br, -PI * 0.5f);
    corner(x - tr, y - tr, tr, 0);
    corner(-x + tl, y - tl, tl, PI * 0.5f);
    corner(-x + bl, -y + bl, bl, PI);
    return out;
}

Outline Rounded(float w, float h, float r) {
    return Corners(w, h, r, r, r, r);
}

Outline Circle(float r) {
    return Corners(r * 2, r * 2, r, r, r, r, 6);
}

Outline Rotated(Outline outline, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    for (Vec2& p : outline) {
        p = {p.x * c - p.y * s, p.x * s + p.y * c};
    }
    return outline;
}

Outline Moved(Outline outline, float dx, float dy) {
    for (Vec2& p : outline) {
        p.x += dx;
        p.y += dy;
    }
    return outline;
}

// The vertices added since `from` in that plastic's own colour.
void Paint(Vertices& out, std::size_t from, Color c) {
    for (std::size_t i = from; i < out.size(); ++i) {
        out[i].part = static_cast<float>(Part::Paint);
        out[i].u = static_cast<float>(c.r * 256 + c.g);
        out[i].v = static_cast<float>(c.b);
    }
}

void Apply(Vertices& out, std::size_t from, const gfx::Mat4& m) {
    const float* a = m.m;
    for (std::size_t i = from; i < out.size(); ++i) {
        gfx::MeshVertex& v = out[i];
        const float x = v.x, y = v.y, z = v.z, nx = v.nx, ny = v.ny, nz = v.nz;
        v.x = a[0] * x + a[4] * y + a[8] * z + a[12];
        v.y = a[1] * x + a[5] * y + a[9] * z + a[13];
        v.z = a[2] * x + a[6] * y + a[10] * z + a[14];
        v.nx = a[0] * nx + a[4] * ny + a[8] * nz;
        v.ny = a[1] * nx + a[5] * ny + a[9] * nz;
        v.nz = a[2] * nx + a[6] * ny + a[10] * nz;
    }
}

// A flat piece `thick` deep standing on the face at `z`, in a colour of its own or a part.
void Plate(Vertices& out, const Outline& outline, float thick, float z, float x, float y, Color c) {
    const std::size_t from = out.size();
    gfx::AddSlab(out, outline, thick, std::min(thick * 0.4f, 0.6f), All(Part::Shell), z + thick * 0.5f, x, y, 2);
    Paint(out, from, c);
}

void PlatePart(Vertices& out, const Outline& outline, float thick, float z, float x, float y, Part part) {
    gfx::AddSlab(out, outline, thick, std::min(thick * 0.4f, 0.6f), All(part), z + thick * 0.5f, x, y, 2);
}

// A round button `r` across standing `tall` out of the face, in a groove a little wider.
void Round(Vertices& out, float x, float y, float r, float tall, float z, std::function<void(std::size_t)> colour,
           bool groove = true) {
    const std::size_t from = out.size();
    gfx::AddSlab(out, Circle(r), tall, std::min(r * 0.35f, tall * 0.45f), All(Part::Shell), z + tall * 0.5f, x, y, 3);
    colour(from);
    if (groove) {
        gfx::AddGroove(out, Circle(r + 0.9f), 0.8f, z + 0.01f, x, y);
    }
}

// A cross-shaped pad `size` across with arms `arm` wide.
void Cross(Vertices& out, float x, float y, float size, float arm, float tall, float z,
           std::function<void(std::size_t)> colour) {
    const std::size_t from = out.size();
    const float b = std::min(0.8f, tall * 0.4f);
    gfx::AddSlab(out, Rounded(size, arm, 0.8f), tall, b, All(Part::Shell), z + tall * 0.5f, x, y, 2);
    gfx::AddSlab(out, Rounded(arm, size, 0.8f), tall, b, All(Part::Shell), z + tall * 0.5f, x, y, 2);
    colour(from);
}

std::function<void(std::size_t)> Painted(Vertices& out, Color c) {
    return [&out, c](std::size_t from) { Paint(out, from, c); };
}

std::function<void(std::size_t)> AsPart(Vertices& out, Part part) {
    return [&out, part](std::size_t from) {
        for (std::size_t i = from; i < out.size(); ++i) {
            out[i].part = static_cast<float>(part);
        }
    };
}

// A screen: dark glass that lights up on boot, `w` x `h`, standing on the face at `z`.
void Glass(Vertices& out, float x, float y, float w, float h, float z) {
    SlabParts glass = All(Part::Dark);
    glass.front = Part::Screen;
    gfx::AddSlab(out, Rounded(w, h, 0.4f), 0.5f, 0, glass, z + 0.25f, x, y, 0);
}

// Little holes in a grid, the cells within `radius` of the middle (none: all of them).
void Holes(Vertices& out, float x, float y, int cols, int rows, float pitch, float z, float radius = 0) {
    for (int i = 0; i < cols; ++i) {
        for (int j = 0; j < rows; ++j) {
            const float dx = (static_cast<float>(i) - (cols - 1) * 0.5f) * pitch;
            const float dy = (static_cast<float>(j) - (rows - 1) * 0.5f) * pitch;
            if (radius > 0 && dx * dx + dy * dy > radius * radius) {
                continue;
            }
            const std::size_t from = out.size();
            gfx::AddRing(out, 0, pitch * 0.28f, 0.2f, Part::Dark, Part::Dark, z + 0.05f, 10);
            Apply(out, from, gfx::Mat4::Translate(x + dx, y + dy, 0));
        }
    }
}

void Screw(Vertices& out, float x, float y, float z) {
    Round(out, x, y, 1.6f, 0.3f, z - 0.1f, AsPart(out, Part::Dark), false);
}

// Details built on the front face at z = depth / 2, turned round onto the back: give them
// in the coordinates of the console seen from behind.
void ToBack(Vertices& out, std::size_t from) {
    Apply(out, from, gfx::Mat4::RotateY(PI));
}

// Scales the mesh to one unit high, centred on the origin in y; returns the scale and the
// middle it took away so the model's other figures can follow.
struct Fit {
    float scale = 1, middle = 0;
    float Y(float mm) const {
        return (mm - middle) * scale;
    }
    float L(float mm) const {
        return mm * scale;
    }
};

Fit FitHeight(Vertices& out) {
    float lo = 1e9f, hi = -1e9f;
    for (const gfx::MeshVertex& v : out) {
        lo = std::min(lo, v.y);
        hi = std::max(hi, v.y);
    }
    Fit fit{1.0f / std::max(1.0f, hi - lo), (hi + lo) * 0.5f};
    for (gfx::MeshVertex& v : out) {
        v.x *= fit.scale;
        v.y = fit.Y(v.y);
        v.z *= fit.scale;
    }
    return fit;
}

// The figures of a handheld whose cartridge stands in a pocket at the top of its back, all
// in millimetres: the front's size and depth, the cartridge's height and how much of it goes
// in, the pocket's width and depth and how far down the back it is open, the screen.
struct Slot {
    float w, h, depth;
    float cart_h, sink, pocket_w, pocket_depth, open;
    float screen_x, screen_y, screen_w, screen_h;
    float slot_x = 0, slot_y = 0; // slot_y: where the cartridge goes in; 0 for the top
};

ConsoleModel Finish(Vertices& out, const Slot& s, std::unique_ptr<gfx::Mesh3D>& mesh) {
    const Fit fit = FitHeight(out);
    if (!mesh) {
        mesh = std::make_unique<gfx::Mesh3D>();
        mesh->Set(out);
    }
    ConsoleModel m;
    m.mesh = mesh.get();
    m.aspect = fit.L(s.w);
    m.depth = fit.L(s.depth);
    m.slot_y = fit.Y(s.slot_y != 0 ? s.slot_y : s.h * 0.5f);
    // In its pocket, its back flush with the console's; in the middle when no pocket shows it.
    m.slot_z = s.pocket_w > 0 ? fit.L(-s.depth * 0.5f + s.pocket_depth * 0.5f) : fit.L(-0.5f);
    m.slot_x = fit.L(s.slot_x);
    m.cart_height = fit.L(s.cart_h);
    m.sink = s.sink;
    m.bottom = -fit.Y(-s.h * 0.5f);
    m.screen_x = fit.L(s.screen_x);
    m.screen_y = fit.Y(s.screen_y);
    m.screen_w = fit.L(s.screen_w);
    m.screen_h = fit.L(s.screen_h);
    if (s.pocket_w > 0) {
        m.pocket_w = fit.L(s.pocket_w);
        m.pocket_depth = fit.L(s.pocket_depth);
        m.lip_y = fit.Y(s.h * 0.5f - s.open);
    }
    return m;
}

// The body with its pocket, in millimetres.
void Body(Vertices& out, const Outline& outline, const Slot& s, float bevel) {
    ConsoleModel pocket;
    pocket.pocket_w = s.pocket_w;
    pocket.pocket_depth = s.pocket_depth;
    pocket.lip_y = s.h * 0.5f - s.open;
    PocketBody(out, outline, s.depth, bevel, All(Part::Shell), pocket);
}

// Game Boy (DMG-01, 1989): 90 x 148 x 32 mm, the bottom right corner rounded.
ConsoleModel Dmg(std::unique_ptr<gfx::Mesh3D>& mesh) {
    const Slot s{90, 148, 32, 65, 1.02f, 59, 8.6f, 36, 0, 31.5f, 47, 43};
    Vertices out;
    const float z = s.depth * 0.5f;
    Body(out, Corners(90, 148, 4, 4, 24, 4), s, 2.2f);
    // The line under the top with the power switch's corner marks.
    gfx::AddGroove(out, Rounded(84, 1.2f, 0.5f), 0.6f, z, 0, 66.5f);
    gfx::AddGroove(out, Rounded(1.2f, 6.5f, 0.5f), 0.6f, z, -37, 70.5f);
    gfx::AddGroove(out, Rounded(1.2f, 6.5f, 0.5f), 0.6f, z, 37, 70.5f);
    // The screen's grey surround with its two lines, the battery light, the screen.
    Plate(out, Corners(76, 66, 3, 3, 12, 3), 0.6f, z, 0, 28.6f, {118, 117, 140, 255});
    const float zs = z + 0.6f;
    for (const float side : {-1.0f, 1.0f}) {
        Plate(out, Rounded(19, 0.7f, 0.2f), 0.15f, zs, side * 23.5f, 57.9f, {140, 40, 90, 255});
        Plate(out, Rounded(19, 0.7f, 0.2f), 0.15f, zs, side * 23.5f, 56.7f, {44, 44, 112, 255});
    }
    Round(out, -32.7f, 38.8f, 1.3f, 0.4f, zs, AsPart(out, Part::Led), false);
    Glass(out, 0, 31.5f, 47, 43, zs);
    // The pad in its round dip, B and A, Select and Start leaning, the speaker's slots.
    gfx::AddGroove(out, Circle(14), 1.4f, z, -26.5f, -27.5f);
    Cross(out, -26.5f, -27.5f, 22, 7.5f, 3, z, Painted(out, {38, 38, 42, 255}));
    Round(out, 18.5f, -30.1f, 5.1f, 3, z, Painted(out, {150, 36, 88, 255}));
    Round(out, 32.6f, -24.5f, 5.1f, 3, z, Painted(out, {150, 36, 88, 255}));
    for (const auto& [x, y] : {std::pair{-11.6f, -51.0f}, std::pair{3.8f, -49.5f}}) {
        Plate(out, Rotated(Rounded(11, 3.8f, 1.9f), 0.44f), 1.6f, z, x, y, {140, 139, 150, 255});
        gfx::AddGroove(out, Rotated(Rounded(13, 5.6f, 2.8f), 0.44f), 0.7f, z, x, y);
    }
    for (int i = 0; i < 6; ++i) {
        gfx::AddGroove(out, Rotated(Rounded(2.4f, 15, 1.2f), -0.44f), 1.0f, z, 14.5f + i * 4.6f, -58 - i * 0.6f);
    }
    // The back, seen from behind: the battery lid with its ribs, the label, four screws.
    const std::size_t back = out.size();
    gfx::AddGroove(out, Corners(66, 48, 3, 3, 3, 16), 0.8f, z, 0, -44);
    for (int i = 0; i < 8; ++i) {
        gfx::AddGroove(out, Rounded(54, 1.4f, 0.6f), 0.5f, z, 0, -29 - i * 4.4f, Part::Shell, true);
    }
    Plate(out, Rounded(42, 19, 1), 0.2f, z, 0, 15, {170, 169, 176, 255});
    for (const float x : {-37.8f, 37.8f}) {
        Screw(out, x, 57.8f, z);
        Screw(out, x, 5.1f, z);
    }
    ToBack(out, back);
    return Finish(out, s, mesh);
}

// Game Boy Color (CGB-001, 1998): 78 x 134 mm, 22 mm deep with the battery's hump behind.
ConsoleModel Cgb(std::unique_ptr<gfx::Mesh3D>& mesh) {
    const Slot s{78, 134, 22, 65, 0.95f, 59, 8.6f, 25, 0, 32.5f, 46, 41};
    Vertices out;
    const float z = s.depth * 0.5f;
    Body(out, Corners(78, 134, 5, 5, 12, 12), s, 2);
    gfx::AddSlab(out, Corners(70, 44, 10, 10, 12, 12), 10, 3, All(Part::Shell), -z, 0, -40);
    // The black surround, the power light, the screen, the Nintendo oval.
    Plate(out, Corners(69, 62.3f, 3, 3, 9, 9), 0.6f, z, 0, 29.9f, {30, 29, 36, 255});
    Round(out, -32, 42, 1.1f, 0.4f, z + 0.6f, AsPart(out, Part::Led), false);
    Glass(out, 0, 32.5f, 46, 41, z + 0.6f);
    gfx::AddGroove(out, Rounded(22, 5, 2.5f), 0.7f, z, 0, -9.4f);
    // The pad, B and A, Select and Start in the shell's colour, darker; the speaker's holes.
    gfx::AddGroove(out, Circle(12), 1.2f, z, -23.2f, -23.4f);
    Cross(out, -23.2f, -23.4f, 19, 6.5f, 2.5f, z, AsPart(out, Part::Dark));
    Round(out, 13.7f, -26, 4.7f, 2.5f, z, AsPart(out, Part::Dark));
    Round(out, 28, -21.3f, 4.7f, 2.5f, z, AsPart(out, Part::Dark));
    for (const float x : {-6.3f, 6.0f}) {
        PlatePart(out, Rounded(9, 3.6f, 1.8f), 1.4f, z, x, -45.5f, Part::Dark);
    }
    Holes(out, 26.2f, -48.6f, 7, 7, 2.5f, z, 8);
    // The back: the label, the lid over the hump, two screws.
    const std::size_t back = out.size();
    Plate(out, Rounded(60, 22, 1), 0.2f, z, 0, 8, {72, 72, 78, 255});
    for (const float x : {-31.0f, 31.0f}) {
        Screw(out, x, 56, z);
    }
    ToBack(out, back);
    const std::size_t lid = out.size();
    gfx::AddGroove(out, Corners(58, 34, 4, 4, 8, 8), 0.8f, z + 5, 0, -42);
    ToBack(out, lid);
    return Finish(out, s, mesh);
}

// Game Boy Advance (AGB-001, 2001): 144 x 82 x 24 mm, its shoulders in grey.
ConsoleModel Agb(std::unique_ptr<gfx::Mesh3D>& mesh) {
    const Slot s{144, 82, 24, 35, 1.0f, 59, 7, 19, 0, 4.4f, 62, 41};
    Vertices out;
    const float z = s.depth * 0.5f;
    const Color grey{200, 200, 207, 255};
    Body(out, Corners(144, 82, 22, 22, 28, 28), s, 2.5f);
    // The grey shoulders and L and R wrap the top corners, a little proud of the shell.
    const Outline wider = Corners(146, 83.4f, 23, 23, 29, 29);
    for (const float side : {-1.0f, 1.0f}) {
        const Outline corner = ClipOutline(ClipOutline(wider, -side, 0, -50), 0, -1, 8);
        if (!corner.empty()) {
            const std::size_t from = out.size();
            gfx::AddSlab(out, corner, 12, 2.5f, All(Part::Shell), -z + 6.2f);
            Paint(out, from, grey);
        }
    }
    // The dark surround, the screen, the power light, the Nintendo oval, the speaker's slots.
    Plate(out, Corners(76, 64, 8, 8, 14, 14), 0.6f, z, 0, 0.2f, {43, 42, 49, 255});
    Glass(out, 0, 4.4f, 62, 41, z + 0.6f);
    Round(out, 48.1f, 26.5f, 1, 0.4f, z, AsPart(out, Part::Led), false);
    gfx::AddGroove(out, Rounded(18, 4.5f, 2.2f), 0.6f, z, 0, 36.2f);
    for (int i = 0; i < 6; ++i) {
        gfx::AddGroove(out, Rotated(Rounded(15, 1.6f, 0.8f), 0.26f), 0.7f, z, 51.2f, -7.5f - i * 3.4f);
    }
    // The pad, A and B, Start and Select, all grey.
    gfx::AddGroove(out, Circle(11.5f), 1.2f, z, -54.1f, 6.6f);
    Cross(out, -54.1f, 6.6f, 18, 6, 2.5f, z, Painted(out, grey));
    Round(out, 48.2f, 4.5f, 4.6f, 2.5f, z, Painted(out, grey));
    Round(out, 61.2f, 9.6f, 4.6f, 2.5f, z, Painted(out, grey));
    for (const float y : {-11.7f, -20.1f}) {
        Round(out, -43.2f, y, 2.2f, 1.6f, z, Painted(out, grey));
        gfx::AddGroove(out, Rounded(10, 4.4f, 2.2f), 0.6f, z, -51.5f, y);
    }
    // The back: the label, the battery lid, four screws.
    const std::size_t back = out.size();
    Plate(out, Rounded(70, 13, 1), 0.2f, z, 0, 8, {38, 38, 42, 255});
    gfx::AddGroove(out, Rounded(66, 32, 3), 0.8f, z, 0, -22);
    for (const float x : {-48.0f, 48.0f}) {
        Screw(out, x, 17, z);
        Screw(out, x * 1.15f, -28, z);
    }
    ToBack(out, back);
    return Finish(out, s, mesh);
}

// The front of a DS Lite's half: the silver rim, the black glass, the screen in it.
void DsScreen(Vertices& out, float y, float z) {
    Plate(out, Rounded(66, 55, 2), 0.5f, z, 0, y, {200, 201, 206, 255});
    Plate(out, Rounded(63, 52, 1.5f), 0.4f, z + 0.5f, 0, y, {22, 22, 25, 255});
    Glass(out, 0, y, 61, 46, z + 0.9f);
}

// Nintendo DS Lite (USG-001, 2006): 133 x 74 mm halves, the base 12 mm deep, the lid 9.5,
// opened flat and standing; the card goes in behind the hinge, down into the base, clear of
// the lid's back.
ConsoleModel DsLite(std::unique_ptr<gfx::Mesh3D>& mesh) {
    constexpr float BASE_H = 70, BASE_D = 12, LID_H = 72, LID_D = 9.5f, HINGE_Z = 3;
    Slot s{133, BASE_H, BASE_D, 35, 1.03f, 35, 4.6f, 6, 0, 0, 61, 46};
    Vertices out;
    // The base, from the hinge's axis (y = 0) down, with its card pocket.
    {
        ConsoleModel pocket;
        pocket.pocket_w = s.pocket_w;
        pocket.pocket_depth = s.pocket_depth;
        pocket.lip_y = -s.open;
        PocketBody(out, Moved(Corners(133, BASE_H, 2, 2, 7, 7), 0, -BASE_H * 0.5f), BASE_D, 2.4f,
                   All(Part::Shell), pocket);
    }
    const float zb = BASE_D * 0.5f;
    DsScreen(out, -37, zb);
    gfx::AddGroove(out, Circle(12.5f), 1.1f, zb, -51.2f, -36.2f);
    Cross(out, -51.2f, -36.2f, 20, 6.6f, 2, zb, AsPart(out, Part::Shell));
    for (const auto& [dx, dy] : {std::pair{0.0f, 7.8f}, std::pair{0.0f, -7.8f}, std::pair{-7.8f, 0.0f},
                                 std::pair{7.8f, 0.0f}}) {
        Round(out, 50.6f + dx, -33.7f + dy, 3.9f, 2, zb, AsPart(out, Part::Shell));
    }
    Round(out, 41.2f, -57.5f, 2.1f, 1.2f, zb, AsPart(out, Part::Shell));
    Round(out, 41.2f, -64, 2.1f, 1.2f, zb, AsPart(out, Part::Shell));
    // The base's back: the battery lid and two screws.
    std::size_t back = out.size();
    gfx::AddGroove(out, Rounded(36, 56, 3), 0.8f, zb, 40, -38);
    Screw(out, -50.5f, -13.8f, zb);
    Screw(out, -54, -62, zb);
    ToBack(out, back);
    // The hinge: a barrel across, proud of the base's face, with the microphone and the
    // power light on its front.
    std::size_t from = out.size();
    gfx::AddSlab(out, Circle(5), 129, 1.2f, All(Part::Shell));
    Apply(out, from, gfx::Mat4::Translate(0, 0, HINGE_Z) * gfx::Mat4::RotateY(PI * 0.5f));
    gfx::AddBox(out, 0.8f, 2.6f, 0.4f, Part::Dark, 0, 0, HINGE_Z + 5.05f);
    gfx::AddBox(out, 1, 3.2f, 0.4f, Part::Led, 54, 0, HINGE_Z + 5.0f);
    // The lid above the hinge.
    from = out.size();
    const float lid_y = 5 + LID_H * 0.5f, zl = LID_D * 0.5f;
    gfx::AddSlab(out, Corners(133, LID_H, 6, 6, 3, 3), LID_D, 2.2f, All(Part::Shell), 0, 0, lid_y);
    DsScreen(out, lid_y, zl);
    for (const float x : {-50.0f, 50.0f}) {
        Holes(out, x, lid_y + 4, 3, 2, 3.4f, zl);
    }
    back = out.size();
    gfx::AddGroove(out, Rounded(9, 6.5f, 1.2f), 0.6f, zl, 0, lid_y + 6);
    gfx::AddGroove(out, Rounded(9, 6.5f, 1.2f), 0.6f, zl, 0, lid_y - 2);
    ToBack(out, back);
    Apply(out, from, gfx::Mat4::Translate(0, 0, HINGE_Z));
    // The top screen is where the launch goes into.
    s.screen_y = lid_y;
    s.slot_y = -0.5f; // the card's top, just under the hinge's axis
    // The halves' heights round the hinge, for the floor and the scale.
    s.h = 2 * BASE_H;      // Finish takes the bottom from h / 2
    s.open = BASE_H + 6;   // and the pocket's lip from h / 2 - open
    return Finish(out, s, mesh);
}

// A Joy-Con's stick: a coloured ring round its well, the stick standing out of it.
void Stick(Vertices& out, float x, float y, float z, Color ring) {
    const std::size_t from = out.size();
    gfx::AddRing(out, 8.4f, 10.6f, 0.8f, Part::Shell, Part::Shell, z + 0.4f, 40);
    Apply(out, from, gfx::Mat4::Translate(x, y, 0));
    Paint(out, from, ring);
    Plate(out, Circle(8.2f), 1.2f, z, x, y, {34, 35, 39, 255});
    Round(out, x, y, 7, 4, z + 1.2f, Painted(out, {46, 47, 52, 255}), false);
}

// Nintendo Switch 2 (BEE-001, 2025): 272 x 116 x 14 mm with its Joy-Cons on; the game card
// goes into the top edge, right of the middle.
ConsoleModel Switch2(std::unique_ptr<gfx::Mesh3D>& mesh) {
    const Slot s{272, 116, 14, 31, 1.02f, 0, 3.3f, 0, 0, 0.5f, 175, 98.5f, 66};
    Vertices out;
    const float z = s.depth * 0.5f;
    const Color key{40, 41, 46, 255};
    gfx::AddSlab(out, Rounded(196, 114, 3), 14, 1.6f, All(Part::Shell));
    gfx::AddSlab(out, Corners(38, 114, 22, 2, 2, 22), 15, 4, All(Part::Shell), 0, -117);
    gfx::AddSlab(out, Corners(38, 114, 2, 22, 22, 2), 15, 4, All(Part::Shell), 0, 117);
    for (const float side : {-1.0f, 1.0f}) {
        gfx::AddBox(out, 0.7f, 108, 14.6f, Part::Dark, side * 98.15f, 0, 0);
    }
    // The black glass over the front, the screen in it, the card slot's lid on the top edge.
    Plate(out, Rounded(192, 111, 2.5f), 0.5f, z, 0, 0, {12, 12, 14, 255});
    Glass(out, 0, 0.5f, 175, 98.5f, z + 0.5f);
    const std::size_t lid = out.size();
    gfx::AddBox(out, 26, 0.6f, 6, Part::Shell, 66, 57.1f, -0.5f);
    Paint(out, lid, key);
    // The left Joy-Con: minus, the stick with its blue ring, the four buttons, capture.
    const float zj = 7.5f;
    Plate(out, Rounded(5.5f, 1.6f, 0.8f), 0.8f, zj, -104.4f, 45.6f, {90, 91, 97, 255});
    Stick(out, -115.8f, 27.4f, zj, {72, 168, 200, 255});
    for (const auto& [dx, dy] : {std::pair{0.0f, 8.8f}, std::pair{0.0f, -8.8f}, std::pair{-8.8f, 0.0f},
                                 std::pair{8.8f, 0.0f}}) {
        Round(out, -115.8f + dx, -4.2f + dy, 4, 1.8f, zj, Painted(out, key));
    }
    Plate(out, Rounded(6, 6, 1.2f), 1, zj, -109.2f, -25.9f, key);
    // The right one: plus, X Y A B, the stick with its red ring, home, C.
    Plate(out, Rounded(6, 1.6f, 0.8f), 0.8f, zj, 103.9f, 45.4f, {90, 91, 97, 255});
    Plate(out, Rounded(1.6f, 6, 0.8f), 0.8f, zj, 103.9f, 45.4f, {90, 91, 97, 255});
    for (const auto& [dx, dy] : {std::pair{0.0f, 8.8f}, std::pair{0.0f, -8.8f}, std::pair{-8.8f, 0.0f},
                                 std::pair{8.8f, 0.0f}}) {
        Round(out, 115.8f + dx, 28.2f + dy, 4, 1.8f, zj, Painted(out, key));
    }
    Stick(out, 116, -3.4f, zj, {224, 98, 78, 255});
    Round(out, 109, -25.9f, 4, 1.2f, zj, Painted(out, key));
    Plate(out, Rounded(5.5f, 5.5f, 1.2f), 1, zj, 109, -40.3f, key);
    // The back, seen from behind: the stand's steel outline and the speakers' grilles.
    const std::size_t back = out.size();
    const auto bar = [&](float x0, float y0, float x1, float y1) {
        gfx::AddBox(out, std::abs(x1 - x0) + 1.2f, std::abs(y1 - y0) + 1.2f, 0.5f, Part::Steel, (x0 + x1) * 0.5f,
                    (y0 + y1) * 0.5f, z + 0.1f);
    };
    for (const float side : {-1.0f, 1.0f}) {
        bar(side * 91, -2.4f, side * 73.3f, -2.4f);
        bar(side * 91, -2.4f, side * 91, -54);
        Plate(out, Rounded(48, 2.6f, 1.3f), 0.2f, z, side * 32, -45, {26, 27, 30, 255});
    }
    bar(-91, -54, 91, -54);
    ToBack(out, back);
    return Finish(out, s, mesh);
}

} // namespace

ConsoleModel HandheldFor(const std::string& name) {
    static std::map<std::string, std::unique_ptr<gfx::Mesh3D>> meshes;
    ConsoleModel (*build)(std::unique_ptr<gfx::Mesh3D>&) = name == "dmg"       ? Dmg
                                                           : name == "cgb"     ? Cgb
                                                           : name == "agb"     ? Agb
                                                           : name == "ds_lite" ? DsLite
                                                           : name == "switch2" ? Switch2
                                                                               : nullptr;
    if (build == nullptr) {
        return {};
    }
    // Built once; the figures come from the same build each time.
    static std::map<std::string, ConsoleModel> models;
    if (const auto it = models.find(name); it != models.end()) {
        return it->second;
    }
    return models[name] = build(meshes[name]);
}

} // namespace den::app
