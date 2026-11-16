// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Which game a ROM hack was made from, read from the hack itself. A hack usually keeps the
// header of the game it was made from: its game code ("BPRE" for Pokemon FireRed) even when
// the title in it changed, and often the title. The codes of the games hacks are made from
// most are listed here; the box of the game found is the hack's cover, with the HACK sticker
// drawn over it (artwork.h IsHackBox).

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <string_view>

#include "den/library/artwork.h"

namespace den::library {

/// What a ROM's header says about the game. Fields are "" when the header has none.
struct RomCode {
    std::string title; // as in the header ("POKEMON FIRE"), trimmed
    std::string code;  // the game code ("BPRE"), upper case
};

/// GBA: title 0xA0, code 0xAC. GB and GBC: title 0x134, and the code at 0x13F on a cartridge
/// that marks Game Boy Color support. NDS: title 0x00, code 0x0C. N64: title 0x20, code 0x3B
/// (any byte order). SNES: the internal name at 0x7FC0 or 0xFFC0 (after a copier's 512 bytes),
/// whichever has a valid checksum pair. PS1 and PS2: the disc's boot serial as the code
/// (ReadDiscSerial). Other consoles: nothing.
RomCode ReadRomCode(const std::filesystem::path& rom, std::string_view system);

/// The serial ("SLUS-20946") of the program SYSTEM.CNF boots (BOOT2, or BOOT on a PS1 disc),
/// read from the disc's ISO 9660 file system: a cooked .iso, raw .bin/.img sectors (mode 1 or
/// mode 2), or the first track a .cue or .m3u names. "" for anything else (CHD, CSO, PBP).
std::string ReadDiscSerial(const std::filesystem::path& disc);

/// ReadRomCode of a library entry (nothing for a game in an archive), with `manifest_title`
/// (the title the console's manifest reads) when the reader has no title for it.
RomCode HeaderOf(const Entry& entry, std::string_view manifest_title);

/// The game a code belongs to, as the libretro collection names it, from the list of games
/// hacks are made from most; the same game in another region when only the region letter
/// differs. "" when the code is not in the list.
std::string GameOfCode(std::string_view system, std::string_view code);
/// The same for a header title the list knows ("SUPER MARIOWORLD"), for consoles without codes.
std::string GameOfTitle(std::string_view system, std::string_view title);

/// The box of the game a hack was made from: by its code, a known title, then MatchBaseGame by
/// the file's name and the header title, then by the header title alone. The name found is
/// fitted to the collection (MatchThumbnail). "" when nothing fits.
std::string FindBaseGame(const ThumbnailIndex& index, std::string_view system, std::string_view original_name,
                         const RomCode& header);

/// Whether `name` only names `game`: each of its words (bracketed tags, "the", "version",
/// revisions and regions left out) is among the game's ("Pokemon FireRed USA" of "Pokemon -
/// FireRed Version (USA, Europe)"); "Pokemon FireRed Plus" is not.
bool NameOfGame(std::string_view name, std::string_view game);

/// The game among `games` an already patched ROM is a hack of: same console, the same code
/// (the same header title on consoles without codes), not a hack itself, another file, and
/// named as another game (NameOfGame false, also against the code's known game). The base
/// is the code's known game, or for other codes one whose name is all in the hack's ("Space
/// Quest" of "Space Quest Redux"); a hack renamed outright is not linked then. Placeholder
/// codes ("0000", "AAAA") are not compared. nullopt when none is.
std::optional<Entry> FindHackBase(const std::vector<Entry>& games, std::string_view system,
                                  const std::filesystem::path& rom, std::string_view name, const RomCode& header);

} // namespace den::library
