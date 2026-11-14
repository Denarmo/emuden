// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_objects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// The moulds of handheld cartridges, which fill in what the shape's defaults do not.
struct StyleDefaults {
    const char* style;
    float aspect, depth;
    gfx::Rect label;
    int grip;
    const char* label_edge;
};

constexpr StyleDefaults STYLES[] = {
    {"pocket", 0.877f, 0.12f, {0.105f, 0.3f, 0.79f, 0.6f}, 5, "foil"},
    {"pocket_wide", 1.64f, 0.18f, {0.11f, 0.29f, 0.78f, 0.64f}, 0, "none"},
};

constexpr SlabParts All(Part part) {
    return SlabParts{part, part, part, part, part};
}

struct Built {
    std::vector<gfx::MeshVertex> solid, clear;
    // The shell's surfaces: see-through ones go to the transparent pass.
    std::vector<gfx::MeshVertex>& Shell(const GameObject& o) {
        return o.smoke ? clear : solid;
    }
};

Part ShellPart(const GameObject& o) {
    return o.smoke ? Part::Smoke : Part::Shell;
}

// The middle of a 0..1 rectangle of the front, in mesh units.
gfx::Vec2 Centre(const GameObject& o, const gfx::Rect& r) {
    return {(r.x + r.w * 0.5f - 0.5f) * o.aspect, 0.5f - (r.y + r.h * 0.5f)};
}

// The sticker, standing a hair proud of the face at `z`: the cover inside a border of paper
// or foil when it has one.
void AddSticker(std::vector<gfx::MeshVertex>& out, const GameObject& o, float z) {
    const float a = o.aspect;
    const gfx::Rect c = o.CoverRect();
    const gfx::Vec2 cc = Centre(o, c);
    if (o.label_edge == "none") {
        SlabParts parts = All(Part::Paper);
        parts.front = Part::Label;
        gfx::AddSlab(out, RoundedOutline(c.w * a, c.h, 0.008f, 0.008f), 0.004f, 0.0012f, parts, z + 0.002f, cc.x,
                     cc.y, 2);
        return;
    }
    const Part edge = o.label_edge == "foil" ? Part::Steel : Part::Paper;
    const gfx::Vec2 sc = Centre(o, o.label);
    gfx::AddSlab(out, RoundedOutline(o.label.w * a, o.label.h, 0.01f, 0.01f), 0.004f, 0.0012f, All(edge), z + 0.002f,
                 sc.x, sc.y, 2);
    gfx::AddSlab(out, RoundedOutline(c.w * a, c.h, 0, 0), 0.0015f, 0, All(Part::Label), z + 0.0045f, cc.x, cc.y, 0);
}

// The board inside a see-through shell: chips, a battery and the traces.
void AddBoard(std::vector<gfx::MeshVertex>& out, const GameObject& o, bool wide) {
    const float a = o.aspect, z = -o.depth * 0.12f;
    SlabParts board = All(Part::Board);
    board.side = Part::Dark;
    gfx::AddSlab(out, RoundedOutline(a * 0.86f, 0.9f, 0.01f, 0.01f), 0.01f, 0, board, z, 0, -0.03f, 0);
    gfx::AddBox(out, a * 0.32f, 0.17f, 0.014f, Part::Dark, -a * 0.18f, wide ? 0.1f : -0.05f, z + 0.012f);
    gfx::AddBox(out, a * 0.16f, 0.1f, 0.012f, Part::Dark, a * 0.22f, wide ? -0.15f : -0.2f, z + 0.011f);
    if (!wide) {
        const std::size_t from = out.size();
        gfx::AddRing(out, 0.0f, 0.12f, 0.02f, Part::Steel, Part::Steel, z + 0.016f, 40);
        for (std::size_t i = from; i < out.size(); ++i) {
            out[i].x += a * 0.12f;
            out[i].y += 0.27f;
        }
    }
}

// The back: contacts along the bottom edge and, on a screwed shell, the screw in its well.
void AddBack(std::vector<gfx::MeshVertex>& out, const GameObject& o, bool screw) {
    const float a = o.aspect, d = o.depth;
    if (screw) {
        gfx::AddRing(out, 0.008f, 0.03f, 0.004f, Part::Dark, Part::Dark, -d * 0.5f - 0.001f, 20);
    }
    gfx::AddBox(out, a * 0.72f, 0.075f, 0.004f, Part::Dark, 0, -0.5f + 0.055f, -d * 0.5f - 0.001f);
    constexpr int CONTACTS = 14;
    const float pitch = a * 0.68f / CONTACTS;
    for (int i = 0; i < CONTACTS; ++i) {
        gfx::AddBox(out, pitch * 0.55f, 0.06f, 0.002f, Part::Metal, -a * 0.34f + pitch * (i + 0.5f), -0.5f + 0.05f,
                    -d * 0.5f - 0.004f);
    }
}

void BuildCartridge(Built& b, const GameObject& o) {
    const float a = o.aspect, d = o.depth, front = d * 0.5f;
    auto& shell = b.Shell(o);
    const Part part = ShellPart(o);
    gfx::AddSlab(shell, RoundedOutline(a, 1, o.radius, 0.03f, o.notch), d, std::min(0.022f, d * 0.3f), All(part));
    // The label sits in a sunk well.
    const gfx::Vec2 lc = Centre(o, o.label);
    gfx::AddGroove(shell, RoundedOutline(o.label.w * a + 0.05f, o.label.h + 0.05f, 0.025f, 0.025f), 0.013f,
                   front + 0.002f, lc.x, lc.y, part);
    AddSticker(b.solid, o, front);
    // Grip ridges in the free part under the label.
    const float top = 0.5f - (o.label.y + o.label.h) - 0.05f, bottom = -0.5f + 0.05f;
    const int ridges = std::clamp(o.grip, 0, 8);
    if (ridges > 0 && top - bottom > 0.05f) {
        const float gap = (top - bottom) / ridges;
        for (int i = 0; i < ridges; ++i) {
            const float h = std::min(0.02f, gap * 0.45f);
            gfx::AddGroove(shell, RoundedOutline(a * 0.56f, h, h * 0.45f, h * 0.45f), h * 0.5f, front + 0.002f, 0,
                           top - gap * (i + 0.5f), part);
        }
    }
    if (o.smoke) {
        AddBoard(b.solid, o, false);
    }
    AddBack(b.solid, o, true);
}

// A handheld's cartridge after the real moulds: the top corners stepped down with grip ridges
// on them, a sunk panel along the top, the label in a sunk well and an arrow under it; the
// wide kind with deep shoulders and a band along the top instead.
void BuildPocket(Built& b, const GameObject& o) {
    const bool wide = o.style == "pocket_wide";
    const float a = o.aspect, d = o.depth, face = d * 0.5f, z = face + 0.002f;
    auto& shell = b.Shell(o);
    const Part part = ShellPart(o);
    const float shoulder = wide ? 0.07f : a * 0.105f, drop = wide ? 0.21f : 0.035f;
    gfx::AddSlab(shell,
                 gfx::ShoulderedOutline(a, 1, shoulder, drop, wide ? 0.03f : 0.01f, wide ? 0.06f : 0.022f,
                                        wide ? 0.04f : 0.02f),
                 d, std::min(0.012f, d * 0.2f), All(part));
    const gfx::Rect& l = o.label;
    const gfx::Vec2 lc = Centre(o, l);
    const float margin = 0.022f, label_top = 0.5f - l.y, label_bottom = 0.5f - (l.y + l.h);
    const float well_top = label_top + margin, well_bottom = wide ? label_bottom - margin : -0.5f + 0.035f;
    gfx::AddGroove(shell, RoundedOutline(l.w * a + 2 * margin, well_top - well_bottom, 0.03f, 0.03f), 0.012f, z, lc.x,
                   (well_top + well_bottom) * 0.5f, part);
    if (wide) {
        // The band along the top, between the shoulders, and a lip across the shoulders.
        const float top = 0.5f - 0.03f, bottom = well_top + 0.03f;
        gfx::AddGroove(shell, RoundedOutline(a - 2 * shoulder - 0.08f, top - bottom, 0.02f, 0.02f), 0.012f, z, 0,
                       (top + bottom) * 0.5f, part);
        gfx::AddGroove(shell, RoundedOutline(a - 0.06f, 0.012f, 0.005f, 0.005f), 0.006f, z, 0, 0.5f - drop - 0.035f,
                       part);
    } else {
        // The arrow under the label, pointing the way in.
        const float ay = (label_bottom + well_bottom) * 0.5f;
        gfx::AddGroove(shell, {{0, ay - 0.02f}, {0.05f, ay + 0.02f}, {-0.05f, ay + 0.02f}}, 0.014f, z, 0, 0, part);
        // The panel along the top, round-ended.
        const float top = 0.5f - 0.045f, bottom = well_top + 0.035f, px = a * 0.5f - shoulder - 0.03f;
        const float r = (top - bottom) * 0.5f;
        gfx::AddGroove(shell, RoundedOutline(2 * px, top - bottom, r, r, 0, 10), 0.014f, z, 0, (top + bottom) * 0.5f,
                       part);
        // Grip ridges on both stepped corners.
        const int ridges = std::clamp(o.grip, 0, 8);
        const float rw = shoulder + 0.03f;
        for (int i = 0; i < ridges; ++i) {
            const float y = 0.5f - drop - 0.03f - static_cast<float>(i) * 0.03f;
            for (const float side : {-1.0f, 1.0f}) {
                gfx::AddGroove(shell, RoundedOutline(rw, 0.012f, 0.004f, 0.004f), 0.006f, z,
                               side * (a * 0.5f - rw * 0.5f - 0.012f), y, part);
            }
        }
    }
    AddSticker(b.solid, o, face);
    if (o.smoke) {
        AddBoard(b.solid, o, wide);
    }
    AddBack(b.solid, o, !wide);
}

void BuildCard(std::vector<gfx::MeshVertex>& out, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    gfx::AddSlab(out, RoundedOutline(a, 1, o.radius, o.radius, o.notch), d, std::min(0.015f, d * 0.35f),
                 All(Part::Shell));
    const gfx::Vec2 lc = Centre(o, o.label);
    gfx::AddGroove(out, RoundedOutline(o.label.w * a + 0.04f, o.label.h + 0.04f, 0.02f, 0.02f), 0.01f, d * 0.5f + 0.002f,
                   lc.x, lc.y);
    AddSticker(out, o, d * 0.5f);
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

// A cartridge or card from its photograph: the moulded body, the photo for a front and the
// back's picture behind; the photo shows the relief and the label as they really are.
void BuildPhoto(Built& b, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    SlabParts parts = All(Part::Shell);
    parts.front = Part::Photo;
    parts.back = Part::Back;
    const std::vector<gfx::Vec2> outline =
        o.style == "pocket"        ? gfx::ShoulderedOutline(a, 1, a * 0.105f, 0.035f, 0.01f, 0.022f, 0.02f)
        : o.style == "pocket_wide" ? gfx::ShoulderedOutline(a, 1, 0.07f, 0.21f, 0.03f, 0.06f, 0.04f)
                                   : RoundedOutline(a, 1, o.radius, std::max(0.02f, o.radius), o.notch);
    gfx::AddSlab(b.solid, outline, d, std::min(0.012f, d * 0.2f), parts);
}

void BuildCase(std::vector<gfx::MeshVertex>& out, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    SlabParts parts = All(Part::Shell);
    parts.front = Part::Gloss;
    parts.back = Part::Back;
    parts.spine = Part::Spine;
    gfx::AddSlab(out, RoundedOutline(a, 1, o.radius, o.radius), d, std::min(0.014f, d * 0.3f), parts);
    // The crease of the sleeve along the hinge.
    gfx::AddBox(out, 0.005f, 0.955f, 0.002f, Part::Dark, -a * 0.5f + 0.032f, 0, d * 0.5f + 0.0005f);
}

void BuildJewel(std::vector<gfx::MeshVertex>& out, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    const float hinge = a * 0.055f;
    // The black tray at the back and the printed inlay under the lid.
    SlabParts tray = All(Part::Dark);
    tray.back = Part::Back;
    gfx::AddSlab(out, RoundedOutline(a * 0.99f, 0.99f, 0.01f, 0.01f), d * 0.6f, 0.006f, tray, -d * 0.2f);
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
    const std::string key =
        std::format("{} {} {} {} {} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {}", o.shape, o.style,
                    o.label_edge, o.smoke, o.photo, o.aspect, o.depth, o.label.x, o.label.y, o.label.w, o.label.h, o.radius,
                    o.notch, o.grip);
    Meshes& meshes = cache[key];
    if (!meshes.solid) {
        Built b;
        if (o.photo) {
            BuildPhoto(b, o);
        } else if (o.shape == "disc") {
            BuildDisc(b.solid, o.depth);
        } else if (o.shape == "cartridge" && o.style != "plain") {
            BuildPocket(b, o);
        } else if (o.shape == "cartridge") {
            BuildCartridge(b, o);
        } else if (o.shape == "card") {
            BuildCard(b.solid, o);
        } else if (o.shape == "jewel") {
            BuildJewel(b.solid, o);
            gfx::AddSlab(b.clear, RoundedOutline(o.aspect, 1, o.radius, o.radius), o.depth * 0.26f, 0.008f,
                         All(Part::Clear), o.depth * 0.5f - o.depth * 0.13f);
        } else {
            BuildCase(b.solid, o);
        }
        meshes.solid = Upload(b.solid);
        meshes.clear = Upload(b.clear);
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
    o.aspect = shape->aspect;
    o.depth = shape->depth;
    o.label = shape->label;
    o.grip = shape->grip;
    o.style = "plain";
    o.label_edge = "none";
    for (const StyleDefaults& s : STYLES) {
        if (o.shape == "cartridge" && look.style == s.style) {
            o.style = s.style;
            o.aspect = s.aspect;
            o.depth = s.depth;
            o.label = s.label;
            o.grip = s.grip;
            o.label_edge = s.label_edge;
        }
    }
    o.aspect = look.aspect > 0 ? look.aspect : o.aspect;
    o.depth = look.depth > 0 ? look.depth : o.depth;
    o.shell = look.has_shell ? look.shell : shape->shell;
    o.smoke = o.shell.a < 250 && (o.shape == "cartridge" || o.shape == "card");
    o.shell.a = o.smoke ? std::max<std::uint8_t>(o.shell.a, 60) : 255;
    o.label = look.label.w > 0 ? look.label : o.label;
    o.label_edge = look.label_edge.empty() ? o.label_edge : look.label_edge;
    o.radius = look.radius >= 0 ? look.radius : shape->radius;
    o.notch = look.notch >= 0 ? look.notch : shape->notch;
    o.grip = look.grip >= 0 ? look.grip : o.grip;
    o.disc = look.disc && (o.shape == "case" || o.shape == "jewel");
    return o;
}

GameObject GameObject::WithPhoto(gfx::Color plastic, float photo_aspect) const {
    GameObject o = *this;
    o.photo = true;
    o.smoke = false;
    o.shell = plastic;
    o.shell.a = 255;
    // Proportions within reach of the mould's: a photo cropped oddly does not squash the cart.
    o.aspect = std::clamp(photo_aspect, aspect * 0.8f, aspect * 1.25f);
    return o;
}

GameObject GameObject::WithCover(float cover_aspect) const {
    GameObject o = *this;
    // A jewel case's inlay leaves room for the hinge; a keep case's sleeve covers its front.
    const float wanted = shape == "jewel" ? cover_aspect + 0.09f : cover_aspect;
    o.aspect = std::round(std::clamp(wanted, aspect * 0.82f, aspect * 1.2f) * 100.0f) / 100.0f;
    return o;
}

gfx::Rect GameObject::CoverRect() const {
    if (label_edge == "none") {
        return label;
    }
    // Printed labels leave a strip each side for the small print, less above and below.
    const float side = label.w * 0.09f, top = label.h * 0.035f, bottom = label.h * 0.03f;
    return gfx::Rect{label.x + side, label.y + top, label.w - 2 * side, label.h - top - bottom};
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
