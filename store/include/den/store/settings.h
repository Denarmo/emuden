// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "den/base/json.h"

namespace den::store {

/// Key-value settings kept in one JSON file ("settings.json" in the data folder). Keys are
/// dotted ("ui.theme", "player.rewind"). Safe from any thread; a change is written to disk
/// right away, atomically. This is the simple store of the first stage: the SQLite store
/// with sync classes replaces the file, not this interface.
class Settings {
public:
    explicit Settings(std::filesystem::path file);
    ~Settings();

    /// Whether the key has a value in effect: stored, or given by the defaults layer.
    bool Has(const std::string& key) const;
    /// Whether the file itself holds the key (the user's own choice).
    bool Stored(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& fallback = {}) const;
    bool GetBool(const std::string& key, bool fallback = false) const;
    double GetNumber(const std::string& key, double fallback = 0) const;
    std::vector<std::string> GetList(const std::string& key) const;

    void SetString(const std::string& key, const std::string& value);
    void SetBool(const std::string& key, bool value);
    void SetNumber(const std::string& key, double value);
    void SetList(const std::string& key, const std::vector<std::string>& values);
    /// Removes the stored value; a default of the key shows through again.
    void Remove(const std::string& key);

    /// Every stored value (the file's object), for sync.
    JsonValue StoredValues() const;
    /// Stores several values at once, any JSON type, with one write.
    void SetValues(const std::vector<std::pair<std::string, JsonValue>>& values);

    /// The defaults layer: values in effect for keys the file does not hold (what suits this
    /// device). Never written to disk; a stored value always wins. Replaces the whole layer.
    void SetDefaults(std::vector<std::pair<std::string, JsonValue>> defaults);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace den::store
