// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pictures of games' other faces (box back and spine, cartridge, disc) for the 3D layout,
// fetched one game at a time in the background, nearest the selection first.

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
    library::GameFaces faces;
    // From the cartridge's photograph: the plastic's colour round its edge, the part of the
    // picture it fills and that part's width over its height (0: no usable photograph).
    gfx::Color plastic;
    gfx::Rect cart_uv{0, 0, 1, 1};
    float cart_aspect = 0;
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
