// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "den/engine/host.h"

namespace den::engine {

/// Whether any engine is known for the system at all.
bool KnownSystem(const std::string& system);

/// The engine for a system: the "engine path" setting when set (nothing else is tried
/// then), else a plugin-installed engine under the data folder, else a desktop copy.
std::optional<Install> Locate(const std::string& system, const std::filesystem::path& data_dir,
                              const store::Settings* settings);

/// The arguments that start `install` for a game.
std::vector<std::string> LaunchArgs(const Install& install, const LaunchRequest& request,
                                    const store::Settings* settings);

} // namespace den::engine
