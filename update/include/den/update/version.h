// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Semantic versions (semver.org): "1.2.3", "v1.2.3-beta.1", "1.2.3+build" (the build part is
// ignored, as semver says).

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace den::update {

struct Version {
    unsigned long long major = 0, minor = 0, patch = 0;
    /// The pre-release identifiers ("beta", "1"); empty for a release.
    std::vector<std::string> pre;
};

/// A leading "v" is allowed. nullopt for anything that is not a semantic version.
std::optional<Version> ParseVersion(std::string_view text);
/// Below 0, 0 or above 0 as `a` has lower, the same or higher precedence than `b`.
int CompareVersions(const Version& a, const Version& b);
/// Whether `candidate` is a later version than `current`; false when either does not parse.
bool IsNewer(std::string_view candidate, std::string_view current);
/// Whether a build of this version may be offered updates: not empty, "0.0.0", "dev" or
/// anything else that does not parse.
bool CanUpdateFrom(std::string_view running);

} // namespace den::update
