// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the feature host reads from the console host: the sources and the indexes it fetched
// and verified.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "den/plugins/sources.h"

namespace den::plugins {

/// plugins/<id>/source.json names the added source a plugin came from; none: the official one.
inline constexpr const char* ORIGIN_FILE = "source.json";
/// The source a plugin folder names, empty when it names none.
std::string ReadOrigin(const std::filesystem::path& plugin_dir);
bool WriteOrigin(const std::filesystem::path& plugin_dir, const std::string& address);

class IndexSource {
public:
    virtual ~IndexSource() = default;
    /// Every source, the official one first, switched-off ones included (their revocations
    /// still apply). An index is nullopt before the first one was verified.
    virtual std::vector<SourceView> SourceViews() const = 0;
};

} // namespace den::plugins
