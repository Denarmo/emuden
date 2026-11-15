// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Games as RetroAchievements knows them: its console ids, the hash it identifies a ROM or a
// disc by, and lookups for pages shown outside a game. Every call here may block: call it
// from a job or a helper thread.

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "den/achievements/runtime.h"
#include "den/base/result.h"

namespace den::achievements {

/// The RetroAchievements console of an EmuDEN system id ("gba"); 0 when it has none.
std::uint32_t ConsoleFor(std::string_view system);

/// "EmuDEN/<version> rcheevos/<version>": the User-Agent of every request to the server.
std::string UserAgent(std::string_view client_name, std::string_view client_version);

/// The hash RetroAchievements identifies the game by: of the whole ROM, of its part without a
/// header, or of a disc's boot files, as the console wants. The file is the one the core is
/// given (an unpacked ROM, not its zip). Paths are UTF-8 safe.
Result<std::string> HashFile(std::uint32_t console, const std::filesystem::path& file);
Result<std::string> HashBuffer(std::uint32_t console, std::span<const std::uint8_t> data);

/// The server's game id for a hash (0: not known). Needs no sign-in.
Result<std::uint32_t> ResolveHash(const HttpFn& http, const std::string& host, const std::string& hash);

/// The game of a hash with the user's unlocks, in hardcore or in softcore.
Result<GameProgress> FetchProgress(const HttpFn& http, const std::string& host, const Credentials& user,
                                   const std::string& hash, bool hardcore);

} // namespace den::achievements
