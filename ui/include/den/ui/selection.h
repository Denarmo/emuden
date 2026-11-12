// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "den/gfx/canvas.h"

namespace den::ui {

/// Draws the accent border around the current selection. Nothing else may use the accent.
void DrawSelectionBorder(gfx::Canvas& canvas, const gfx::Rect& rect, float radius, double time);

} // namespace den::ui
