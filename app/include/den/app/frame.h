// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "den/app/app.h"
#include "den/ui/page.h"

namespace den::app {

/// The width of the page in logical units (the canvas'), which the layouts follow.
float PageWidth();
float PageHeight();
Services::SafePadding SafeMargins();

/// Builds the frame every top-level screen shares into the page's root: the rail at the left
/// (from the registry, the entry for `page`'s route marked; slim on a narrow page, see
/// logic/page_widths.h) and a titled content column. Returns the content column to fill. The
/// page is built again when its width calls for the other rail.
ui::Node* BuildRailFrame(ui::Page& page, const std::string& title);

/// A content column with a title and no rail, for pages reached from another page.
ui::Node* BuildPlainFrame(ui::Page& page, const std::string& title);

/// The frame of a page's second level (a Settings section, a plugin's page): no rail, so at
/// most two levels show, and a back button before the title that does what the pad's Back
/// does. Returns the content column, across the page.
ui::Node* BuildLevelFrame(ui::Page& page, const std::string& title);

} // namespace den::app
