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

// The usual media of the consoles whose manifest has no presentation.media, for the standard
// Home: shape and mould (game_moulds.cpp).
struct Usual {
    std::string_view system, shape, style;
    float aspect;
    bool disc, umd;
};
constexpr Usual USUAL[] = {
    {"gb", "cartridge", "pocket", 0, false, false},       {"gbc", "cartridge", "pocket", 0, false, false},
    {"gba", "cartridge", "pocket_wide", 0, false, false}, {"nds", "card", "", 1.05f, false, false},
    {"3ds", "card", "", 1.05f, false, false},             {"nes", "cartridge", "nes", 0, false, false},
    {"snes", "cartridge", "snes", 0, false, false},       {"n64", "cartridge", "n64", 0, false, false},
    {"switch", "card", "", 0.68f, false, false},          {"switch2", "card", "", 0.68f, false, false},
    {"genesis", "cartridge", "megadrive", 0, false, false}, {"sms", "cartridge", "mastersystem", 0, false, false},
    {"gg", "cartridge", "gamegear", 0, false, false},     {"32x", "cartridge", "32x", 0, false, false},
    {"sg1000", "card", "mycard", 0, false, false},        {"pce", "card", "hucard", 0, false, false},
    {"lynx", "card", "lynx", 0, false, false},            {"ngp", "cartridge", "neogeopocket", 0, false, false},
    {"ngpc", "cartridge", "neogeopocket", 0, false, false}, {"wonderswan", "cartridge", "wonderswan", 0, false, false},
    {"wsc", "cartridge", "wonderswan", 0, false, false},  {"virtualboy", "cartridge", "virtualboy", 0, false, false},
    {"pokemini", "cartridge", "pokemini", 0, false, false}, {"atari2600", "cartridge", "atari", 0, false, false},
    {"atari7800", "cartridge", "atari", 0, false, false}, {"atari5200", "cartridge", "atari5200", 0, false, false},
    {"jaguar", "cartridge", "jaguar", 0, false, false},   {"vectrex", "cartridge", "vectrex", 0, false, false},
    {"colecovision", "cartridge", "colecovision", 0, false, false},
    {"intellivision", "cartridge", "intellivision", 0, false, false},
    {"gamecube", "case", "", 0.72f, true, false},         {"wii", "case", "", 0.7f, true, false},
    {"wiiu", "case", "", 0.7f, true, false},              {"psx", "jewel", "", 0, true, false},
    {"ps2", "case", "", 0.7f, true, false},               {"psp", "case", "", 0.58f, true, true},
    {"saturn", "jewel", "", 0, true, false},              {"dreamcast", "jewel", "", 0, true, false},
    {"segacd", "jewel", "", 0, true, false},              {"3do", "jewel", "", 0, true, false},
};

} // namespace

GameObject MediaFor(const library::Entry& game) {
    ObjectLook look;
    look.shape = "cartridge";
    const auto console = Svc().plugins->FindConsole(game.system);
    if (const std::shared_ptr<const InterfaceSpec> spec = ChosenInterface(); spec && spec->layout == "shelf3d") {
        look = spec->LookFor(game.system, console ? console->group : std::string()).object;
    } else if (console && !console->media.shape.empty()) {
        const plugins::MediaShape& media = console->media;
        look.shape = media.shape;
        look.style = media.style;
        look.aspect = media.aspect;
        look.disc = media.disc;
        look.umd = media.umd;
    } else {
        for (const Usual& usual : USUAL) {
            if (usual.system == game.system) {
                look.shape = std::string(usual.shape);
                look.style = std::string(usual.style);
                look.aspect = usual.aspect;
                look.disc = usual.disc;
                look.umd = usual.umd;
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
        // A UMD's shell stays still; only its disc turns.
        const gfx::Material d = DiscMaterial(ctx, game, media, faces);
        const float size = media.umd ? h / 1.12f : h;
        const gfx::Mat4 at = gfx::Renderer3D::Model(pose.x, pose.bottom - h * 0.5f, 0, pose.yaw) *
                             gfx::Mat4::Scale(size, size, size);
        if (const gfx::Mesh3D* shell = media.UmdMesh()) {
            renderer->Draw(*shell, at, d);
        }
        renderer->Draw(*disc, at * gfx::Mat4::RotateZ(pose.spin), d);
        if (const gfx::Mesh3D* window = media.UmdClearMesh()) {
            renderer->DrawClear(*window, at, d);
        }
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
