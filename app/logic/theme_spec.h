// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Themes of an interface plugin (plugin.json, "themes"; docs/themes.md in emuden-plugins):
// the colours of the whole app. Sizes stay the app's own.

#pragma once

#include <string_view>
#include <vector>

#include "den/base/result.h"
#include "den/ui/theme.h"

namespace den::app {

/// The contrast text needs on the page (WCAG AA for body text).
inline constexpr double MIN_TEXT_CONTRAST = 4.5;

/// Whether fg and text_dim reach MIN_TEXT_CONTRAST on the page.
bool ReadableTheme(const ui::Theme& theme);

/// One theme per entry of "themes" with a valid id, name and readable colours, each id once.
/// A theme's key is "<plugin>/<id>". Fails when none is left or there is no such array.
Result<std::vector<ui::Theme>> ParseThemes(std::string_view manifest_json);

} // namespace den::app
