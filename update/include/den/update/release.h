// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// EmuDEN's releases on GitHub: the answer of the releases API, the file each system installs,
// the release's SHA256SUMS.txt and when to ask again.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/json.h"

namespace den::update {

/// Asked without a token: GitHub allows 60 such requests an hour per address.
inline constexpr const char* LATEST_URL = "https://api.github.com/repos/Denarmo/emuden/releases/latest";
/// The newest releases, pre-releases included (Beta updates).
inline constexpr const char* LIST_URL = "https://api.github.com/repos/Denarmo/emuden/releases?per_page=10";
inline constexpr const char* RELEASES_PAGE = "https://github.com/Denarmo/emuden/releases";
inline constexpr const char* SUMS_NAME = "SHA256SUMS.txt";
inline constexpr std::int64_t CHECK_INTERVAL = 24 * 60 * 60;

struct Asset {
    std::string name;
    std::string url; // browser_download_url
    std::uint64_t size = 0;
    std::string sha256; // GitHub's own digest ("sha256:..."), lower-case hex; empty when it gives none
};

struct Release {
    std::string tag;     // "v0.2.0"
    std::string version; // "0.2.0"
    std::string name;    // "EmuDEN 0.2.0"
    std::string notes;   // the body, as GitHub has it (Markdown)
    std::string page;    // html_url
    std::string published; // "2026-11-16T22:00:00Z"
    bool draft = false;
    bool prerelease = false;
    std::vector<Asset> assets;

    const Asset* Find(std::string_view asset_name) const;
};

/// One release object of the API. nullopt without a tag that is a semantic version.
std::optional<Release> ParseRelease(const JsonValue& json);
/// The answer of LATEST_URL (an object) or LIST_URL (an array): the newest release that may be
/// offered. Drafts never are, pre-releases only with `beta`.
std::optional<Release> PickRelease(std::string_view json_text, bool beta);

/// The file of a release this system installs, by the platform key ("windows-x86_64",
/// "linux-x86_64", "android-arm64"); empty for a system no release is built for.
std::string AssetNameFor(std::string_view platform_key);

/// The lines of SHA256SUMS.txt ("<hex>  <name>", or "<hex> *<name>"): name to lower-case hex.
/// Lines that are not such are skipped.
std::map<std::string, std::string, std::less<>> ParseSums(std::string_view text);

/// Release notes in Markdown as plain text: no emphasis marks, headings or link targets;
/// list items start with "- ".
std::string PlainNotes(std::string_view markdown);

/// "2026-11-16T22:00:00Z" as "16 November 2026"; the text as it is when it is no such date.
std::string DateText(std::string_view iso);

/// Whether an automatic check is due: never checked, `interval` seconds since the last, or a
/// clock that went back past it.
bool CheckDue(std::int64_t last_check, std::int64_t now, std::int64_t interval = CHECK_INTERVAL);

} // namespace den::update
