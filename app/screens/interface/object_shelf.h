// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "shelf3d" layout's row: each game as a lit 3D object (a cart, a card, a case, a jewel
// case, a disc) with its cover on it, and, where the LaunchBox Games Database has them, the
// pictures of its back, spine, cartridge and disc. Side objects turn towards the selected one, which
// stands forward, sways slowly and, for a case with a disc, slides the disc out. Selection,
// swipes and the launch timing are CoverCarousel's.

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

protected:
    void DrawContent(ui::Context& ctx) override;
    void OnTick(ui::Context& ctx) override;

private:
    struct Placed {
        std::size_t index;
        const GameObject* object;
        float x, y, z; // the centre of its bottom edge on the floor, and depth
        float height, yaw, light, weight;
    };
    const GameObject& ObjectFor(const std::string& system) const;
    /// The game's object: its console's, or the photographed cartridge once that is loaded.
    const GameObject& ObjectAt(std::size_t index) const;
    Placed Place(std::size_t index, float time) const;
    float Floor() const;
    float ObjectHeight() const;

    mutable std::map<std::string, GameObject> objects_; // by system
    mutable std::map<std::string, GameObject> photos_;  // by game, cartridges from a photograph
    FaceLoader faces_;
    float slide_ = 0; // the selected case's disc: 0 inside, 1 out
    float view_h_ = 720, view_w_ = 1280;
};

} // namespace den::app
