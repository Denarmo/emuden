// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the feature host reads from the console host: the index it fetched and verified.

#pragma once

#include <optional>
#include <string>

#include "den/plugins/index.h"

namespace den::plugins {

class IndexSource {
public:
    virtual ~IndexSource() = default;
    /// The verified index, nullopt before the first one. `expired`: nothing new may be
    /// installed from it, its revocations still apply.
    virtual std::optional<Index> OfferedIndex(bool& expired) const = 0;
    virtual std::string IndexUrl() const = 0;
    /// "github:owner/repo[@ref]" as the host was given it.
    virtual std::string SourceText() const = 0;
};

} // namespace den::plugins
