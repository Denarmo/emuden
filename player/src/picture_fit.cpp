// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>

#include "picture_fit.h"

namespace den::player {

PictureSize FitPicture(float area_w, float area_h, float aspect, int rows, WholeScale whole) {
    if (aspect <= 0 || area_w <= 0 || area_h <= 0) {
        return PictureSize{area_w, area_h};
    }
    float w = area_w, h = area_w / aspect;
    if (h > area_h) {
        h = area_h;
        w = area_h * aspect;
    }
    if (whole == WholeScale::Off || rows <= 0) {
        return PictureSize{w, h};
    }
    const float multiple = std::floor(h / static_cast<float>(rows) + 1e-3f);
    if (multiple < 1.0f) {
        return PictureSize{w, h};
    }
    const float whole_h = multiple * static_cast<float>(rows);
    const float loss = 1.0f - whole_h / h;
    if (whole == WholeScale::Always || (multiple >= 2.0f && loss <= NEAR_WHOLE_LOSS)) {
        h = whole_h;
        w = h * aspect;
    }
    return PictureSize{w, h};
}

} // namespace den::player
