// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shelf_parts.h"

#include <algorithm>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/gfx/device.h"

namespace den::app {

namespace {

bool IsHack(const library::Entry& game, const LoadedFaces* faces) {
    return !game.hack_of.empty() || (faces != nullptr && faces->hack);
}

gfx::Texture HackSticker(ui::Context& ctx) {
    static const std::string path = PathToUtf8(pal::AssetsDir() / "branding" / "hack-sticker.png");
    return ctx.textures->Get(path, 256);
}

// The picture on a cartridge's label or a disc: a hack's is the original game's cover.
std::string LabelCover(const library::Entry& game, const LoadedFaces* faces, bool own) {
    if (!own && faces != nullptr && !faces->base_cover.empty()) {
        return faces->base_cover;
    }
    return faces != nullptr && !faces->cover.empty() ? faces->cover : game.cover;
}

} // namespace

gfx::Renderer3D* ShelfRenderer() {
    static gfx::Renderer3D renderer;
    static int state = 0; // 0 not tried, 1 ready, -1 failed
    if (state == 0) {
        const Status status = renderer.Init(*Svc().device);
        state = status.ok ? 1 : -1;
        if (!status.ok) {
            log::Warn("interface", "3D layout drawn flat: {}", status.error);
        }
    }
    return state > 0 ? &renderer : nullptr;
}

// The front's width over its height of a game's objects together.
float Footprint(const GameObject& object, const GameObject* box) {
    return box == nullptr ? object.aspect : box->aspect + 0.23f * CART_SHARE * object.aspect;
}

std::optional<GameObject> OwnObject(const GameObject& look, const LoadedFaces& faces) {
    const bool cart = look.shape == "cartridge" || look.shape == "card";
    std::optional<GameObject> own;
    // A photograph far from the mould's proportions shows another side (an end label).
    const float ratio = faces.cart_aspect / std::max(0.01f, look.aspect);
    if (cart && faces.cart_aspect > 0 && ratio > 0.6f && ratio < 1.6f) {
        own = look.WithPhoto(faces.plastic, faces.cart_aspect);
    } else if ((look.shape == "case" || look.shape == "jewel") && faces.cover_aspect > 0) {
        own = look.WithCover(faces.cover_aspect);
    }
    if (faces.hack) {
        own = own.value_or(look);
        own->hack = true;
    }
    return own;
}

ShelfPiece CartPieceOf(float height, const GameObject& object, const GameObject* box) {
    if (box == nullptr) {
        return {0, 0, height};
    }
    const float h = height * CART_SHARE;
    return {height * box->aspect * 0.5f - h * object.aspect * 0.45f, height * 0.14f, h};
}

ShelfPiece BoxPieceOf(float height, const GameObject& object) {
    return {-height * CART_SHARE * object.aspect * 0.18f, -height * 0.05f, height};
}

gfx::Material ShelfMaterial(ui::Context& ctx, const library::Entry& game, const GameObject& object,
                            const LoadedFaces* faces, bool box, bool beside_box) {
    gfx::Material m;
    m.shell = object.shell;
    const bool full = box || object.shape == "case" || object.shape == "jewel";
    // The box and the case show the game's own cover; a cartridge the original game's.
    const std::string cover = LabelCover(game, faces, full);
    m.cover = cover.empty() ? gfx::Texture{} : ctx.textures->Get(cover, 1024);
    if (m.cover) {
        const float aspect = full ? object.aspect : object.LabelAspect();
        m.label_uv = gfx::CoverUv(m.cover.width, m.cover.height, gfx::Rect{0, 0, aspect * 100, 100});
        m.spine_uv = gfx::Rect{m.label_uv.x, m.label_uv.y, m.label_uv.w * 0.07f, m.label_uv.h};
    }
    const auto art = [&](const std::string& path, int size) {
        return path.empty() ? gfx::Texture{} : ctx.textures->Get(path, size);
    };
    const bool hack = IsHack(game, faces);
    if (hack) {
        m.sticker_art = HackSticker(ctx);
    }
    if (box) {
        // The box carries the game's own cover: a hack's on foil, so its art reads as printed.
        m.foil = hack;
        if (faces != nullptr) {
            m.shell = faces->cover_color;
            m.back_art = art(faces->faces.back, 1024);
            m.spine_art = art(faces->faces.spine, 512);
        }
    } else if (faces != nullptr) {
        if (object.photo) {
            m.cart_art = art(faces->faces.cart, 1024);
            m.back_art = art(faces->faces.cart_back, 512);
        } else if (!beside_box) {
            m.back_art = art(faces->faces.back, 1024);
            m.spine_art = art(faces->faces.spine, 512);
        }
        m.disc_art = art(faces->faces.disc, 512);
    }
    return m;
}

gfx::Material DiscMaterial(ui::Context& ctx, const library::Entry& game, const GameObject& object,
                           const LoadedFaces* faces) {
    gfx::Material m;
    const std::string cover = LabelCover(game, faces, false);
    m.cover = cover.empty() ? gfx::Texture{} : ctx.textures->Get(cover, 1024);
    if (m.cover) {
        m.label_uv = gfx::CoverUv(m.cover.width, m.cover.height, gfx::Rect{0, 0, 100, 100});
    }
    // A UMD's picture shows its shell as well: its disc takes the cover.
    if (faces != nullptr && !faces->faces.disc.empty() && !object.umd) {
        m.disc_art = ctx.textures->Get(faces->faces.disc, 512);
    }
    if (IsHack(game, faces)) {
        m.sticker_art = HackSticker(ctx);
    }
    m.shell = gfx::Color{226, 227, 232, 255}; // a UMD's shell
    return m;
}

} // namespace den::app
