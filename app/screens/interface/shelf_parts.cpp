// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shelf_parts.h"

#include <algorithm>

#include "den/app/app.h"
#include "den/base/log.h"
#include "den/gfx/device.h"

namespace den::app {

namespace {

// Where a hack's sticker goes on a photographed cartridge: the label found in the photo when
// it agrees with where the console's labels are, else that place (a label coloured like its
// plastic is not found whole).
gfx::Rect StickerRect(const gfx::Rect& found, const gfx::Rect& mould) {
    const float ix = std::max(0.0f, std::min(found.Right(), mould.Right()) - std::max(found.x, mould.x));
    const float iy = std::max(0.0f, std::min(found.Bottom(), mould.Bottom()) - std::max(found.y, mould.y));
    const float common = ix * iy, area = mould.w * mould.h;
    return found.w > 0 && common > area * 0.8f && found.w * found.h < area * 1.25f ? found : mould;
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
    if (cart && faces.cart_aspect > 0) {
        return look.WithPhoto(faces.plastic, faces.cart_aspect);
    }
    if ((look.shape == "case" || look.shape == "jewel") && faces.cover_aspect > 0) {
        return look.WithCover(faces.cover_aspect);
    }
    return std::nullopt;
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
    const std::string& cover = faces != nullptr && !faces->cover.empty() ? faces->cover : game.cover;
    m.cover = cover.empty() ? gfx::Texture{} : ctx.textures->Get(cover, 1024);
    const bool full = box || object.shape == "case" || object.shape == "jewel";
    if (m.cover) {
        const float aspect = full ? object.aspect : object.LabelAspect();
        m.label_uv = gfx::CoverUv(m.cover.width, m.cover.height, gfx::Rect{0, 0, aspect * 100, 100});
        m.spine_uv = gfx::Rect{m.label_uv.x, m.label_uv.y, m.label_uv.w * 0.07f, m.label_uv.h};
    }
    const auto art = [&](const std::string& path, int size) {
        return path.empty() ? gfx::Texture{} : ctx.textures->Get(path, size);
    };
    const bool hack = !game.hack_of.empty() || (faces != nullptr && faces->faces.base_game);
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
            // A hack on the cartridge of the game it was made from: its own cover over the
            // label (where the photo shows one, else where the mould has it).
            if (faces->faces.base_game && m.cover) {
                const gfx::Rect place = StickerRect(faces->cart_label, object.label);
                m.sticker = place;
                const float aspect = place.w * object.aspect / std::max(0.01f, place.h);
                m.label_uv = gfx::CoverUv(m.cover.width, m.cover.height, gfx::Rect{0, 0, aspect * 100, 100});
            }
        } else if (!beside_box) {
            m.back_art = art(faces->faces.back, 1024);
            m.spine_art = art(faces->faces.spine, 512);
        }
        m.disc_art = art(faces->faces.disc, 512);
    }
    return m;
}

} // namespace den::app
