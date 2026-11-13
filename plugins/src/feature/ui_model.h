// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// From the table a script gives den.ui.set to the PageModel the renderer draws.

#pragma once

#include <string>

#include "den/base/json.h"
#include "den/plugins/feature_ui.h"

namespace den::plugins::feature {

inline constexpr std::size_t MAX_ELEMENTS = 400;
inline constexpr std::size_t MAX_PAGES = 8;
inline constexpr std::size_t MAX_STRING = 4000;

/// Unknown element types and fields are left out; strings are cut at MAX_STRING and the page
/// stops taking elements after MAX_ELEMENTS.
PageModel PageFromJson(const JsonValue& page);

/// Changes the fields named in `fields` on the element with that id. False when the page has
/// no such element.
bool PatchElement(PageModel& page, const std::string& id, const JsonValue& fields);

/// The compact text of a value (one line), for storage and arguments (lua_json.cpp).
std::string CompactJson(const JsonValue& value);

/// The text cut at MAX_STRING bytes, never inside a UTF-8 sequence.
std::string CutText(std::string text);

} // namespace den::plugins::feature
