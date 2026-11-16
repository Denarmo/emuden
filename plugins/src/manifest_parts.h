// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Pieces of the manifest reader shared between its files.

#pragma once

#include "den/base/json.h"
#include "den/plugins/manifest.h"

namespace den::plugins {

/// One pinned download object; `local` also takes a "file" of this device.
Result<Download> ParseDownload(const JsonValue& value, bool local = false);
/// settings[] (manifest_fields.cpp).
void ParseSettings(const JsonValue& settings, Manifest& manifest);
/// depends[]: a strict object each.
Status ParseDepends(const JsonValue& depends, Manifest& manifest);
/// capabilities of the manifest, then of the system, applied to the console.
void ApplyCapabilities(const JsonValue* manifest_level, const JsonValue* system_level, Console& console);
/// The "ecp", "executable" and "android" members of an engine-app component.
Result<EngineAppSpec> ParseEngineApp(const JsonValue& component);
/// presentation.screens: the shapes a core's picture may have (manifest_presentation.cpp).
std::vector<ScreenShape> ParseScreens(const JsonValue& shapes);
/// The members of a system's presentation and content beyond the scan rules.
void ParsePresentation(const JsonValue& presentation, Console& console);
/// option_hints, presentation, netplay, options_name and tuning of a core component.
void ParseCoreExtras(const JsonValue& value, Component& component);
/// A signing certificate digest as lower-case hex without separators ("AB:CD:.." or "abcd..").
std::string NormalCertDigest(std::string_view text);

} // namespace den::plugins
