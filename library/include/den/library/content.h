// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// From a library entry to the file a core is started with: zipped ROMs and disc sets.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"
#include "den/library/library.h"

namespace den::library {

/// The file to start a core with: the entry's file, or for a game in a zip archive its
/// unpacked copy under `unpack_dir` (unpacked on the first call, which takes as long as
/// copying the game; games not played for a month are removed from there). Run it from a job.
Result<std::filesystem::path> PrepareRom(const Entry& entry, const std::filesystem::path& unpack_dir,
                                         const std::function<bool()>& cancelled = {});

/// The name the discs of one game share: the file name up to its disc tag, with the region
/// when that is only written after it. "Final Quest (USA)" and "Final Quest (Japan)" are two
/// games. Saves and playlists of a multi-disc game are named by it.
std::string DiscSetName(const std::filesystem::path& disc);

/// The discs of the game a disc image belongs to, in disc order: the images of the same kind
/// with the same DiscSetName, in its folder and, when each disc has a folder of its own, in
/// the folders next to it. Only `disc` itself when the game has one disc.
std::vector<std::filesystem::path> DiscSet(const std::filesystem::path& disc);

/// A relative path that stays inside the folder it is joined to.
bool IsSafeRelativePath(std::string_view relative);

/// The game's name as the cartridge itself carries it ("POKEMON EMER"), for consoles whose
/// manifest says where it is. "" otherwise or for an unreadable file.
std::string ReadRomHeaderTitle(const SystemDef& system, const std::filesystem::path& rom);

} // namespace den::library
