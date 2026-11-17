// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What den.mods does on disk (docs/game-mods-api.md), without Lua or the app: looking for
// files in a game's folder, unpacking entries of an archive, copying a plugin's files into a
// game's folder with a record and the originals kept, and setting up Vulkan implicit layers.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"

namespace den::plugins::feature::mods {

namespace fs = std::filesystem;

inline constexpr std::size_t MAX_NAMES = 16;
inline constexpr std::size_t MAX_FOUND = 64;
inline constexpr int MAX_DEPTH = 8;
inline constexpr std::uint64_t MAX_HASHED = 512ull << 20;
inline constexpr std::size_t MAX_ENTRIES = 32;
inline constexpr std::uint64_t MAX_ENTRY = 512ull << 20;
inline constexpr std::size_t MAX_FILES = 64;
inline constexpr std::uint64_t MAX_APPLY = 1ull << 30;
inline constexpr std::size_t MAX_LAYERS = 8;
inline constexpr std::uint64_t MAX_MANIFEST = 1ull << 20;

/// A Steam app id: up to ten digits.
bool IsAppId(std::string_view text);
/// A file inside a folder as scripts name it: relative, "/" between its parts, no "..", no
/// part that ends with a dot or a space, nothing a drive or a stream could hide behind.
bool IsInnerPath(std::string_view path);
/// One part of such a path.
bool IsPlainName(std::string_view name);

/// The file `to` names in the game's folder. Fails when it leaves the folder, when a part
/// of the way is a link, or when something other than a file is already there.
Result<fs::path> Target(const fs::path& game_dir, std::string_view to);

struct Found {
    std::string path; // relative, "/"
    std::uint64_t size = 0;
    std::string sha256; // empty for a file over MAX_HASHED
};

/// At most MAX_NAMES file names, each with at most one "*".
Status CheckNames(const std::vector<std::string>& names);
/// The files of the folder with those names (compared without case), at most MAX_FOUND, in
/// folders at most MAX_DEPTH deep. Links are neither followed nor listed.
std::vector<Found> Find(const fs::path& dir, const std::vector<std::string>& names,
                        const std::atomic<bool>* cancel = nullptr);

/// The folder name of an archive's unpacked entries: its file name without the archive
/// extension ("lsfg-vk-2.0.tar.xz" -> "lsfg-vk-2.0").
std::string ArchiveStem(std::string_view file_name);
/// Writes those entries of a zip, 7z or tar.xz to `out_dir`/<entry>. Fails for an entry the
/// archive does not have, one that is a link, one over MAX_ENTRY, or when they need more
/// than `room` bytes.
Status Unpack(const fs::path& archive, const std::vector<std::string>& entries, const fs::path& out_dir,
              std::uint64_t room, const std::function<bool()>& cancelled = {});

struct Order {
    fs::path from;
    std::string to;      // relative in the game's folder
    std::string keep_as; // or empty: a file name the replaced file is also kept under, beside `to`

    Order() = default;
    Order(fs::path from_, std::string to_, std::string keep_as_ = {})
        : from(std::move(from_)), to(std::move(to_)), keep_as(std::move(keep_as_)) {}
};

struct Planned {
    std::string to;
    bool replaces = false; // a file of the game is there now
    std::uint64_t size = 0;
    std::string keep; // the kept copy's path in the game's folder, or empty
};

struct FileState {
    std::string to;
    bool intact = false; // still the file the app wrote
};

struct Applied {
    std::string name;
    std::int64_t time = 0;
    bool complete = false; // false: the apply was cut short
    std::vector<FileState> files;
};

struct LayerOrder {
    fs::path manifest, library;
};

struct LayerState {
    std::int64_t time = 0;
    std::vector<std::string> manifests; // the file names in the layer folder
    bool intact = false;
};

/// The records of one plugin: <data>/mods/<plugin id>.
class Store {
public:
    explicit Store(fs::path root) : root_(std::move(root)) {}

    /// Checks an apply and says for each file whether it is new or replaces one.
    Result<std::vector<Planned>> Plan(const std::string& appid, const fs::path& game_dir,
                                      const std::vector<Order>& files) const;
    /// Undoes an earlier apply to the game, writes the record, keeps the originals, copies.
    /// A failed copy undoes what was done. `cut` returning true stops at once and leaves
    /// things as they are, as when the app is closed: Remove undoes that too.
    Status Apply(const std::string& appid, const std::string& name, const fs::path& game_dir,
                 const std::vector<Order>& files, std::int64_t time, const std::function<bool()>& cut = {});
    std::optional<Applied> State(const std::string& appid) const;
    /// The name of what is applied to the game, without looking at the game's files.
    std::optional<std::string> Name(const std::string& appid) const;
    /// Deletes added files and puts originals back where the file is still the one the app
    /// wrote. Returns the files left alone. On failure the record stays.
    Result<std::vector<std::string>> Remove(const std::string& appid);
    /// The games with a record.
    std::vector<std::string> Games() const;

    /// The manifest file names an ApplyLayer would write; fails for what it would refuse.
    Result<std::vector<std::string>> PlanLayer(const std::string& name, const std::vector<LayerOrder>& layers,
                                               const std::string& plugin) const;
    /// Copies the libraries to <root>/layers/<name>/ and writes each manifest to
    /// `manifest_dir`/emuden-<plugin>-<file> with library_path set to the copy.
    Status ApplyLayer(const std::string& name, const std::vector<LayerOrder>& layers, const fs::path& manifest_dir,
                      const std::string& plugin, std::int64_t time);
    std::optional<LayerState> Layer(const std::string& name) const;
    Status RemoveLayer(const std::string& name);
    std::vector<std::string> Layers() const;

    /// Everything the plugin applied and every layer it set up, undone. `notes` name what
    /// could not be.
    Status RemoveAll();
    std::size_t Count() const {
        return Games().size() + Layers().size();
    }

private:
    fs::path root_;
};

/// A layer's name as scripts give it: letters, digits, ".", "-" and "_", at most 48.
bool IsLayerName(std::string_view name);
/// The manifest with every library_path set to `library_path` and nothing else changed.
/// Fails for text that is not a Vulkan layer manifest, or whose layer has no
/// disable_environment (it could not be switched off per game).
Result<std::string> RewriteLayerManifest(std::string_view text, std::string_view library_path);

} // namespace den::plugins::feature::mods
