// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "shelf3d" layout's row: each game as a lit 3D object (a cart, a card, a case, a jewel
// case, a disc) with its cover on it, and, where the LaunchBox Games Database has them, the
// pictures of its back, spine, cartridge and disc. A cartridge can stand before its cardboard
// box. Side objects turn towards the selected one, which stands forward, sways slowly and,
// for a case with a disc, slides the disc out. Starting a cartridge can play it into its
// console (shelf_launch.cpp). Selection, swipes and the launch timing are CoverCarousel's.

#pragma once

#include <map>

#include "cover_carousel.h"
#include "game_faces.h"
#include "game_objects.h"

namespace den::app {

class ObjectShelf final : public CoverCarousel {
public:
    explicit ObjectShelf(std::shared_ptr<const InterfaceSpec> spec);

    gfx::Rect FocusRect() const override;
    float LaunchCover() const override;

protected:
    void DrawContent(ui::Context& ctx) override;
    void OnTick(ui::Context& ctx) override;
    float LaunchSeconds() const override;

private:
    // One game in its place: the anchor is the middle of its bottom on the floor.
    struct Placed {
        std::size_t index;
        const GameObject* object; // the cartridge, card, case or disc
        const GameObject* box;    // its cardboard box behind it, or null
        float x, y, z;
        float height, yaw, light, weight;
        float width; // across, at the front
    };
    // One object of a game, relative to the anchor: across, forward and its height.
    struct Piece {
        float dx = 0, dz = 0, height = 0;
    };
    const GameObject& ObjectFor(const std::string& system) const;
    /// The game's object: its console's, or shaped by its own pictures once they are loaded.
    const GameObject& ObjectAt(std::size_t index) const;
    const GameObject* BoxAt(std::size_t index) const;
    Placed Place(std::size_t index, float time) const;
    static Piece CartPiece(const Placed& p);
    static Piece BoxPiece(const Placed& p);
    float Floor() const;
    float ObjectHeight() const;
    gfx::Material MaterialFor(ui::Context& ctx, const Placed& p, bool box) const;
    void DrawLaunch(ui::Context& ctx, gfx::Renderer3D& renderer, const std::vector<Placed>& placed);

    mutable std::map<std::string, GameObject> objects_; // by system
    mutable std::map<std::string, GameObject> own_;     // by game: shaped by its own pictures
    mutable std::map<std::string, GameObject> boxes_;   // by system, then by game
    mutable bool console_launch_ = false;               // the launch plays into a console
    mutable FaceLoader faces_;
    float slide_ = 0; // the selected case's disc: 0 inside, 1 out
    float view_h_ = 720, view_w_ = 1280;
};

} // namespace den::app
