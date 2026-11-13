// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// ROM hacks: finding the game a patch was made for among the user's games, and adding the
// patched copy to the library. The base game is only read.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/library/library.h"
#include "den/patching/patch.h"

namespace den::library {

struct BaseQuery {
    std::string system;                // empty = every system in `systems` (all when that is empty too)
    std::vector<std::string> systems;
    std::optional<std::uint32_t> crc32;
    std::string sha1, md5;             // lower-case hex, empty = not asked
    std::optional<std::uint64_t> size; // narrows the candidates before hashing
};

struct BaseMatch {
    Entry entry;
    std::string form; // "raw", "headerless", "z64": the form of the file that has the checksum
    /// The file that matched: the entry's own, or one of its other regional versions.
    std::string path;
};

struct HackRequest {
    std::string base_id; // library entry to patch
    std::filesystem::path patch;
    std::string title, version, description, plugin, source;
    std::filesystem::path cover; // empty = the base's cover
    bool keep_old = false;       // an installed older version stays as a game of its own
};

struct Hack {
    std::string id, base_id, title, version, plugin, source;
    std::int64_t installed = 0;
};

class Hacks {
public:
    virtual ~Hacks() = default;
    /// The game with that checksum in any of its forms. Blocking (hashes candidates; every
    /// file is hashed once and remembered): call from a job or a worker thread.
    virtual std::optional<BaseMatch> FindBase(const BaseQuery& query, const Job* cancel = nullptr) = 0;
    /// Patches a copy of the base game and adds it to the library at once. The copy goes to
    /// "Hacks" in the console's folder, or under the app's data when that cannot be written.
    /// Installing a title again replaces its file and keeps the entry's id, so its saves and
    /// records stay. Blocking.
    virtual Result<Entry> InstallHack(const HackRequest& request,
                                      std::function<bool(float)> progress = {}) = 0;
    /// The installed hacks; with `plugin`, those it installed.
    virtual std::vector<Hack> List(const std::string& plugin = {}) const = 0;
    /// `plugin` may change only the hacks it installed; the app itself ("") any.
    virtual Status Update(const std::string& id, const std::string& plugin,
                          const std::optional<std::string>& title,
                          const std::optional<std::string>& description,
                          const std::filesystem::path& cover) = 0;
    /// Deletes the patched file and what belongs to it. Saves stay.
    virtual Status Remove(const std::string& id, const std::string& plugin) = 0;
};

/// The hacks of a library; owned by it, valid as long as it is.
Hacks& HacksOf(Library& library);

/// A patch file the user chose, ready to install.
struct PatchFile {
    std::filesystem::path patch; // the patch itself (unpacked when it came in a zip)
    std::string title;           // from the file name
    patching::PatchInfo info;
    /// The game the patch names by its checksum, when the library has it.
    std::optional<BaseMatch> base;
    /// Without one: the games it may be for (the right size when the patch says it, else
    /// every game of the consoles such patches are for), for the user to choose from.
    std::vector<Entry> choices;
    /// Why `base` is empty although the patch names its game, for the user.
    std::string missing;
};

/// Reads a patch (.ips, .ups, .bps, .aps, .ppf, .xdelta, or a zip holding one; a zip is
/// unpacked to `work_dir`) and looks for its base game. Blocking.
Result<PatchFile> OpenPatchFile(Library& library, const std::filesystem::path& file,
                                const std::filesystem::path& work_dir, const Job* cancel = nullptr);

} // namespace den::library
