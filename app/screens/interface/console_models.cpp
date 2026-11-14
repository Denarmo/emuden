// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console_models.h"

#include <map>
#include <memory>
#include <vector>

#include "den/gfx/mesh_shapes.h"

namespace den::app {

namespace {

using gfx::Part;
using gfx::RoundedOutline;
using gfx::SlabParts;
using Vertices = std::vector<gfx::MeshVertex>;

constexpr SlabParts All(Part part) {
    return SlabParts{part, part, part, part, part};
}

// Moves the vertices added since `from` by (dx, dy).
void Shift(Vertices& out, std::size_t from, float dx, float dy) {
    for (std::size_t i = from; i < out.size(); ++i) {
        out[i].x += dx;
        out[i].y += dy;
    }
}

// A round button standing out of the face at `z`.
void Button(Vertices& out, float x, float y, float radius, float z, Part part = Part::Dark) {
    const std::size_t from = out.size();
    gfx::AddRing(out, 0, radius, 0.02f, part, part, z + 0.006f, 24);
    Shift(out, from, x, y);
}

// A cross-shaped pad.
void Pad(Vertices& out, float x, float y, float size, float z) {
    gfx::AddBox(out, size, size * 0.32f, 0.024f, Part::Dark, x, y, z + 0.008f);
    gfx::AddBox(out, size * 0.32f, size, 0.024f, Part::Dark, x, y, z + 0.008f);
}

// A small pill-shaped key.
void Key(Vertices& out, float x, float y, float w, float z) {
    gfx::AddSlab(out, RoundedOutline(w, w * 0.32f, w * 0.15f, w * 0.15f), 0.012f, 0.004f, All(Part::Dark), z + 0.004f,
                 x, y, 2);
}

// The screen in its dark surround, at height `cy` of a face at `z`.
void Screen(Vertices& out, float cy, float bezel_w, float bezel_h, float w, float h, float z) {
    gfx::AddSlab(out, RoundedOutline(bezel_w, bezel_h, 0.02f, 0.05f), 0.006f, 0.002f, All(Part::Dark), z + 0.002f, 0,
                 cy, 2);
    SlabParts glass = All(Part::Dark);
    glass.front = Part::Screen;
    gfx::AddSlab(out, RoundedOutline(w, h, 0, 0), 0.004f, 0, glass, z + 0.006f, 0, cy + bezel_h * 0.02f, 0);
}

// Speaker slots: short grooves in a row.
void Speaker(Vertices& out, float x, float y, float z, int count) {
    for (int i = 0; i < count; ++i) {
        gfx::AddGroove(out, RoundedOutline(0.012f, 0.09f, 0.006f, 0.006f), 0.006f, z + 0.002f, x + i * 0.024f, y);
    }
}

// The back's battery lid (a slightly darker panel with a groove round it) and four screws.
void Back(Vertices& out, float w, float h, float y, float z) {
    SlabParts lid = All(Part::Dark);
    lid.back = Part::Shell;
    gfx::AddSlab(out, RoundedOutline(w + 0.012f, h + 0.012f, 0.02f, 0.02f), 0.003f, 0, All(Part::Dark), -z - 0.0005f, 0, y);
    gfx::AddSlab(out, RoundedOutline(w, h, 0.018f, 0.018f), 0.004f, 0.0015f, lid, -z - 0.001f, 0, y, 2);
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sy : {-1.0f, 1.0f}) {
            const std::size_t from = out.size();
            gfx::AddRing(out, 0.004f, 0.012f, 0.004f, Part::Dark, Part::Dark, -z - 0.001f, 12);
            Shift(out, from, sx * (w * 0.5f + 0.05f), y + sy * (h * 0.5f + 0.06f));
        }
    }
}

Vertices Portrait() {
    Vertices out;
    const float w = 0.6f, d = 0.2f, z = d * 0.5f;
    gfx::AddSlab(out, RoundedOutline(w, 1, 0.03f, 0.07f), d, 0.03f, All(Part::Shell));
    Screen(out, 0.24f, 0.5f, 0.42f, 0.3f, 0.27f, z);
    Button(out, -0.2f, 0.29f, 0.011f, z + 0.004f, Part::Led);
    Pad(out, -0.15f, -0.13f, 0.13f, z);
    Button(out, 0.19f, -0.09f, 0.035f, z);
    Button(out, 0.09f, -0.15f, 0.035f, z);
    Key(out, -0.05f, -0.3f, 0.07f, z);
    Key(out, 0.05f, -0.3f, 0.07f, z);
    Speaker(out, 0.11f, -0.4f, z, 5);
    // The slot at the top of the back, the battery lid and the screws.
    gfx::AddBox(out, 0.5f, 0.05f, 0.004f, Part::Dark, 0, 0.47f, -z - 0.002f);
    Back(out, 0.4f, 0.3f, -0.2f, z);
    return out;
}

Vertices Landscape() {
    Vertices out;
    const float w = 1.75f, d = 0.24f, z = d * 0.5f;
    gfx::AddSlab(out, RoundedOutline(w, 1, 0.12f, 0.32f), d, 0.05f, All(Part::Shell));
    Screen(out, 0.05f, 0.82f, 0.66f, 0.62f, 0.44f, z);
    Button(out, -0.62f, 0.32f, 0.016f, z + 0.004f, Part::Led);
    Pad(out, -0.6f, 0.02f, 0.2f, z);
    Button(out, 0.58f, 0.0f, 0.06f, z);
    Button(out, 0.72f, 0.12f, 0.06f, z);
    Key(out, -0.62f, -0.3f, 0.09f, z);
    Key(out, -0.5f, -0.36f, 0.09f, z);
    Speaker(out, 0.55f, -0.3f, z, 4);
    gfx::AddBox(out, 0.8f, 0.06f, 0.004f, Part::Dark, 0, 0.46f, -z - 0.002f);
    Back(out, 0.5f, 0.32f, -0.15f, z);
    return out;
}

Vertices Clamshell() {
    Vertices out;
    const float w = 0.95f, h = 0.47f, d = 0.07f, z = d * 0.5f;
    // The lid, upright above the hinge.
    std::size_t from = out.size();
    gfx::AddSlab(out, RoundedOutline(w, h, 0.05f, 0.02f), d, 0.015f, All(Part::Shell));
    Screen(out, 0, 0.78f, 0.4f, 0.52f, 0.36f, z);
    Shift(out, from, 0, h * 0.5f + 0.02f);
    // The base, folded open towards the viewer about the hinge.
    from = out.size();
    gfx::AddSlab(out, RoundedOutline(w, h, 0.02f, 0.06f), d * 1.2f, 0.02f, All(Part::Shell));
    Screen(out, 0.02f, 0.5f, 0.4f, 0.4f, 0.32f, z * 1.2f);
    Pad(out, -0.36f, 0.04f, 0.12f, z * 1.2f);
    Button(out, 0.36f, 0.08f, 0.026f, z * 1.2f);
    Button(out, 0.42f, 0.02f, 0.026f, z * 1.2f);
    Button(out, 0.30f, 0.02f, 0.026f, z * 1.2f);
    Button(out, 0.36f, -0.04f, 0.026f, z * 1.2f);
    Button(out, -0.4f, 0.17f, 0.01f, z * 1.2f + 0.004f, Part::Led);
    const gfx::Mat4 fold = gfx::Mat4::RotateX(-0.75f) * gfx::Mat4::Translate(0, -h * 0.5f, 0);
    for (std::size_t i = from; i < out.size(); ++i) {
        gfx::MeshVertex& v = out[i];
        const float* m = fold.m;
        const float x = v.x, y = v.y, zz = v.z;
        v.x = m[0] * x + m[4] * y + m[8] * zz + m[12];
        v.y = m[1] * x + m[5] * y + m[9] * zz + m[13];
        v.z = m[2] * x + m[6] * y + m[10] * zz + m[14];
        const float nx = v.nx, ny = v.ny, nz = v.nz;
        v.nx = m[0] * nx + m[4] * ny + m[8] * nz;
        v.ny = m[1] * nx + m[5] * ny + m[9] * nz;
        v.nz = m[2] * nx + m[6] * ny + m[10] * nz;
    }
    // The card slot's housing behind the hinge.
    gfx::AddBox(out, 0.42f, 0.07f, 0.08f, Part::Dark, 0, 0.0f, -0.09f);
    return out;
}

Vertices Home() {
    Vertices out;
    const float w = 2.4f, d = 1.6f, z = d * 0.5f;
    gfx::AddSlab(out, RoundedOutline(w, 1, 0.1f, 0.06f), d, 0.06f, All(Part::Shell));
    // The slot on top, the front's ridge, the power key, its light and two controller ports.
    gfx::AddBox(out, 0.95f, 0.012f, 0.42f, Part::Dark, 0, 0.5f + 0.004f, 0);
    gfx::AddGroove(out, RoundedOutline(w - 0.2f, 0.03f, 0.015f, 0.015f), 0.015f, z + 0.002f, 0, 0.12f);
    gfx::AddBox(out, 0.26f, 0.14f, 0.03f, Part::Dark, -0.82f, -0.18f, z + 0.012f);
    Button(out, -0.52f, -0.18f, 0.025f, z + 0.004f, Part::Led);
    gfx::AddBox(out, 0.2f, 0.14f, 0.02f, Part::Dark, 0.55f, -0.2f, z + 0.008f);
    gfx::AddBox(out, 0.2f, 0.14f, 0.02f, Part::Dark, 0.85f, -0.2f, z + 0.008f);
    return out;
}

} // namespace

ConsoleModel ConsoleFor(const std::string& name) {
    static std::map<std::string, std::unique_ptr<gfx::Mesh3D>> meshes;
    ConsoleModel model;
    Vertices (*build)() = nullptr;
    if (name == "portrait") {
        build = Portrait;
        model = {nullptr, 0.6f, 0.2f, 0.5f, -0.045f, 0.44f, 0.55f, true};
    } else if (name == "landscape") {
        build = Landscape;
        model = {nullptr, 1.75f, 0.24f, 0.5f, -0.06f, 0.43f, 0.6f, true};
    } else if (name == "clamshell") {
        build = Clamshell;
        model = {nullptr, 0.95f, 0.07f, 0.04f, -0.09f, 0.4f, 0.55f, true, 0.36f};
    } else if (name == "home") {
        build = Home;
        model = {nullptr, 2.4f, 1.6f, 0.5f, 0.0f, 1.3f, 0.45f, false};
    } else {
        return model;
    }
    auto& mesh = meshes[name];
    if (!mesh) {
        mesh = std::make_unique<gfx::Mesh3D>();
        mesh->Set(build());
    }
    model.mesh = mesh.get();
    return model;
}

} // namespace den::app
