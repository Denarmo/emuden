// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_objects.h"

#include <algorithm>
#include <format>
#include <map>

#include "den/gfx/mesh_shapes.h"

namespace den::app {

namespace {

using gfx::Part;
using gfx::RoundedOutline;
using gfx::SlabParts;

struct ShapeDefaults {
    const char* shape;
    float aspect, depth;
    gfx::Color shell;
    gfx::Rect label;
    float radius, notch;
    int grip;
};

// Generic proportions of the kinds of carts, cards, cases and discs games came in.
constexpr ShapeDefaults SHAPES[] = {
    {"cartridge", 0.9f, 0.16f, {143, 144, 150, 255}, {0.1f, 0.1f, 0.8f, 0.6f}, 0.03f, 0, 4},
    {"card", 0.86f, 0.07f, {58, 59, 66, 255}, {0.07f, 0.17f, 0.86f, 0.76f}, 0.06f, 0.08f, 0},
    {"case", 0.7f, 0.075f, {18, 18, 22, 255}, {0, 0, 1, 1}, 0.025f, 0, 0},
    {"jewel", 1.12f, 0.07f, {16, 16, 20, 255}, {0, 0, 1, 1}, 0.012f, 0, 0},
    {"disc", 1.0f, 0.012f, {200, 204, 212, 255}, {0, 0, 1, 1}, 0, 0, 0},
};

constexpr SlabParts All(Part part) {
    return SlabParts{part, part, part, part, part};
}

// The label in its recess: a dark groove around a paper sticker standing a hair proud.
void AddLabel(std::vector<gfx::MeshVertex>& out, const GameObject& o, float front_z) {
    const gfx::Rect& l = o.label;
    const float a = o.aspect;
    const float cx = (l.x + l.w * 0.5f - 0.5f) * a, cy = 0.5f - (l.y + l.h * 0.5f);
    const float lw = l.w * a, lh = l.h;
    gfx::AddSlab(out, RoundedOutline(lw + 0.026f, lh + 0.026f, 0.02f, 0.02f), 0.006f, 0, All(Part::Dark),
                 front_z - 0.001f, cx, cy);
    SlabParts sticker = All(Part::Paper);
    sticker.front = Part::Label;
    gfx::AddSlab(out, RoundedOutline(lw, lh, 0.012f, 0.012f), 0.004f, 0.0015f, sticker, front_z + 0.0025f, cx, cy, 2);
}

void BuildCartridge(std::vector<gfx::MeshVertex>& out, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    gfx::AddSlab(out, RoundedOutline(a, 1, o.radius, 0.03f, o.notch), d, std::min(0.022f, d * 0.3f), All(Part::Shell));
    AddLabel(out, o, d * 0.5f);
    // Grip ridges in the free part under the label.
    const float top = 0.5f - (o.label.y + o.label.h) - 0.04f, bottom = -0.5f + 0.05f;
    const int ridges = std::clamp(o.grip, 0, 8);
    if (ridges > 0 && top - bottom > 0.05f) {
        const float gap = (top - bottom) / ridges;
        for (int i = 0; i < ridges; ++i) {
            gfx::AddSlab(out, RoundedOutline(a * 0.56f, std::min(0.02f, gap * 0.45f), 0.008f, 0.008f), 0.014f, 0.005f,
                         All(Part::Shell), d * 0.5f, 0, top - gap * (i + 0.5f), 2);
        }
    }
    // The back: a screw in its well and the contacts along the bottom edge.
    gfx::AddRing(out, 0.008f, 0.03f, 0.004f, Part::Dark, Part::Dark, -d * 0.5f - 0.001f, 20);
    gfx::AddBox(out, a * 0.72f, 0.075f, 0.004f, Part::Dark, 0, -0.5f + 0.055f, -d * 0.5f - 0.001f);
    constexpr int CONTACTS = 14;
    const float pitch = a * 0.68f / CONTACTS;
    for (int i = 0; i < CONTACTS; ++i) {
        gfx::AddBox(out, pitch * 0.55f, 0.06f, 0.002f, Part::Metal, -a * 0.34f + pitch * (i + 0.5f), -0.5f + 0.05f,
                    -d * 0.5f - 0.004f);
    }
}

void BuildCard(std::vector<gfx::MeshVertex>& out, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    gfx::AddSlab(out, RoundedOutline(a, 1, o.radius, o.radius, o.notch), d, std::min(0.015f, d * 0.35f),
                 All(Part::Shell));
    AddLabel(out, o, d * 0.5f);
    // A ridge to hold it by, and the contacts on the back's top edge.
    gfx::AddSlab(out, RoundedOutline(a * 0.5f, 0.022f, 0.01f, 0.01f), 0.01f, 0.004f, All(Part::Shell), d * 0.5f, 0,
                 0.5f - o.label.y * 0.5f, 2);
    constexpr int CONTACTS = 16;
    const float pitch = a * 0.8f / CONTACTS;
    for (int i = 0; i < CONTACTS; ++i) {
        gfx::AddBox(out, pitch * 0.6f, 0.07f, 0.002f, Part::Metal, -a * 0.4f + pitch * (i + 0.5f), 0.5f - 0.07f,
                    -d * 0.5f - 0.001f);
    }
}

void BuildCase(std::vector<gfx::MeshVertex>& out, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    SlabParts parts = All(Part::Shell);
    parts.front = Part::Gloss;
    parts.spine = Part::Spine;
    gfx::AddSlab(out, RoundedOutline(a, 1, o.radius, o.radius), d, std::min(0.014f, d * 0.3f), parts);
    // The crease of the sleeve along the hinge.
    gfx::AddBox(out, 0.005f, 0.955f, 0.002f, Part::Dark, -a * 0.5f + 0.032f, 0, d * 0.5f + 0.0005f);
}

void BuildJewel(std::vector<gfx::MeshVertex>& out, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    const float hinge = a * 0.055f;
    // The black tray at the back and the printed inlay under the lid.
    gfx::AddSlab(out, RoundedOutline(a * 0.99f, 0.99f, 0.01f, 0.01f), d * 0.6f, 0.006f, All(Part::Dark), -d * 0.2f);
    SlabParts inlay = All(Part::Paper);
    inlay.front = Part::Label;
    gfx::AddSlab(out, RoundedOutline(a - hinge - 0.03f, 0.95f, 0, 0), 0.003f, 0, inlay, d * 0.5f - 0.016f,
                 hinge * 0.5f + 0.004f, 0);
    // The hinge, with the spine insert showing on its side.
    SlabParts spine = All(Part::Dark);
    spine.spine = Part::Spine;
    gfx::AddSlab(out, RoundedOutline(hinge, 1, 0.01f, 0.01f), d, 0.008f, spine, 0, -a * 0.5f + hinge * 0.5f, 0);
}

void BuildDisc(std::vector<gfx::MeshVertex>& out, float depth) {
    gfx::AddRing(out, 0.075f, 0.5f, depth, Part::DiscPrint, Part::DiscData);
}

std::unique_ptr<gfx::Mesh3D> Upload(const std::vector<gfx::MeshVertex>& vertices) {
    if (vertices.empty()) {
        return nullptr;
    }
    auto mesh = std::make_unique<gfx::Mesh3D>();
    mesh->Set(vertices);
    return mesh;
}

struct Meshes {
    std::unique_ptr<gfx::Mesh3D> solid, clear;
};

const Meshes& MeshesFor(const GameObject& o) {
    static std::map<std::string, Meshes> cache;
    const std::string key = std::format("{} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {}", o.shape,
                                        o.aspect, o.depth, o.label.x, o.label.y, o.label.w, o.label.h, o.radius,
                                        o.notch, o.grip);
    Meshes& meshes = cache[key];
    if (!meshes.solid) {
        std::vector<gfx::MeshVertex> solid, clear;
        if (o.shape == "disc") {
            BuildDisc(solid, o.depth);
        } else if (o.shape == "cartridge") {
            BuildCartridge(solid, o);
        } else if (o.shape == "card") {
            BuildCard(solid, o);
        } else if (o.shape == "jewel") {
            BuildJewel(solid, o);
            gfx::AddSlab(clear, RoundedOutline(o.aspect, 1, o.radius, o.radius), o.depth * 0.26f, 0.008f,
                         All(Part::Clear), o.depth * 0.5f - o.depth * 0.13f);
        } else {
            BuildCase(solid, o);
        }
        meshes.solid = Upload(solid);
        meshes.clear = Upload(clear);
    }
    return meshes;
}

} // namespace

GameObject Resolve(const ObjectLook& look) {
    const ShapeDefaults* shape = &SHAPES[2];
    for (const ShapeDefaults& s : SHAPES) {
        shape = look.shape == s.shape ? &s : shape;
    }
    GameObject o;
    o.shape = shape->shape;
    o.aspect = look.aspect > 0 ? look.aspect : shape->aspect;
    o.depth = look.depth > 0 ? look.depth : shape->depth;
    o.shell = look.has_shell ? look.shell : shape->shell;
    o.label = look.label.w > 0 ? look.label : shape->label;
    o.radius = look.radius >= 0 ? look.radius : shape->radius;
    o.notch = look.notch >= 0 ? look.notch : shape->notch;
    o.grip = look.grip >= 0 ? look.grip : shape->grip;
    o.disc = look.disc && (o.shape == "case" || o.shape == "jewel");
    return o;
}

const gfx::Mesh3D& GameObject::Mesh() const {
    return *MeshesFor(*this).solid;
}

const gfx::Mesh3D* GameObject::ClearMesh() const {
    return MeshesFor(*this).clear.get();
}

const gfx::Mesh3D* GameObject::DiscMesh() const {
    if (!disc) {
        return nullptr;
    }
    static std::unique_ptr<gfx::Mesh3D> mesh;
    if (!mesh) {
        std::vector<gfx::MeshVertex> vertices;
        BuildDisc(vertices, 0.012f);
        mesh = Upload(vertices);
    }
    return mesh.get();
}

} // namespace den::app
