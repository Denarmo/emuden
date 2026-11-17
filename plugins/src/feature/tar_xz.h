// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Files out of a .tar.xz (ustar, with GNU and pax long names), read once from start to end.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "den/base/result.h"

namespace den::plugins::feature {

struct TarMember {
    std::string name; // "/" between its parts, a leading "./" taken off
    std::uint64_t size = 0;
    bool file = false; // a regular file; false for folders, links and devices
};

/// Asks `take` for every member in the archive's order. A path it returns gets the member's
/// bytes (regular files only; the file appears once complete), an empty path skips the
/// member, a failure ends the walk with it.
Status ReadTarXz(const std::filesystem::path& archive,
                 const std::function<Result<std::filesystem::path>(const TarMember&)>& take,
                 const std::function<bool()>& cancelled = {});

} // namespace den::plugins::feature
