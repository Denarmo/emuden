// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/base/files.h"
#include "den/states/gba.h"
#include "gba_machine.h"

namespace den::states {

using namespace gba;

namespace {

enum class Format { None, MGba, Gpsp };

// GBA DS is libmgba: its state is mGBA's, without the extras mGBA's libretro core appends.
Format FormatOf(std::string_view core) {
    if (EqualsNoCase(core, "mGBA") || EqualsNoCase(core, "GBA DS")) {
        return Format::MGba;
    }
    if (EqualsNoCase(core, "gpSP")) {
        return Format::Gpsp;
    }
    return Format::None;
}

Bios BiosOf(Format format) {
    return format == Format::Gpsp ? Bios::Open : Bios::MGba;
}

Result<Machine> Read(Format format, std::span<const u8> state) {
    return format == Format::Gpsp ? ReadGpsp(state) : ReadMgba(state);
}

// The machine read and made fit for another BIOS and a target that runs DMAs whole.
Result<Machine> Portable(Format format, std::span<const u8> state, Bios to) {
    auto read = Read(format, state);
    if (!read) {
        return read;
    }
    Machine& m = read.value();
    if (!m.not_portable.empty()) {
        return Result<Machine>::Fail(m.not_portable);
    }
    if (m.bios != to) {
        if (Status s = LeaveBios(m); !s) {
            return Result<Machine>::Fail(s.error);
        }
        if (Status s = MoveIrqToBios(m, to); !s) {
            return Result<Machine>::Fail(s.error);
        }
    }
    if (Status s = FinishDmas(m); !s) {
        return Result<Machine>::Fail(s.error);
    }
    return read;
}

} // namespace

bool CanConvertGbaState(std::string_view from, std::string_view to) {
    return FormatOf(from) != Format::None && FormatOf(to) != Format::None && !EqualsNoCase(from, to);
}

Result<std::vector<std::uint8_t>> ConvertGbaState(std::string_view from, std::span<const std::uint8_t> state,
                                                  std::string_view to,
                                                  std::span<const std::uint8_t> target) {
    using Out = Result<std::vector<std::uint8_t>>;
    const Format source = FormatOf(from);
    const Format dest = FormatOf(to);
    if (source == Format::None || dest == Format::None) {
        return Out::Fail("EmuDEN cannot convert states between " + std::string(from) + " and " + std::string(to));
    }
    if (source == Format::MGba && dest == Format::MGba) {
        // The same libmgba state: only mGBA's extras are dropped (its save chip comes from
        // the battery save, which syncs with it).
        if (state.size() < MGBA_STATE_SIZE) {
            return Out::Fail("the save state is cut short");
        }
        std::vector<std::uint8_t> out(state.begin(), state.begin() + MGBA_STATE_SIZE);
        out.resize(MGBA_STATE_SIZE + 16, 0);
        return out;
    }
    auto machine = Portable(source, state, BiosOf(dest));
    if (!machine) {
        return Out::Fail(machine.error());
    }
    return dest == Format::Gpsp ? WriteGpsp(machine.value(), target) : WriteMgba(machine.value(), target);
}

std::string GbaStateNotPortable(std::string_view core, std::span<const std::uint8_t> state) {
    const Format format = FormatOf(core);
    if (format == Format::None) {
        return "not a GBA core EmuDEN converts states of";
    }
    // To the other BIOS: the stricter of the two.
    const auto machine = Portable(format, state, format == Format::Gpsp ? Bios::MGba : Bios::Open);
    return machine ? std::string() : machine.error();
}

std::vector<std::uint8_t> PortableGbaSave(std::span<const std::uint8_t> file) {
    constexpr std::size_t VBA_SIZE = 0x22000, EEPROM_AT = 0x20000;
    const auto blank = [](std::span<const std::uint8_t> bytes) {
        return std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b == 0xFF; });
    };
    if (file.size() == VBA_SIZE && blank(file.subspan(0, EEPROM_AT)) && !blank(file.subspan(EEPROM_AT))) {
        return {file.begin() + EEPROM_AT, file.end()};
    }
    return {file.begin(), file.end()};
}

void LoadGbaSave(std::string_view core, std::span<const std::uint8_t> file, bool rom_says_eeprom,
                 std::span<std::uint8_t> memory) {
    constexpr std::size_t VBA_SIZE = 0x22000, EEPROM_AT = 0x20000, EEPROM_SIZE = 0x2000;
    std::vector<std::uint8_t> save = PortableGbaSave(file);
    std::size_t at = 0;
    if (EqualsNoCase(core, "VBA Next") && memory.size() == VBA_SIZE && file.size() != VBA_SIZE) {
        // VBA Next reads an EEPROM from 0x20000: a save that is one, or a larger file of an
        // EEPROM game (gpSP writes 128 KiB whatever the chip), goes there.
        const bool eeprom = save.size() <= EEPROM_SIZE || rom_says_eeprom;
        if (eeprom) {
            save.resize(std::min(save.size(), EEPROM_SIZE));
            at = EEPROM_AT;
        }
    }
    const std::size_t n = std::min(save.size(), memory.size() - std::min(at, memory.size()));
    std::copy_n(save.begin(), n, memory.begin() + at);
}

bool RomUsesEeprom(std::span<const std::uint8_t> rom) {
    static constexpr std::string_view TAG = "EEPROM_V";
    return std::search(rom.begin(), rom.end(), TAG.begin(), TAG.end()) != rom.end();
}

} // namespace den::states
