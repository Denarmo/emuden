// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The widths, in logical units, at which the rail-framed pages change their layout. They go by
// the room there is, not by the device: a small window on a PC gets the same layout.

#pragma once

namespace den::app {

/// Pages narrower than this get the slim rail of icons instead of the labelled one.
inline constexpr float FULL_RAIL_MIN_PAGE = 900;
inline constexpr float FULL_RAIL_WIDTH = 232;
inline constexpr float SLIM_RAIL_WIDTH = 84;
/// Settings shows its sections beside the rows from this content width; below, the sections
/// fill the page and a chosen one opens its rows on their own.
inline constexpr float SETTINGS_SIDE_BY_SIDE_MIN = 720;
/// The sections column beside the rows, and the gap between them.
inline constexpr float SETTINGS_SECTIONS_WIDTH = 220;
inline constexpr float SETTINGS_PANE_GAP = 20;

inline constexpr bool SlimRail(float page_width) {
    return page_width > 0 && page_width < FULL_RAIL_MIN_PAGE;
}

inline constexpr float RailWidth(float page_width) {
    return SlimRail(page_width) ? SLIM_RAIL_WIDTH : FULL_RAIL_WIDTH;
}

/// What a rail frame leaves for its content column: the page less the rail and both margins.
inline constexpr float RailContentWidth(float page_width, float margin) {
    return page_width - RailWidth(page_width) - margin * 2;
}

inline constexpr bool SettingsSideBySide(float content_width) {
    return content_width >= SETTINGS_SIDE_BY_SIDE_MIN;
}

} // namespace den::app
