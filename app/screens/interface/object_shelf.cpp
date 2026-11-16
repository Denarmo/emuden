// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "object_shelf.h"

#include <algorithm>
#include <cmath>

#include "den/app/boot.h"
#include "den/app/app.h"
#include "den/base/log.h"
#include "den/gfx/canvas.h"
#include "den/app/interfaces.h"
#include "den/app/shelf_parts.h"

namespace den::app {

namespace {

constexpr float HALF_FIELD = 0.26f; // half of Renderer3D's field of view
constexpr float PUSH_BACK = 0.45f;  // how far side objects stand behind, in object heights
// A case's disc, across, in case heights: a tall case holds a smaller disc than a jewel case.
float DiscSize(const GameObject& object) {
    return std::min(0.95f, object.aspect * 0.9f);
}
constexpr float REFLECTION_SHARE = 0.45f; // of the object's height shows under the floor

} // namespace

ObjectShelf::ObjectShelf(std::shared_ptr<const InterfaceSpec> spec) : CoverCarousel(std::move(spec)) {}

const GameObject& ObjectShelf::ObjectFor(const std::string& system) const {
    const auto found = objects_.find(system);
    if (found != objects_.end()) {
        return found->second;
    }
    const auto console = Svc().plugins->FindConsole(system);
    const ConsoleLook look = spec_->LookFor(system, console ? console->group : std::string());
    return objects_[system] = Resolve(look.object);
}

const GameObject& ObjectShelf::ObjectAt(std::size_t index) const {
    const library::Entry& game = games_[index];
    const GameObject& object = ObjectFor(game.system);
    if (const auto found = own_.find(game.id); found != own_.end()) {
        return found->second;
    }
    const LoadedFaces* faces = faces_.Peek(game.id);
    if (faces == nullptr) {
        return object;
    }
    if (std::optional<GameObject> own = OwnObject(object, *faces)) {
        return own_[game.id] = std::move(*own);
    }
    return object;
}

const GameObject* ObjectShelf::BoxAt(std::size_t index) const {
    const library::Entry& game = games_[index];
    const GameObject& object = ObjectFor(game.system);
    if (!object.box) {
        return nullptr;
    }
    if (const auto found = boxes_.find(game.id); found != boxes_.end()) {
        return &found->second;
    }
    if (const LoadedFaces* faces = faces_.Peek(game.id)) {
        return &(boxes_[game.id] = object.Carton(faces->cover_aspect));
    }
    const std::string key = "system:" + game.system;
    if (const auto found = boxes_.find(key); found != boxes_.end()) {
        return &found->second;
    }
    return &(boxes_[key] = object.Carton(0));
}

float ObjectShelf::ObjectHeight() const {
    return std::max(40.0f, std::min(rect.h * spec_->cover_height, rect.w * 0.3f));
}

float ObjectShelf::Floor() const {
    return rect.y + rect.h * 0.06f + spec_->bob + ObjectHeight();
}

ShelfPiece ObjectShelf::CartPiece(const Placed& p) {
    return CartPieceOf(p.height, *p.object, p.box);
}

ShelfPiece ObjectShelf::BoxPiece(const Placed& p) {
    return BoxPieceOf(p.height, *p.object);
}

ObjectShelf::Placed ObjectShelf::Place(std::size_t index, float time) const {
    const GameObject& object = ObjectAt(index);
    const GameObject* box = BoxAt(index);
    const float footprint = Footprint(object, box);
    // A wide object (a landscape cart) is kept to a width that leaves its neighbours room.
    const float h_main = std::min(ObjectHeight(), rect.w * 0.26f / footprint);
    const float offset = static_cast<float>(index) - pos_;
    const float a = std::fabs(offset), side = offset < 0 ? -1.0f : 1.0f;
    const float weight = std::max(0.0f, 1.0f - a);
    const float near_a = std::min(a, 1.0f);
    const float scale = spec_->side_scale + (1 - spec_->side_scale) * weight;
    const float h = h_main * scale;
    // Turned objects take less room across: the row packs them closer than flat covers.
    const float w_main = h_main * footprint, w_side = h_main * spec_->side_scale * 0.8f;
    const float near = w_main * 0.5f + spec_->spacing + w_side * 0.62f;
    const float step = w_side * 0.55f + spec_->spacing * 0.5f;
    const float distance = a <= 1 ? a * near : near + (a - 1) * step;

    Placed p{index, &object, box, rect.CenterX() + side * distance, Floor(), -h_main * PUSH_BACK * near_a,
             h,     0,       1,   weight, h * footprint};
    p.yaw = -side * spec_->side_angle * near_a + weight * spec_->sway * std::sin(time * 0.9f);
    p.light = 1.0f - spec_->side_dim * (1.0f - weight);
    p.y -= spec_->bob * (0.5f + 0.5f * std::sin(time * 2.2f)) * weight;
    if (index == index_ && LaunchAmount() > 0) {
        const float t = LaunchAmount();
        p.yaw *= 1 - t;
        if (object.shape == "cartridge" || object.shape == "card") {
            p.y += t * h * 0.7f; // goes down into the slot
        }
        p.z += t * h_main * 0.9f;
    }
    return p;
}

gfx::Rect ObjectShelf::FocusRect() const {
    if (games_.empty()) {
        return rect;
    }
    const Placed p = Place(index_, static_cast<float>(time_));
    const float distance = view_h_ * 0.5f / std::tan(HALF_FIELD);
    const float k = distance / std::max(1.0f, distance - p.z);
    // A bar on the floor under the object: a frame around it would hide its shape.
    const float w = p.width * k * std::fabs(std::cos(p.yaw));
    const float cx = view_w_ * 0.5f + (p.x - view_w_ * 0.5f) * k;
    const float bottom = view_h_ * 0.5f + (Floor() - view_h_ * 0.5f) * k;
    return gfx::Rect{cx - w * 0.4f, bottom + 14, w * 0.8f, 4};
}

gfx::Rect ObjectShelf::LaunchRect() const {
    if (games_.empty()) {
        return rect;
    }
    // The selected cartridge (or case) as the camera sees it.
    const Placed p = Place(index_, static_cast<float>(time_));
    const ShelfPiece piece = CartPiece(p);
    const float distance = view_h_ * 0.5f / std::tan(HALF_FIELD);
    const float k = distance / std::max(1.0f, distance - (p.z + piece.dz));
    const float cx = view_w_ * 0.5f + (p.x + piece.dx - view_w_ * 0.5f) * k;
    const float bottom = view_h_ * 0.5f + (p.y - view_h_ * 0.5f) * k;
    const float h = piece.height * k, w = h * p.object->aspect;
    return gfx::Rect{cx - w * 0.5f, bottom - h, w, h};
}

void ObjectShelf::OnTick(ui::Context& ctx) {
    CoverCarousel::OnTick(ctx);
    const bool still = std::fabs(pos_ - static_cast<float>(index_)) < 0.05f && !Launching();
    const bool disc = !games_.empty() && ObjectFor(games_[index_].system).disc;
    const float target = still && disc ? 1.0f : 0.0f;
    slide_ = ui::Approach(slide_, target, ctx.dt, target > slide_ ? 4.0f : 12.0f);
}

gfx::Material ObjectShelf::MaterialFor(ui::Context& ctx, const Placed& p, bool box) const {
    const library::Entry& game = games_[p.index];
    gfx::Material m = ShelfMaterial(ctx, game, box ? *p.box : *p.object, faces_.Peek(game.id), box, p.box != nullptr);
    m.light = p.light;
    return m;
}

void ObjectShelf::DrawContent(ui::Context& ctx) {
    if (games_.empty()) {
        return;
    }
    gfx::Renderer3D* renderer = ShelfRenderer();
    if (renderer == nullptr) {
        CoverCarousel::DrawContent(ctx);
        return;
    }
    gfx::Canvas& canvas = *ctx.canvas;
    view_w_ = canvas.Width();
    view_h_ = canvas.Height();
    const float time = static_cast<float>(time_);
    // The faces of the selected game first, then of its neighbours.
    for (int d = 0; d <= 3; ++d) {
        for (const int i : {static_cast<int>(index_) - d, static_cast<int>(index_) + d}) {
            if (i >= 0 && i < static_cast<int>(games_.size()) && (d > 0 || i == static_cast<int>(index_))) {
                faces_.Get(games_[static_cast<std::size_t>(i)].id);
            }
        }
    }

    std::vector<Placed> placed;
    const float reach = 7;
    const std::size_t first = static_cast<std::size_t>(std::max(0.0f, std::floor(pos_ - reach)));
    const std::size_t last = std::min(games_.size() - 1, static_cast<std::size_t>(std::ceil(pos_ + reach)));
    for (std::size_t i = first; i <= last; ++i) {
        placed.push_back(Place(i, time));
    }
    // Far first: there is no depth buffer, the nearer object is drawn over the farther.
    std::sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return a.z < b.z; });
    // The game being started is the boot screen's to draw.
    std::erase_if(placed, [&](const Placed& p) { return BootCarries(games_[p.index].id); });

    const auto model = [&](const Placed& p, const ShelfPiece& piece) {
        return gfx::Renderer3D::Model(p.x, p.y, p.z, p.yaw) * gfx::Mat4::Translate(piece.dx, piece.height * 0.5f, piece.dz) *
               gfx::Mat4::Scale(piece.height, piece.height, piece.height);
    };
    // Everything a game is drawn with, in its place: its box, the disc out of its case, then
    // the object itself.
    const auto draw = [&](const Placed& p, const gfx::Mat4& place, bool mirrored, bool clear) {
        const gfx::Mat4 m = place * model(p, CartPiece(p));
        const gfx::Material look = MaterialFor(ctx, p, false);
        // The selected case's disc, out to its side: a UMD's shell stays still round it.
        const gfx::Mesh3D* disc = p.index == index_ && slide_ > 0.01f ? p.object->DiscMesh() : nullptr;
        gfx::Mat4 at;
        gfx::Material d;
        if (disc != nullptr) {
            const float size = DiscSize(*p.object) * (p.object->umd ? 0.9f : 1.0f);
            const float start = std::max(0.0f, size * 0.5f - p.object->aspect * 0.5f);
            const float x = start + slide_ * p.object->aspect * 0.62f;
            at = m * gfx::Mat4::Translate(x, 0, -(p.object->depth * 0.5f + 0.03f)) * gfx::Mat4::Scale(size, size, size);
            d = DiscMaterial(ctx, games_[p.index], *p.object, faces_.Peek(games_[p.index].id));
            d.light = p.light;
        }
        if (clear) {
            if (const gfx::Mesh3D* lid = p.object->ClearMesh()) {
                renderer->DrawClear(*lid, m, look, mirrored);
            }
            if (const gfx::Mesh3D* window = disc ? p.object->UmdClearMesh() : nullptr) {
                renderer->DrawClear(*window, at, d, mirrored);
            }
            return;
        }
        if (p.box != nullptr) {
            renderer->Draw(p.box->Mesh(), place * model(p, BoxPiece(p)), MaterialFor(ctx, p, true), mirrored);
        }
        if (disc != nullptr) {
            if (const gfx::Mesh3D* shell = p.object->UmdMesh()) {
                renderer->Draw(*shell, at, d, mirrored);
            }
            renderer->Draw(*disc, at * gfx::Mat4::RotateZ(-time * 1.6f * slide_), d, mirrored);
        }
        const float spin = p.object->shape == "disc" ? -time * (0.6f + 4.0f * LaunchAmount()) * p.weight : 0;
        renderer->Draw(p.object->Mesh(), spin != 0 ? m * gfx::Mat4::RotateZ(spin) : m, look, mirrored);
    };

    const float floor = Floor();
    // Soft contact shadows: stacked, each wider and fainter.
    for (const Placed& p : placed) {
        for (int i = 0; i < 4; ++i) {
            const float spread = p.width * (0.8f + 0.12f * i), tall = 6.0f + 5.0f * i;
            canvas.FillRect(gfx::Rect{p.x - spread * 0.5f, floor - tall * 0.5f, spread, tall},
                            gfx::Color{0, 0, 0, 255}.WithAlpha(0.13f * p.light), tall * 0.5f);
        }
    }
    const gfx::Mat4 none;
    if (spec_->reflection > 0) {
        const float depth = ObjectHeight() * REFLECTION_SHARE;
        const gfx::Mat4 mirror = gfx::Mat4::Translate(0, 2 * floor, 0) * gfx::Mat4::Scale(1, -1, 1);
        renderer->Begin(canvas, gfx::Rect{0, floor, view_w_, depth});
        for (const Placed& p : placed) {
            draw(p, mirror, true, false);
        }
        renderer->End(canvas, spec_->reflection, 0);
    }
    renderer->Begin(canvas, gfx::Rect{0, rect.y, view_w_, floor - rect.y + 2});
    for (const Placed& p : placed) {
        draw(p, none, false, false);
    }
    for (const Placed& p : placed) {
        draw(p, none, false, true);
    }
    renderer->End(canvas);
}

} // namespace den::app
