// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/app/game_objects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>

#include "den/app/registry.h"
#include "den/gfx/mesh_shapes.h"
#include "object_build.h"

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
    {"pocket", 0.877f, 0.115f, {0.105f, 0.28f, 0.79f, 0.57f}, 5, "foil"},
};

gfx::Rect ClampStickerRect(gfx::Rect r) {
    if (r.y < 0.018f) {
        const float trim = 0.018f - r.y;
        r.y = 0.018f;
        r.h = std::max(0.05f, r.h - trim);
    }
    if (r.y + r.h > 0.982f) {
        r.h = std::max(0.05f, 0.982f - r.y);
    }
    return r;
}

std::vector<gfx::Vec2> PocketOutline(float a) {
    const float x = a * 0.5f, rt = 0.045f, rb = 0.02f;
    const float notch_x = x - 0.11f * a, notch_y = 0.37f;
    constexpr float PI = 3.14159265f;
    std::vector<gfx::Vec2> pts;
    const auto arc = [&](float cx, float cy, float r, float from, float to, int steps) {
        for (int i = 0; i <= steps; ++i) {
            const float ang = from + (to - from) * static_cast<float>(i) / steps;
            pts.push_back({cx + std::cos(ang) * r, cy + std::sin(ang) * r});
        }
    };
    arc(x - rb, -0.5f + rb, rb, -PI * 0.5f, 0.0f, 5);
    pts.push_back({x, notch_y});
    pts.push_back({notch_x, notch_y});
    pts.push_back({notch_x, 0.5f});
    arc(-x + rt, 0.5f - rt, rt, PI * 0.5f, PI, 5);
    arc(-x + rb, -0.5f + rb, rb, PI, PI * 1.5f, 5);
    return pts;
}

} // namespace

SlabParts All(Part part) {
    return SlabParts{part, part, part, part, part};
}

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
    const gfx::Rect c = ClampStickerRect(o.CoverRect());
    const gfx::Vec2 cc = Centre(o, c);
    if (o.label_edge == "none") {
        SlabParts parts = All(Part::Paper);
        parts.front = Part::Label;
        gfx::AddSlab(out, RoundedOutline(c.w * a, c.h, 0.008f, 0.008f), 0.004f, 0.0012f, parts, z + 0.002f, cc.x,
                     cc.y, 2);
        return;
    }
    const Part edge = o.label_edge == "foil" ? Part::Steel : Part::Paper;
    const gfx::Rect lbl = ClampStickerRect(o.label);
    const gfx::Vec2 sc = Centre(o, lbl);
    gfx::AddSlab(out, RoundedOutline(lbl.w * a, lbl.h, 0.01f, 0.01f), 0.004f, 0.0012f, All(edge), z + 0.002f,
                 sc.x, sc.y, 2);
    gfx::AddSlab(out, RoundedOutline(c.w * a, c.h, 0, 0), 0.0015f, 0, All(Part::Label), z + 0.0045f, cc.x, cc.y, 0);
}

void PaintFrom(std::vector<gfx::MeshVertex>& out, std::size_t from, gfx::Color color) {
    for (std::size_t i = from; i < out.size(); ++i) {
        out[i].u = static_cast<float>(color.r * 256 + color.g);
        out[i].v = static_cast<float>(color.b);
        out[i].part = static_cast<float>(Part::Paint);
    }
}

void AddHackSticker(std::vector<gfx::MeshVertex>& out, const GameObject& o, float z) {
    // Over the label's top right corner, tilted a little, as if put on by hand.
    const float w = std::clamp(o.aspect * 0.32f, 0.16f, 0.3f), h = w * 104.0f / 256.0f;
    const gfx::Rect& l = o.label;
    const float x = (l.x + l.w - 0.5f) * o.aspect - w * 0.38f, y = 0.5f - l.y - h * 0.3f;
    SlabParts parts = All(Part::Steel);
    parts.front = Part::Sticker;
    const std::size_t from = out.size();
    gfx::AddSlab(out, RoundedOutline(w, h, 0, 0), 0.003f, 0, parts, 0, 0, 0, 0);
    const float c = std::cos(-0.12f), s = std::sin(-0.12f);
    for (std::size_t i = from; i < out.size(); ++i) {
        gfx::MeshVertex& v = out[i];
        const float vx = v.x, vy = v.y, nx = v.nx, ny = v.ny;
        v.x = vx * c - vy * s + x;
        v.y = vx * s + vy * c + y;
        v.z += z + 0.009f;
        v.nx = nx * c - ny * s;
        v.ny = nx * s + ny * c;
    }
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

namespace {

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

// A Game Boy DMG cartridge after the real mould: asymmetric top-right power notch, upper side
// grip ridges, a sunk pill panel along the top, the label in a sunk well and an arrow under it.
void BuildPocket(Built& b, const GameObject& o) {
    const float a = o.aspect, d = o.depth, face = d * 0.5f;
    const float z = face + (o.photo ? 0.0012f : 0.002f);
    auto& shell = b.Shell(o);
    const Part part = ShellPart(o);
    const Part relief = o.photo ? Part::Photo : part;
    const float photo_w = o.photo ? a : 0.0f;
    SlabParts slab = All(part);
    if (o.photo) {
        slab.front = Part::Photo;
        slab.bevel = Part::Photo;
        slab.back = Part::Back;
    }
    gfx::AddSlab(shell, PocketOutline(a), d, std::min(0.012f, d * 0.2f), slab);
    const gfx::Rect& l = o.label;
    const gfx::Vec2 lc = Centre(o, l);
    const float margin = 0.022f, label_top = 0.5f - l.y, label_bottom = 0.5f - (l.y + l.h);
    const float well_top = label_top + margin, well_bottom = -0.5f + 0.035f;
    gfx::AddGroove(shell, RoundedOutline(l.w * a + 2 * margin, well_top - well_bottom, 0.03f, 0.03f), 0.012f, z, lc.x,
                   (well_top + well_bottom) * 0.5f, relief, false, photo_w);
    // The arrow under the label, pointing the way in.
    const float ay = (label_bottom + well_bottom) * 0.5f;
    gfx::AddGroove(shell, {{0, ay - 0.02f}, {0.05f, ay + 0.02f}, {-0.05f, ay + 0.02f}}, 0.014f, z, 0, 0, relief, false,
                   photo_w);
    // The sunken pill panel along the top.
    const float top = 0.5f - 0.045f, bottom = well_top + 0.04f, px = a * 0.5f - a * 0.135f - 0.02f;
    const float r = (top - bottom) * 0.5f;
    gfx::AddGroove(shell, RoundedOutline(2 * px, top - bottom, r, r, 0, 10), 0.014f, z, -a * 0.03f,
                   (top + bottom) * 0.5f, relief, false, photo_w);
    // Grip ridges along the upper left and right edges, below the DMG notch.
    const int ridges = std::clamp(o.grip, 0, 8);
    const float rw = a * 0.105f;
    for (int i = 0; i < ridges; ++i) {
        const float y = 0.345f - static_cast<float>(i) * 0.024f;
        for (const float side : {-1.0f, 1.0f}) {
            gfx::AddGroove(shell, RoundedOutline(rw, 0.011f, 0.004f, 0.004f), 0.005f, z,
                           side * (a * 0.5f - rw * 0.5f - 0.012f), y, relief, false, photo_w);
        }
    }
    if (!o.photo) {
        AddSticker(b.solid, o, face);
        AddBack(b.solid, o, true);
    }
    if (o.smoke) {
        AddBoard(b.solid, o, false);
    }
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

// A cartridge or card from its photograph when it has no console mould.
void BuildPhoto(Built& b, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    SlabParts parts = All(Part::Shell);
    parts.front = Part::Photo;
    parts.bevel = Part::Photo;
    parts.back = Part::Back;
    const std::optional<std::vector<gfx::Vec2>> mould = MouldOutline(o);
    const std::vector<gfx::Vec2> outline =
        mould                 ? *mould
        : o.style == "pocket" ? PocketOutline(a)
                              : RoundedOutline(a, 1, o.radius, std::max(0.02f, o.radius), o.notch);
    gfx::AddSlab(b.solid, outline, d, std::min(0.012f, d * 0.2f), parts);
}

// A cardboard box: the cover printed on its front, satin paper grain on back and spine.
void BuildCarton(std::vector<gfx::MeshVertex>& out, const GameObject& o) {
    SlabParts parts = All(Part::Shell);
    parts.front = Part::Print;
    parts.back = Part::CartonBack;
    parts.spine = Part::CartonSpine;
    const float r = o.radius > 0 ? o.radius : 0.005f;
    gfx::AddSlab(out, RoundedOutline(o.aspect, 1, r, r), o.depth, std::min(0.004f + r * 0.25f, o.depth * 0.25f), parts,
                 0, 0, 0, r > 0.01f ? 4 : 2);
    gfx::AddBox(out, o.aspect * 0.94f, 0.0025f, 0.004f, Part::Dark, 0, 0.499f, o.depth * 0.34f);
    gfx::AddBox(out, o.aspect * 0.94f, 0.0025f, 0.004f, Part::Dark, 0, -0.499f, o.depth * 0.34f);
}

void BuildCase(Built& b, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    const float r = o.radius > 0 ? o.radius : 0.025f;
    const float bevel = std::min(0.012f, d * 0.25f);
    if (o.smoke) {
        gfx::AddSlab(b.clear, RoundedOutline(a, 1, r, r), d, bevel, All(Part::Smoke));
    } else {
        gfx::AddSlab(b.solid, RoundedOutline(a, 1, r, r), d, bevel, All(Part::Shell));
        // Opening seam and recessed thumb-catch notch along the right wall.
        gfx::AddBox(b.solid, 0.002f, 0.94f, 0.010f, Part::Dark, a * 0.5f - 0.0005f, 0, 0);
        gfx::AddBox(b.solid, 0.008f, 0.14f, d * 0.52f, Part::Dark, a * 0.5f - 0.002f, 0, 0);
    }
    const float rim_y = 0.018f, rim_right = 0.022f;
    const float sw = std::max(0.1f, a - rim_right + 0.0015f), sh = 1.0f - 2.0f * rim_y;
    const float sx = -rim_right * 0.5f - 0.00075f;
    SlabParts sleeve = All(Part::Shell);
    sleeve.front = Part::Gloss;
    sleeve.back = Part::Back;
    sleeve.spine = Part::Spine;
    gfx::AddSlab(b.solid, RoundedOutline(sw, sh, r * 0.5f, r * 0.5f), d + 0.003f, std::min(0.006f, d * 0.12f), sleeve,
                 0, sx, 0, 3);
    // The crease of the sleeve along the hinge.
    gfx::AddBox(b.solid, 0.004f, sh * 0.985f, 0.0015f, Part::Dark, -a * 0.5f + 0.028f, 0, d * 0.5f + 0.0018f);
    // Clear outer protective sleeve over the paper insert.
    gfx::AddSlab(b.clear, RoundedOutline(sw + 0.001f, sh + 0.001f, r * 0.5f, r * 0.5f), d + 0.005f,
                 std::min(0.006f, d * 0.12f), All(Part::Clear), 0, sx, 0, 2);
}

void BuildJewel(Built& b, const GameObject& o) {
    const float a = o.aspect, d = o.depth;
    const float r = o.radius > 0 ? o.radius : 0.012f;
    const float hinge = o.full_inlay ? a * 0.028f : a * 0.095f;
    // The black tray at the back and the left ribbed spine strip under the lid.
    SlabParts tray = All(Part::Dark);
    tray.back = Part::Back;
    tray.spine = Part::Spine;
    gfx::AddSlab(b.solid, RoundedOutline(a * 0.995f, 0.995f, r, r), d * 0.58f, 0.005f, tray, -d * 0.21f);
    SlabParts spine = All(Part::Dark);
    spine.spine = Part::Spine;
    gfx::AddSlab(b.solid, RoundedOutline(hinge, 0.995f, r, r), d * 0.62f, 0.004f, spine, -d * 0.19f,
                 -a * 0.5f + hinge * 0.5f, 0);
    if (!o.full_inlay && hinge > 0.04f) {
        constexpr int RIBS = 6;
        const float pitch = (hinge - 0.02f) / RIBS;
        for (int i = 0; i < RIBS; ++i) {
            const float rx = -a * 0.5f + 0.01f + pitch * (i + 0.5f);
            gfx::AddGroove(b.solid, RoundedOutline(pitch * 0.45f, 0.94f, 0.002f, 0.002f), pitch * 0.18f,
                           d * 0.12f + 0.001f, rx, 0, Part::Dark, true);
        }
    }
    // Printed front booklet inside the tray.
    SlabParts inlay = All(Part::Paper);
    inlay.front = Part::Label;
    gfx::AddSlab(b.solid, RoundedOutline(a - hinge - 0.022f, 0.955f, 0, 0), 0.005f, 0, inlay, d * 0.10f,
                 hinge * 0.5f + 0.003f, 0, 0);
    // Flush clear acrylic lid and hinge tabs.
    gfx::AddSlab(b.clear, RoundedOutline(a, 1.0f, r, r), d * 0.42f, 0.006f, All(Part::Clear), d * 0.29f);
    for (const float side : {-1.0f, 1.0f}) {
        gfx::AddBox(b.clear, 0.008f, 0.045f, d * 0.85f, Part::Clear, -a * 0.5f - 0.003f, side * 0.38f, 0);
    }
}

void BuildDisc(Built& b, float depth, bool hack = false, bool mini = false, bool dark = false) {
    const float r_hole = mini ? 0.094f : 0.0625f;
    const float r_hub = mini ? 0.23f : 0.165f;
    const Part data_side = dark ? Part::DiscDataDark : Part::DiscData;
    gfx::AddRing(b.solid, r_hub, 0.5f, depth, Part::DiscPrint, data_side);
    gfx::AddRing(b.clear, r_hole, r_hub + 0.003f, depth * 1.05f, Part::DiscHub, Part::DiscHub);
    if (hack) {
        // On the print near the rim, turning with the disc.
        GameObject spot;
        spot.aspect = 1;
        spot.label = {0.3f, 0.06f, 0.4f, 0.4f};
        AddHackSticker(b.solid, spot, depth * 0.5f - 0.007f);
    }
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
    std::uint64_t used = 0; // the frame it was last asked for
};

// Built meshes by look. Trimmed between frames to those asked for lately, so a mesh handed out
// stays valid for the rest of its frame; all are freed while the GL context still exists.
constexpr std::size_t MESH_CACHE_SIZE = 128;
std::map<std::string, Meshes> g_meshes;
Meshes g_disc_meshes[8];
Meshes g_umd;
std::uint64_t g_frame = 0;

void TrimMeshes() {
    ++g_frame;
    while (g_meshes.size() > MESH_CACHE_SIZE) {
        const auto oldest = std::min_element(g_meshes.begin(), g_meshes.end(),
                                             [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
        if (oldest->second.used + 1 >= g_frame) {
            break; // all drawn last frame: keep them
        }
        g_meshes.erase(oldest);
    }
}

DEN_REGISTER_FRAME(TrimMeshes);
DEN_REGISTER_SHUTDOWN([] {
    g_meshes.clear();
    for (Meshes& m : g_disc_meshes) {
        m = {};
    }
    g_umd = {};
});

const Meshes& MeshesFor(const GameObject& o) {
    const std::string key = std::format(
        "{} {} {} {} {} {} {} {} {} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {} {}", o.shape, o.style,
        o.label_edge, o.smoke, o.photo, o.case_box, o.full_inlay, o.mini_disc, o.dark_disc, o.aspect, o.depth,
        o.label.x, o.label.y, o.label.w, o.label.h, o.radius, o.notch, o.grip, o.hack);
    Meshes& meshes = g_meshes[key];
    meshes.used = g_frame;
    if (!meshes.solid) {
        Built b;
        if (o.shape == "carton") {
            BuildCarton(b.solid, o);
        } else if (o.shape == "disc") {
            BuildDisc(b, o.depth, o.hack, o.mini_disc, o.dark_disc);
        } else if (BuildMould(b, o)) {
        } else if (o.shape == "cartridge" && o.style != "plain") {
            BuildPocket(b, o);
        } else if (o.photo) {
            BuildPhoto(b, o);
        } else if (o.shape == "cartridge") {
            BuildCartridge(b, o);
        } else if (o.shape == "card") {
            BuildCard(b.solid, o);
        } else if (o.shape == "jewel") {
            BuildJewel(b, o);
        } else {
            BuildCase(b, o);
        }
        if (o.hack && (o.shape == "cartridge" || o.shape == "card")) {
            AddHackSticker(b.solid, o, o.depth * 0.5f);
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
    const std::optional<gfx::Color> mould_shell = ApplyMould(o, look.style);
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
    o.shell = look.has_shell ? look.shell : mould_shell.value_or(shape->shell);
    o.smoke = o.shell.a < 250 && (o.shape == "cartridge" || o.shape == "card" || o.shape == "case");
    o.shell.a = o.smoke ? std::max<std::uint8_t>(o.shell.a, 60) : 255;
    o.label = look.label.w > 0 ? look.label : o.label;
    o.label_edge = look.label_edge.empty() ? o.label_edge : look.label_edge;
    o.radius = look.radius >= 0 ? look.radius : shape->radius;
    o.notch = look.notch >= 0 ? look.notch : shape->notch;
    o.grip = look.grip >= 0 ? look.grip : o.grip;
    o.disc = look.disc && (o.shape == "case" || o.shape == "jewel");
    o.umd = o.disc && look.umd;
    const bool cart = o.shape == "cartridge" || o.shape == "card";
    o.box = look.box && cart;
    o.box_aspect = look.box_aspect;
    return o;
}

GameObject GameObject::Carton(float cover_aspect) const {
    GameObject o;
    const float raw_aspect = cover_aspect > 0 ? cover_aspect : box_aspect;
    if (style == "nds" || style == "n3ds") {
        o.shape = "case";
        o.case_box = true;
        o.aspect = std::round(std::clamp(raw_aspect, 0.9f, 1.25f) * 100.0f) / 100.0f;
        o.depth = 0.095f;
        o.radius = 0.025f;
        o.shell = style == "n3ds" ? gfx::Color{240, 240, 244, 255} : gfx::Color{40, 42, 48, 255};
        return o;
    }
    if (style == "switch") {
        o.shape = "case";
        o.case_box = true;
        o.aspect = std::round(std::clamp(raw_aspect, 0.55f, 0.72f) * 100.0f) / 100.0f;
        o.depth = 0.068f;
        o.radius = 0.025f;
        o.shell = gfx::Color{200, 40, 50, 255};
        return o;
    }
    if (style == "megadrive" || style == "mastersystem" || style == "32x") {
        o.shape = "case";
        o.case_box = true;
        o.aspect = std::round(std::clamp(raw_aspect, 0.62f, 0.85f) * 100.0f) / 100.0f;
        o.depth = 0.145f;
        o.radius = 0.022f;
        o.shell = gfx::Color{24, 25, 30, 255};
        return o;
    }
    o.shape = "carton";
    o.aspect = std::round(std::clamp(raw_aspect, 0.4f, 2.0f) * 100.0f) / 100.0f;
    if (style == "nes" || style == "atari" || style == "atari5200" || style == "jaguar" ||
        style == "colecovision" || style == "intellivision" || style == "vectrex") {
        o.depth = 0.135f;
    } else if (style == "snes" || style == "n64" || style == "virtualboy") {
        o.depth = 0.23f;
    } else {
        o.depth = 0.18f;
    }
    o.shell = gfx::Color{236, 234, 228, 255};
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
    if (shape == "jewel") {
        if (cover_aspect < 0.80f) {
            // Longbox jewel cases (Saturn / PS1) have tall covers with a left hinge spine.
            const float wanted = cover_aspect + 0.065f;
            o.aspect = std::round(std::clamp(wanted, 0.58f, 1.30f) * 100.0f) / 100.0f;
            o.depth = 0.09f;
            o.full_inlay = false;
        } else {
            o.full_inlay = cover_aspect >= 1.06f;
            const float wanted = o.full_inlay ? cover_aspect + 0.03f : cover_aspect + 0.095f;
            o.aspect = std::round(std::clamp(wanted, 0.58f, 1.30f) * 100.0f) / 100.0f;
        }
        return o;
    }
    o.aspect = std::round(std::clamp(cover_aspect, aspect * 0.82f, aspect * 1.2f) * 100.0f) / 100.0f;
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

namespace {

const Meshes& DiscMeshes(const GameObject& o) {
    const std::size_t idx = (o.hack ? 1u : 0u) | (o.mini_disc ? 2u : 0u) | (o.dark_disc ? 4u : 0u);
    Meshes& meshes = g_disc_meshes[idx];
    if (!meshes.solid) {
        Built b;
        BuildDisc(b, 0.012f, o.hack, o.mini_disc, o.dark_disc);
        meshes.solid = Upload(b.solid);
        meshes.clear = Upload(b.clear);
    }
    return meshes;
}

const Meshes& Umd() {
    if (!g_umd.solid) {
        std::vector<gfx::MeshVertex> solid, clear;
        BuildUmdShell(solid, clear);
        g_umd.solid = Upload(solid);
        g_umd.clear = Upload(clear);
    }
    return g_umd;
}

} // namespace

const gfx::Mesh3D* GameObject::DiscMesh() const {
    return disc ? DiscMeshes(*this).solid.get() : nullptr;
}

const gfx::Mesh3D* GameObject::DiscClearMesh() const {
    return disc ? DiscMeshes(*this).clear.get() : nullptr;
}

const gfx::Mesh3D* GameObject::UmdMesh() const {
    return umd ? Umd().solid.get() : nullptr;
}

const gfx::Mesh3D* GameObject::UmdClearMesh() const {
    return umd ? Umd().clear.get() : nullptr;
}

} // namespace den::app
