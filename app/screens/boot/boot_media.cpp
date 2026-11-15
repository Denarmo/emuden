// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <string_view>

#include "../interface/interfaces.h"
#include "../interface/shelf_parts.h"
#include "boot_media.h"
#include "den/app/app.h"
#include "den/gfx/canvas.h"

namespace den::app {

namespace {

// The usual media of the consoles, for the standard Home: shape, mould, width over height.
struct Usual {
    std::string_view system, shape, style;
    float aspect;
    bool disc;
};
constexpr Usual USUAL[] = {
    {"gb", "cartridge", "pocket", 0, false},       {"gbc", "cartridge", "pocket", 0, false},
    {"gba", "cartridge", "pocket_wide", 0, false}, {"nds", "card", "", 1.05f, false},
    {"3ds", "card", "", 1.05f, false},             {"nes", "cartridge", "", 0.89f, false},
    {"snes", "cartridge", "", 1.35f, false},       {"n64", "cartridge", "", 1.5f, false},
    {"switch", "card", "", 0.68f, false},          {"switch2", "card", "", 0.68f, false},
    {"genesis", "cartridge", "", 0.84f, false},    {"sms", "cartridge", "", 0.74f, false},
    {"gg", "cartridge", "", 1.25f, false},         {"32x", "cartridge", "", 0.84f, false},
    {"sg1000", "cartridge", "", 0.74f, false},     {"pce", "card", "", 0.64f, false},
    {"lynx", "card", "", 1.4f, false},             {"ngp", "cartridge", "", 0.95f, false},
    {"ngpc", "cartridge", "", 0.95f, false},       {"wonderswan", "cartridge", "", 1.35f, false},
    {"wsc", "cartridge", "", 1.35f, false},        {"virtualboy", "cartridge", "", 0.86f, false},
    {"gamecube", "case", "", 0.72f, true},         {"wii", "case", "", 0.7f, true},
    {"wiiu", "case", "", 0.7f, true},              {"psx", "jewel", "", 0, true},
    {"ps2", "case", "", 0.7f, true},               {"psp", "case", "", 0.58f, true},
    {"saturn", "jewel", "", 0, true},              {"dreamcast", "jewel", "", 0, true},
    {"segacd", "jewel", "", 0, true},              {"3do", "jewel", "", 0, true},
};

} // namespace

GameObject MediaFor(const library::Entry& game) {
    ObjectLook look;
    look.shape = "cartridge";
    const auto console = Svc().plugins->FindConsole(game.system);
    if (const std::shared_ptr<const InterfaceSpec> spec = ChosenInterface(); spec && spec->layout == "shelf3d") {
        look = spec->LookFor(game.system, console ? console->group : std::string()).object;
    } else {
        for (const Usual& usual : USUAL) {
            if (usual.system == game.system) {
                look.shape = std::string(usual.shape);
                look.style = std::string(usual.style);
                look.aspect = usual.aspect;
                look.disc = usual.disc;
            }
        }
    }
    look.box = false;
    return Resolve(look);
}

float MediaAspect(const GameObject& media) {
    return media.DiscMesh() != nullptr || media.shape == "disc" ? 1.0f : media.aspect;
}

void DrawMedia(ui::Context& ctx, const library::Entry& game, const GameObject& media, const LoadedFaces* faces,
               const MediaPose& pose) {
    gfx::Canvas& canvas = *ctx.canvas;
    const float h = pose.height;
    const gfx::Rect clip{0, 0, canvas.Width(), std::min(canvas.Height(), pose.cut)};
    if (clip.h <= 0 || pose.alpha <= 0.01f) {
        return;
    }
    gfx::Renderer3D* renderer = ShelfRenderer();
    gfx::Material look = ShelfMaterial(ctx, game, media, faces, false, false);
    const gfx::Mesh3D* disc = media.DiscMesh();
    if (renderer == nullptr) {
        // Flat: the cover on a rounded card.
        const float w = h * MediaAspect(media);
        const gfx::Rect r{pose.x - w * 0.5f, pose.bottom - h, w, h};
        canvas.PushClip(clip);
        canvas.FillRect(r, media.shell.WithAlpha(pose.alpha), w * 0.06f);
        if (look.cover) {
            const gfx::Rect in{r.x + w * 0.08f, r.y + h * 0.12f, w * 0.84f, h * 0.7f};
            canvas.DrawImage(in, look.cover, gfx::Color{255, 255, 255, 255}.WithAlpha(pose.alpha), 4,
                             gfx::CoverUv(look.cover.width, look.cover.height, in));
        }
        canvas.PopClip();
        return;
    }
    renderer->Begin(canvas, clip);
    if (disc != nullptr) {
        if (look.cover) {
            look.label_uv = gfx::CoverUv(look.cover.width, look.cover.height, gfx::Rect{0, 0, 100, 100});
        }
        const gfx::Mat4 model = gfx::Renderer3D::Model(pose.x, pose.bottom - h * 0.5f, 0, pose.yaw) *
                                gfx::Mat4::RotateZ(pose.spin) * gfx::Mat4::Scale(h, h, 1);
        renderer->Draw(*disc, model, look);
    } else {
        const float spin = media.shape == "disc" ? pose.spin : 0;
        const gfx::Mat4 model = gfx::Renderer3D::Model(pose.x, pose.bottom, 0, pose.yaw) *
                                gfx::Mat4::Translate(0, h * 0.5f, 0) * gfx::Mat4::RotateZ(spin) *
                                gfx::Mat4::Scale(h, h, h);
        renderer->Draw(media.Mesh(), model, look);
        if (const gfx::Mesh3D* clear = media.ClearMesh()) {
            renderer->DrawClear(*clear, model, look);
        }
    }
    renderer->End(canvas, pose.alpha, pose.alpha);
}

} // namespace den::app
