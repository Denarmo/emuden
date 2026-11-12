// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "den/ui/page.h"

namespace den::app {

/// Builds the frame every top-level screen shares into the page's root: the rail at the left
/// (from the registry, the entry for `page`'s route marked) and a titled content column.
/// Returns the content column to fill.
ui::Node* BuildRailFrame(ui::Page& page, const std::string& title);

/// A content column with a title and no rail, for pages reached from another page.
ui::Node* BuildPlainFrame(ui::Page& page, const std::string& title);

} // namespace den::app
