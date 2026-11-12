// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace den::store {

/// Key-value settings kept in one JSON file ("settings.json" in the data folder). Keys are
/// dotted ("ui.theme", "player.rewind"). Safe from any thread; a change is written to disk
/// right away, atomically. This is the simple store of the first stage: the SQLite store
/// with sync classes replaces the file, not this interface.
class Settings {
public:
    explicit Settings(std::filesystem::path file);
    ~Settings();

    bool Has(const std::string& key) const;
    std::string GetString(const std::string& key, const std::string& fallback = {}) const;
    bool GetBool(const std::string& key, bool fallback = false) const;
    double GetNumber(const std::string& key, double fallback = 0) const;
    std::vector<std::string> GetList(const std::string& key) const;

    void SetString(const std::string& key, const std::string& value);
    void SetBool(const std::string& key, bool value);
    void SetNumber(const std::string& key, double value);
    void SetList(const std::string& key, const std::vector<std::string>& values);
    void Remove(const std::string& key);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace den::store
