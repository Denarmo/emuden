// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Shelf's object of one game outside its row: its cartridge before its box, its case or its
// disc, lit and slightly turned, as the "shelf3d" layout draws the selected game.

#include <algorithm>

#include "den/app/app.h"
#include "den/gfx/canvas.h"
#include "den/app/interfaces.h"
#include "den/app/shelf_parts.h"

namespace den::app {

namespace {

constexpr float TURN = -0.38f; // shows the side and some depth

class ShelfPreview final : public ui::Node {
public:
    ShelfPreview(const InterfaceSpec& spec, library::Entry game, float w, float h) : game_(std::move(game)) {
        width = w;
        height = h;
        const auto console = Svc().plugins->FindConsole(game_.system);
        look_ = Resolve(spec.LookFor(game_.system, console ? console->group : std::string()).object);
        look_.mini_disc = game_.system == "gamecube";
        look_.dark_disc = game_.system == "psx";
        object_ = look_;
        if (look_.box) {
            box_ = look_.Carton(0);
        }
    }

protected:
    void DrawContent(ui::Context& ctx) override {
        gfx::Renderer3D* renderer = ShelfRenderer();
        if (renderer == nullptr) {
            return;
        }
        const LoadedFaces* faces = faces_.Get(game_.id);
        if (faces != nullptr && !shaped_) {
            shaped_ = true;
            if (std::optional<GameObject> own = OwnObject(look_, *faces)) {
                object_ = std::move(*own);
            }
            if (look_.box) {
                box_ = look_.Carton(faces->cover_aspect);
            }
        }
        const GameObject* box = box_ ? &*box_ : nullptr;
        const float footprint = Footprint(object_, box);
        const float h = std::min(rect.h * 0.84f, rect.w * 0.92f / std::max(0.1f, footprint));
        const float x = rect.CenterX(), floor = rect.Bottom() - rect.h * 0.06f;

        gfx::Canvas& canvas = *ctx.canvas;
        for (int i = 0; i < 4; ++i) {
            const float spread = h * footprint * (0.8f + 0.12f * i), tall = 6.0f + 5.0f * i;
            canvas.FillRect(gfx::Rect{x - spread * 0.5f, floor - tall * 0.5f, spread, tall},
                            gfx::Color{0, 0, 0, 255}.WithAlpha(0.08f), tall * 0.5f);
        }
        const auto model = [&](const ShelfPiece& piece) {
            return gfx::Renderer3D::Model(x, floor, 0, TURN) * gfx::Mat4::Translate(piece.dx, piece.height * 0.5f, piece.dz) *
                   gfx::Mat4::Scale(piece.height, piece.height, piece.height);
        };
        const gfx::Mat4 cart = model(CartPieceOf(h, object_, box));
        const gfx::Material look = ShelfMaterial(ctx, game_, object_, faces, false, box != nullptr);
        const gfx::Mat4 box_model = box != nullptr ? model(BoxPieceOf(h, object_)) : gfx::Mat4{};
        const gfx::Material box_look =
            box != nullptr ? ShelfMaterial(ctx, game_, *box, faces, true, true) : gfx::Material{};
        renderer->Begin(canvas, gfx::Rect{rect.x - rect.w * 0.1f, rect.y, rect.w * 1.2f, rect.h});
        if (box != nullptr) {
            renderer->Draw(box->Mesh(), box_model, box_look);
        }
        renderer->Draw(object_.Mesh(), cart, look);
        if (box != nullptr) {
            if (const gfx::Mesh3D* box_clear = box->ClearMesh()) {
                renderer->DrawClear(*box_clear, box_model, box_look);
            }
        }
        if (const gfx::Mesh3D* lid = object_.ClearMesh()) {
            renderer->DrawClear(*lid, cart, look);
        }
        renderer->End(canvas);
    }

private:
    library::Entry game_;
    GameObject look_, object_;
    std::optional<GameObject> box_;
    bool shaped_ = false;
    FaceLoader faces_;
};

} // namespace

std::unique_ptr<ui::Node> MakeShelfPreview(const library::Entry& game, float width, float height) {
    const std::shared_ptr<const InterfaceSpec> spec = ChosenInterface();
    if (spec == nullptr || spec->layout != "shelf3d" || ShelfRenderer() == nullptr) {
        return nullptr;
    }
    return std::make_unique<ShelfPreview>(*spec, game, width, height);
}

} // namespace den::app
