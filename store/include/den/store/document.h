// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A versioned JSON document in one file: {"version": N, "data": {...}}. The simple store of
// the first stage for a module's own records (the library's folders and play records); the
// SQLite store replaces the file, not what is kept in it.

#pragma once

#include <filesystem>
#include <optional>

#include "den/base/json.h"

namespace den::store {

struct Document {
    int version = 0;
    JsonValue data; // an object
};

/// nullopt when the file is missing or is not such a document. A document written by a newer
/// version of the app is returned as it is: the caller decides by `version`.
std::optional<Document> LoadDocument(const std::filesystem::path& file);

/// Written atomically; the folder is created when missing.
bool SaveDocument(const std::filesystem::path& file, int version, JsonValue data);

} // namespace den::store
