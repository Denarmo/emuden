// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pictures of games' other faces (box back and spine, cartridge, disc) for the 3D layout,
// fetched one game at a time in the background, nearest the selection first. Each picture is
// cleaned once and kept beside the download: cropped to the object, a cartridge's or disc's
// plain background made clear, a sideways spine turned upright.

#pragma once

#include <deque>
#include <map>
#include <memory>
#include <string>

#include "den/base/jobs.h"
#include "den/gfx/types.h"
#include "den/library/library.h"

namespace den::app {

struct LoadedFaces {
    library::GameFaces faces; // the cleaned pictures
    std::string cover;        // the front cover, its scan margins cropped off; "" for none
    float cover_aspect = 0;   // its width over its height
    gfx::Color cover_color{214, 210, 202, 255}; // its mean colour: a cardboard box's sides
    // A ROM hack: its cartridge and disc are the original game's, with the original's cover on
    // the label (`base_cover`, "" when it is not known); only its box shows its own.
    bool hack = false;
    std::string base_cover;
    // From the cartridge's photograph: the plastic's colour round its edge, the cartridge's
    // width over its height (0: no usable photograph) and where its label is, 0..1 of the
    // picture (empty when it could not be told).
    gfx::Color plastic;
    float cart_aspect = 0;
    gfx::Rect cart_label{0, 0, 0, 0};
};

class FaceLoader {
public:
    ~FaceLoader();
    /// The game's faces once they are fetched; null until then (the fetch is queued).
    const LoadedFaces* Get(const std::string& id);
    /// The same without asking for them.
    const LoadedFaces* Peek(const std::string& id) const;

private:
    struct Slot {
        LoadedFaces result;
        bool done = false;
    };
    void Pump();

    std::map<std::string, std::shared_ptr<Slot>> slots_;
    std::deque<std::string> queue_;
    JobPtr job_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true); // false once this is gone
};

} // namespace den::app
