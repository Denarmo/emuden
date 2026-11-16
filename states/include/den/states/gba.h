// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Game Boy Advance save states and battery saves between the GBA cores EmuDEN ships
// (docs/gba-states.md): a state one core wrote is rewritten for another, and the battery
// saves each core keeps in its own layout read and write the same file.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"

namespace den::states {

/// Whether a state of core `from` can be turned into one of core `to` (by libretro name:
/// "mGBA", "GBA DS", "gpSP"). False for the same core: nothing to convert.
bool CanConvertGbaState(std::string_view from, std::string_view to);

/// `state` (core `from`'s bytes) rewritten for core `to`. `target` is a state the running
/// `to` core just wrote for the same game: what the source does not hold (the ROM header,
/// the BIOS, the save chip, sound timing) is taken from it.
Result<std::vector<std::uint8_t>> ConvertGbaState(std::string_view from, std::span<const std::uint8_t> state,
                                                  std::string_view to,
                                                  std::span<const std::uint8_t> target);

/// Why `state` of core `core` could not be converted to another core right now ("" when it
/// could): the CPU inside a BIOS routine other than a wait, a DMA or save chip command
/// midway. A frame later it usually can.
std::string GbaStateNotPortable(std::string_view core, std::span<const std::uint8_t> state);

/// The battery save in the layout every GBA core reads: the chip's bytes from offset 0.
/// VBA Next keeps an EEPROM at 0x20000 of a 0x22000-byte buffer; any other file is returned
/// as it is.
std::vector<std::uint8_t> PortableGbaSave(std::span<const std::uint8_t> file);

/// Puts a portable save (or any core's) into `core`'s save memory. `rom_says_eeprom`: the
/// game's ROM names an EEPROM library (see RomUsesEeprom), which only VBA Next needs.
void LoadGbaSave(std::string_view core, std::span<const std::uint8_t> file, bool rom_says_eeprom,
                 std::span<std::uint8_t> memory);

/// Whether a GBA ROM carries Nintendo's EEPROM library ("EEPROM_V").
bool RomUsesEeprom(std::span<const std::uint8_t> rom);

} // namespace den::states
