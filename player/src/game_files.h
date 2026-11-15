// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Where a game's files live under the player's data folder, the save-state file format, and
// getting a ROM out of a zip.

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::player {

/// A name safe as a file name on every device.
std::string SafeFileName(const std::string& name);

struct GamePaths {
    std::filesystem::path save_dir;  // <data>/saves/<system>: the core's own files too
    std::filesystem::path state_dir; // <data>/states/<system>
    std::string base;                // file name stem for this game

    std::filesystem::path BatterySave() const;      // <save_dir>/<base>.srm
    std::filesystem::path State(int slot) const;    // <state_dir>/<base>.slot<N>.state
    std::filesystem::path Thumbnail(int slot) const; // the same with .png
    std::filesystem::path Resume() const;           // <state_dir>/<base>.resume.state
    std::filesystem::path ResumePicture() const;    // <state_dir>/<base>.resume.png
};

GamePaths MakeGamePaths(const std::filesystem::path& data_dir, const std::string& system,
                        const std::string& game_id, const std::filesystem::path& content);

/// A save state as it is stored: a header naming the core that made it, then the core's
/// bytes. A state only loads on the core it names.
std::vector<std::uint8_t> WrapState(const std::string& core_name,
                                    std::span<const std::uint8_t> state);
/// The core's bytes inside `file`.
Result<std::span<const std::uint8_t>> UnwrapState(const std::string& core_name,
                                                  std::span<const std::uint8_t> file);

/// The game inside a zip, for a core that does not open zips itself: the first file with one
/// of `extensions` (lower case, no dot), extracted under `cache_dir` (emptied first).
/// A path that is not a zip is returned as it is.
Result<std::filesystem::path> ResolveContent(const std::filesystem::path& content,
                                             const std::vector<std::string>& extensions,
                                             const std::filesystem::path& cache_dir);

} // namespace den::player
