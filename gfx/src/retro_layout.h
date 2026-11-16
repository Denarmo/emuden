// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sizes of a preset's passes and how each texture a pass reads is sampled, without GL.

#pragma once

#include <cstddef>

#include "den/gfx/retro_chain.h"
#include "retro_names.h"

namespace den::gfx {

struct RetroPassSize {
    float width = 0, height = 0;
    bool into_frame = false; // the last pass draws straight into the frame, at its size
};

/// A pass's output size from its source's size and the frame's. The last pass draws into the
/// frame unless it asks for a size of its own other than the frame's.
RetroPassSize RetroPassOutputSize(const RetroPassSpec& pass, bool last, float source_w, float source_h,
                                  float view_w, float view_h);

/// The pass whose filtering and wrapping a texture is read with: the pass after the one that
/// drew it (the first for the original and history), the reading pass for its source.
std::size_t RetroReaderPass(RetroSem sem, int index, std::size_t pass);

} // namespace den::gfx
