// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "shelf3d" layout's launch into a console: the other games and the box go, a console
// rises, shown from behind; the cartridge goes up over its slot and drops in; the console
// turns round, its light comes on and its screen lights up while the game's cover slides
// down onto it; then the console's colour covers everything and the game starts.

#include <algorithm>
#include <cmath>
#include <random>

#include "console_models.h"
#include "den/app/app.h"
#include "den/gfx/canvas.h"
#include "interfaces.h"
#include "object_shelf.h"

namespace den::app {

namespace {

constexpr float LAUNCH_SECONDS = 3.4f;
constexpr float PI = 3.14159265f;

// 0 before `a`, 1 after `b`, eased between.
float Ease(float t, float a, float b) {
    const float x = std::clamp((t - a) / (b - a), 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace

float ObjectShelf::LaunchSeconds() const {
    console_launch_ = false;
    if (games_.empty() || !Svc().settings->GetBool(LAUNCH_ANIMATION_KEY, true) ||
        ConsoleFor(ObjectAt(index_).player).mesh == nullptr) {
        return spec_->launch_seconds;
    }
    console_launch_ = true;
    static std::mt19937 random{std::random_device{}()};
    launch_pick_ = static_cast<std::size_t>(random());
    return LAUNCH_SECONDS;
}

float ObjectShelf::LaunchCover() const {
    if (!console_launch_ || !Launching()) {
        return LaunchAmount();
    }
    return Ease(launch_, 0.88f, 1.0f);
}

void ObjectShelf::DrawLaunch(ui::Context& ctx, gfx::Renderer3D& renderer, const std::vector<Placed>& placed) {
    const auto selected = std::find_if(placed.begin(), placed.end(), [&](const Placed& p) { return p.index == index_; });
    if (selected == placed.end()) {
        return;
    }
    const Placed& sel = *selected;
    const library::Entry& game = games_[sel.index];
    const LoadedFaces* faces = faces_.Peek(game.id);
    // The console from its photograph when there is one, in one of its colours.
    ConsoleModel console = ConsoleFor(sel.object->player);
    const ConsolePhoto* photo = faces != nullptr && !faces->console.images.empty() ? &faces->console : nullptr;
    if (photo != nullptr) {
        const ConsoleModel shaped = PhotoConsole(*photo, sel.object->player, sel.object->player_screen);
        console = shaped.mesh != nullptr ? shaped : console;
        photo = shaped.mesh != nullptr ? photo : nullptr;
    }
    if (console.mesh == nullptr) {
        return;
    }
    gfx::Canvas& canvas = *ctx.canvas;
    const float t = launch_;
    const float floor = Floor();

    // The console: the cartridge held over it must fit above the floor, and it is no wider
    // than half the screen; it rises from under the floor and, with its slot at the back,
    // turns round once the cartridge is in.
    const float hover = console.slot_y + console.cart_height + 0.2f;
    const float hc = std::min((floor - rect.y) * 0.94f / (hover + console.bottom), rect.w * 0.55f / console.aspect);
    const float rise = Ease(t, 0.04f, 0.26f);
    const float cx = rect.CenterX(), cz = 0;
    const float cy = floor - hc * console.bottom + (1 - rise) * hc * 1.4f;
    const float turn = console.turns ? PI * (1 - Ease(t, 0.44f, 0.64f)) : 0;

    // A point of the console (in its heights, y up) on the screen.
    const auto attached = [&](float ly, float lz, float& x, float& y, float& z) {
        x = cx + std::sin(turn) * lz * hc;
        z = cz + std::cos(turn) * lz * hc;
        y = cy - ly * hc;
    };

    const auto piece_model = [&](const Placed& p, const Piece& piece) {
        return gfx::Renderer3D::Model(p.x, p.y, p.z, p.yaw) * gfx::Mat4::Translate(piece.dx, piece.height * 0.5f, piece.dz) *
               gfx::Mat4::Scale(piece.height, piece.height, piece.height);
    };

    // The console's shadow grows as it comes up.
    const float shadow = hc * console.aspect * rise;
    for (int i = 0; i < 4; ++i) {
        const float spread = shadow * (0.8f + 0.12f * i), tall = 6.0f + 5.0f * i;
        canvas.FillRect(gfx::Rect{cx - spread * 0.5f, floor - tall * 0.5f, spread, tall},
                        gfx::Color{0, 0, 0, 255}.WithAlpha(0.13f * rise), tall * 0.5f);
    }

    renderer.Begin(canvas, gfx::Rect{0, rect.y, view_w_, floor - rect.y + 2});
    // The other games slide away to the sides; the box goes back behind the cartridge.
    const float fade = 1 - Ease(t, 0.0f, 0.18f);
    for (const Placed& p : placed) {
        if (p.index == index_ || fade <= 0.01f) {
            continue;
        }
        Placed q = p;
        q.light *= fade;
        q.x += (p.x < sel.x ? -1.0f : 1.0f) * (1 - fade) * rect.w * 0.6f;
        if (q.box != nullptr) {
            renderer.Draw(q.box->Mesh(), piece_model(q, BoxPiece(q)), MaterialFor(ctx, q, true));
        }
        renderer.Draw(q.object->Mesh(), piece_model(q, CartPiece(q)), MaterialFor(ctx, q, false));
    }
    if (sel.box != nullptr && fade > 0.01f) {
        Placed q = sel;
        q.light *= fade;
        q.z -= (1 - fade) * sel.height * 3.0f;
        renderer.Draw(q.box->Mesh(), piece_model(q, BoxPiece(q)), MaterialFor(ctx, q, true));
    }

    // The console, its light and screen coming on once it faces the front.
    gfx::Material body;
    if (photo != nullptr) {
        const std::size_t pick = launch_pick_ % photo->images.size();
        body.shell = photo->colors[pick];
        body.cart_art = ctx.textures->Get(photo->images[pick], 1024);
    } else {
        const std::vector<gfx::Color>& others = sel.object->player_colors;
        const std::size_t pick = launch_pick_ % (others.size() + 1);
        body.shell = pick == 0 ? sel.object->player_color : others[pick - 1];
    }
    const std::string& cover = faces != nullptr && !faces->cover.empty() ? faces->cover : game.cover;
    body.cover = cover.empty() ? gfx::Texture{} : ctx.textures->Get(cover, 1024);
    if (body.cover) {
        body.label_uv = gfx::CoverUv(body.cover.width, body.cover.height, gfx::Rect{0, 0, 110, 100});
    }
    body.boot = t < 0.64f ? -1.0f : t < 0.7f ? (t - 0.64f) / 0.06f : 1 + Ease(t, 0.7f, 0.84f);
    renderer.Draw(*console.mesh, gfx::Renderer3D::Model(cx, cy, cz, turn) * gfx::Mat4::Scale(hc, hc, hc), body);

    // The cartridge: up from its place over the slot, then down into it.
    const Piece piece = CartPiece(sel);
    const float lift = Ease(t, 0.02f, 0.26f);
    const float fall = std::clamp((t - 0.28f) / 0.14f, 0.0f, 1.0f);
    const float above = hover - console.cart_height * 0.5f; // the cartridge's middle, held over the slot
    const float in = console.slot_y + console.cart_height * (0.5f - console.sink);
    float ax = 0, ay = 0, az = 0;
    attached(Lerp(above, in, fall * fall), console.slot_z, ax, ay, az);
    // Its label faces the console's back when the slot is there, its front otherwise.
    const float facing = console.turns ? turn + PI : turn;
    const float home_yaw = std::remainder(facing, 2 * PI);
    const float x = Lerp(sel.x + piece.dx, ax, lift);
    const float y = Lerp(sel.y - piece.height * 0.5f, ay, lift);
    const float z = Lerp(sel.z + piece.dz, az, lift);
    const float yaw = lift < 1 ? Lerp(sel.yaw, home_yaw, lift) : facing;
    const float h = Lerp(piece.height, hc * console.cart_height, lift);
    Placed lit = sel;
    lit.light = 1;
    renderer.Draw(sel.object->Mesh(), gfx::Renderer3D::Model(x, y, z, yaw) * gfx::Mat4::Scale(h, h, h),
                  MaterialFor(ctx, lit, false));
    if (const gfx::Mesh3D* clear = sel.object->ClearMesh()) {
        renderer.DrawClear(*clear, gfx::Renderer3D::Model(x, y, z, yaw) * gfx::Mat4::Scale(h, h, h),
                           MaterialFor(ctx, lit, false));
    }
    renderer.End(canvas);
}

} // namespace den::app
