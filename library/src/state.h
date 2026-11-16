// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the library keeps on disk under <data>/library: library.json (the folders and the play
// records, kept for games that are gone too) and cache.json (the entries of the last scan,
// shown at once on the next start).

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "den/library/library.h"

namespace den::library {

struct Record {
    bool favourite = false;
    std::int64_t last_played = 0;
    std::int64_t play_seconds = 0;
    int play_count = 0;
    std::int64_t added = 0;
    PlayTotals other; // the other devices' play, from sync
    std::string name; // the name the user sees instead of the file's; "" = the file's
    std::string hack_of; // a hack the tidy-up linked to its base game in place; "" = none
};

struct Saved {
    std::vector<Folder> folders;
    std::map<std::string, Record> records;
    std::vector<Session> sessions; // oldest first
};

/// nullopt when there is no library file yet (the first run).
std::optional<Saved> LoadSaved(const std::filesystem::path& file);
bool SaveSaved(const std::filesystem::path& file, const Saved& saved);

std::vector<Entry> LoadEntryCache(const std::filesystem::path& file);
bool SaveEntryCache(const std::filesystem::path& file, const std::vector<Entry>& entries);

/// The host folders the earlier app's profile lists as game folders, in its order.
std::optional<std::vector<std::string>> ReadOldGameFolders(const std::filesystem::path& profile);

} // namespace den::library
