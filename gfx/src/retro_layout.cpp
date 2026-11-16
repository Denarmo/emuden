// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "retro_layout.h"

#include <algorithm>

namespace den::gfx {

namespace {

float Side(RetroScale scale, float factor, float from, float view) {
    switch (scale) {
    case RetroScale::Source:
        return from * factor;
    case RetroScale::Viewport:
        return view * factor;
    case RetroScale::Absolute:
        return factor;
    }
    return from;
}

} // namespace

RetroPassSize RetroPassOutputSize(const RetroPassSpec& pass, bool last, float source_w, float source_h,
                                  float view_w, float view_h) {
    RetroPassSize size;
    size.into_frame = last && (!pass.scale_given ||
                               (pass.scale_x == RetroScale::Viewport && pass.scale_y == RetroScale::Viewport &&
                                pass.factor_x == 1.0f && pass.factor_y == 1.0f));
    if (size.into_frame) {
        size.width = view_w;
        size.height = view_h;
    } else {
        size.width = Side(pass.scale_x, pass.factor_x, source_w, view_w);
        size.height = Side(pass.scale_y, pass.factor_y, source_h, view_h);
    }
    return size;
}

std::size_t RetroReaderPass(RetroSem sem, int index, std::size_t pass) {
    if (sem == RetroSem::TexPassOutput || sem == RetroSem::TexPassFeedback) {
        return static_cast<std::size_t>(std::max(index, 0)) + 1;
    }
    if (sem == RetroSem::TexOriginal || sem == RetroSem::TexHistory) {
        return 0;
    }
    return pass;
}

} // namespace den::gfx
