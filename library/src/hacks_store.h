// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the hacks of a library are made of: the file beside each patched ROM that says what it
// is, the place hacks are written to, and the remembered checksums of the user's games.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/library/hacks.h"
#include "den/library/systems.h"
#include "den/patching/checksum.h"

namespace den::library {

/// What the hacks need from the library that owns them.
struct HackHooks {
    Library* library = nullptr;
    std::filesystem::path data_dir; // the app's data folder
    std::function<SystemTable()> systems;
    /// Shows an entry at once, in place of the one with its id.
    std::function<void(Entry)> adopt;
    std::function<void(const std::string& id)> drop;
};

std::unique_ptr<Hacks> CreateHacks(HackHooks hooks);

/// "<rom>.hack.json", beside a patched ROM.
struct Sidecar {
    std::string title, version, description, base_id, base_sha1, patch_sha256, plugin, source;
    std::int64_t installed = 0; // unix seconds
};

std::optional<Sidecar> ReadSidecar(const std::filesystem::path& rom);
bool WriteSidecar(const std::filesystem::path& rom, const Sidecar& sidecar);
void RemoveSidecar(const std::filesystem::path& rom);

/// The hack folders under the app's data (<library dir>/hacks/<system>), as folders to scan.
void AddHackFolders(const std::filesystem::path& library_dir, std::vector<Folder>& folders);

/// After a scan: the files with a sidecar become hacks (title, base, and the base's cover
/// when they have no picture of their own).
void ApplyHackSidecars(std::vector<Entry>& entries);
/// A hack without a picture of its own (none, or a drawn placeholder) takes the cover of the
/// game it was made from, when that game has one. True when a cover changed.
bool UseBaseCovers(std::vector<Entry>& entries);

/// Where the hacks of a game are written.
struct HackPlace {
    std::filesystem::path dir;  // ".../Hacks"
    std::filesystem::path root; // the console's folder: ids are made of the path below it
};

/// "Hacks" in the console's folder of the library folder the base game is in, when that can
/// be written; else under the app's data.
HackPlace PlaceForHack(const Entry& base, const std::vector<Folder>& folders, const SystemTable& systems,
                       const std::filesystem::path& library_dir);

/// A title as a file name: no characters a file system refuses, never empty.
std::string SafeFileName(std::string_view title);

/// The cover file beside a ROM (same name, an image extension), empty when there is none.
std::filesystem::path CoverBeside(const std::filesystem::path& rom);
/// Copies `image` beside the ROM as its cover, replacing an older one.
std::filesystem::path PutCoverBeside(const std::filesystem::path& rom, const std::filesystem::path& image);

/// The checksums of the user's games, computed once per file (path, size and date) and kept
/// in <library dir>/checksums.json.
class ChecksumCache {
public:
    explicit ChecksumCache(std::filesystem::path file);
    Result<std::vector<patching::RomForm>> Get(const std::filesystem::path& rom, std::string_view system,
                                               const std::function<bool()>& cancelled = {});
    /// Writes what Get learned since the last time.
    void Flush();

private:
    struct Known {
        std::uint64_t size = 0;
        std::string written, system;
        std::vector<patching::RomForm> forms;
    };
    void Load();
    void Save() const;

    std::filesystem::path file_;
    std::mutex mutex_;
    bool loaded_ = false;
    bool dirty_ = false;
    std::map<std::string, Known> known_;
};

} // namespace den::library
