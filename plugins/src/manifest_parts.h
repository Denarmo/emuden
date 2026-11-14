// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Pieces of the manifest reader shared between its files.

#pragma once

#include "den/base/json.h"
#include "den/plugins/manifest.h"

namespace den::plugins {

/// One pinned download object.
Result<Download> ParseDownload(const JsonValue& value);
/// The "ecp", "executable" and "android" members of an engine-app component.
Result<EngineAppSpec> ParseEngineApp(const JsonValue& component);
/// A signing certificate digest as lower-case hex without separators ("AB:CD:.." or "abcd..").
std::string NormalCertDigest(std::string_view text);

} // namespace den::plugins
