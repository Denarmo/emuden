// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Names for downloads of a backend whose engine does not suggest one (Android).

#pragma once

#include <string>
#include <string_view>

namespace den::pal {

/// The file name a browser would save a response under: the Content-Disposition header's
/// (filename*, then filename), else the last part of the address's path. Always safe
/// (WebSafeFileName).
std::string WebDownloadName(const std::string& disposition, const std::string& url);

/// ".bps", ".ips", ".ups", ".xdelta", ".aps", ".ppf", ".zip" or ".7z" when a file's first
/// bytes say it is one; empty otherwise.
std::string WebPatchExtension(std::string_view head);

} // namespace den::pal
