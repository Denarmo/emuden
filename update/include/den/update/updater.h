// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Asking GitHub for the newest release and downloading its file for this system, checked
// against the release's SHA256SUMS.txt. Every call blocks: run them from a job. Nothing about
// the user or the device is sent, only the requests for the public release files.

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/platform/pal.h"
#include "den/update/release.h"

namespace den::update {

using HttpGetter = std::function<pal::HttpResponse(const pal::HttpRequest&)>;

struct Source {
    HttpGetter http; // default: pal::HttpGet
    /// The releases API address; empty: LATEST_URL, or LIST_URL for beta updates.
    std::string url;
    /// Downloads may come from http://127.0.0.1 (a test's server); otherwise https only.
    bool allow_loopback = false;
};

struct CheckResult {
    std::string body;               // the API's answer, kept to show the notes after a restart
    std::optional<Release> release; // the newest release that may be offered, newer or not
};

/// Asks the releases API once.
Result<CheckResult> FetchLatest(const Source& source, bool beta);

/// Downloads the release's file `asset_name` to `file` and keeps it only when its size and
/// SHA-256 are the ones the release states (SHA256SUMS.txt, and GitHub's digest when it gives
/// one). A file already there with that checksum is kept as it is. The job's progress is set
/// and its cancel honoured.
Status DownloadAsset(const Source& source, const Release& release, const std::string& asset_name,
                     const std::filesystem::path& file, Job* job = nullptr);

/// AppImage: makes the checked download executable and renames it over `target` (the running
/// AppImage keeps its own copy open until it ends). Both must be in the same folder.
Status ReplaceAppImage(const std::filesystem::path& downloaded, const std::filesystem::path& target);

} // namespace den::update
