// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The neutral GBA machine a state is read into and written from: the hardware as a game
// sees it, with nothing of how one emulator schedules it.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::states::gba {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 EWRAM_SIZE = 0x40000;
constexpr u32 IWRAM_SIZE = 0x8000;
constexpr u32 VRAM_SIZE = 0x18000;
constexpr u32 OAM_SIZE = 0x400;
constexpr u32 PALETTE_SIZE = 0x400;
constexpr u32 IO_SIZE = 0x400;

// I/O registers (byte offsets from 0x04000000)
constexpr u32 REG_DISPSTAT = 0x004;
constexpr u32 REG_VCOUNT = 0x006;
constexpr u32 REG_SOUNDCNT_L = 0x080;
constexpr u32 REG_SOUNDCNT_H = 0x082;
constexpr u32 REG_SOUNDCNT_X = 0x084;
constexpr u32 REG_WAVE_RAM = 0x090;
constexpr u32 REG_DMA0SAD = 0x0B0;
constexpr u32 REG_TM0CNT_L = 0x100;
constexpr u32 REG_IE = 0x200;
constexpr u32 REG_IF = 0x202;
constexpr u32 REG_IME = 0x208;

constexpr u32 LINE_CYCLES = 1232;
constexpr u32 LINES = 228;
constexpr u32 VISIBLE_LINES = 160;

// ARM modes (CPSR bits 0-4)
constexpr u32 MODE_USER = 0x10;
constexpr u32 MODE_FIQ = 0x11;
constexpr u32 MODE_IRQ = 0x12;
constexpr u32 MODE_SVC = 0x13;
constexpr u32 MODE_ABT = 0x17;
constexpr u32 MODE_UND = 0x1B;
constexpr u32 MODE_SYSTEM = 0x1F;

// Register banks, as mGBA numbers them.
enum Bank { BANK_USER, BANK_FIQ, BANK_IRQ, BANK_SVC, BANK_ABT, BANK_UND, BANK_COUNT };
int BankOf(u32 mode); // -1: not a mode

enum class Bios {
    MGba,     // mGBA's own (mGBA and GBA DS without a BIOS file)
    Open,     // the open-source one gpSP builds in (Normmatt's)
    Official, // Nintendo's: nothing inside it can be moved to another BIOS
};

enum class SaveChip { Unknown, None, Sram, Flash64, Flash128, Eeprom512, Eeprom8k };

struct Cpu {
    // Every physical register: r0-r7, r8-r12 (user and FIQ copies), and the banked sp, lr
    // and spsr. The current mode's are not special.
    std::array<u32, 8> low{};
    std::array<u32, 5> high_user{};
    std::array<u32, 5> high_fiq{};
    std::array<u32, BANK_COUNT> sp{};
    std::array<u32, BANK_COUNT> lr{};
    std::array<u32, BANK_COUNT> spsr{}; // [BANK_USER] unused
    u32 pc = 0;                         // the next instruction to run
    u32 cpsr = MODE_SYSTEM;
    bool halted = false;

    u32 Mode() const {
        return cpsr & 0x1F;
    }
    bool Thumb() const {
        return (cpsr & 0x20) != 0;
    }
    /// r0-r15 as the current mode sees them (r15: the next instruction).
    u32 Get(int index) const;
    void Set(int index, u32 value);
    std::array<u32, 16> Visible() const;
    void SetVisible(const std::array<u32, 16>& r);
};

struct Timer {
    u16 counter = 0; // TMxCNT_L as read now
    u16 reload = 0;
    u16 control = 0;
};

struct Dma {
    u32 src = 0; // where the next unit comes from and goes to
    u32 dst = 0;
    u32 count = 0; // units left of a transfer that started, 0: none running
};

struct Fifo {
    std::array<u8, 36> bytes{}; // oldest first
    u32 size = 0;
};

struct Machine {
    Cpu cpu;
    std::vector<u8> ewram = std::vector<u8>(EWRAM_SIZE);
    std::vector<u8> iwram = std::vector<u8>(IWRAM_SIZE);
    std::vector<u8> vram = std::vector<u8>(VRAM_SIZE);
    std::vector<u8> oam = std::vector<u8>(OAM_SIZE);
    std::vector<u8> palette = std::vector<u8>(PALETTE_SIZE);
    std::vector<u8> io = std::vector<u8>(IO_SIZE); // as read; write-only ones as last written

    u32 line_cycle = 0; // cycles since the current line (REG_VCOUNT) started
    std::array<Timer, 4> timers{};
    std::array<Dma, 4> dma{};
    std::array<Fifo, 2> fifo{};
    std::array<u8, 32> wave_ram{}; // both banks, bank 0 first
    std::array<u8, 4> psg_volume{}; // current envelope volume of channels 1, 2, (3), 4

    SaveChip chip = SaveChip::Unknown;
    u8 flash_bank = 0;
    u8 gpio_data = 0, gpio_direction = 0, gpio_readable = 0;
    u8 rtc_control = 0x40;
    u32 bios_bus = 0;    // the last BIOS word fetched (what reading the BIOS returns)
    u32 frame = 0;

    Bios bios = Bios::MGba;
    std::string not_portable; // set by a reader: a part it cannot express

    u16 Io16(u32 reg) const {
        return static_cast<u16>(io[reg] | io[reg + 1] << 8);
    }
    void SetIo16(u32 reg, u16 value) {
        io[reg] = static_cast<u8>(value);
        io[reg + 1] = static_cast<u8>(value >> 8);
    }
    /// A word of EWRAM or IWRAM; false elsewhere.
    bool Read32(u32 address, u32* value) const;
    bool Write32(u32 address, u32 value);
};

// Little-endian helpers
inline u32 Load32(std::span<const u8> data, std::size_t at) {
    return static_cast<u32>(data[at]) | static_cast<u32>(data[at + 1]) << 8 | static_cast<u32>(data[at + 2]) << 16 |
           static_cast<u32>(data[at + 3]) << 24;
}
inline u16 Load16(std::span<const u8> data, std::size_t at) {
    return static_cast<u16>(data[at] | data[at + 1] << 8);
}
inline void Store32(std::span<u8> data, std::size_t at, u32 value) {
    data[at] = static_cast<u8>(value);
    data[at + 1] = static_cast<u8>(value >> 8);
    data[at + 2] = static_cast<u8>(value >> 16);
    data[at + 3] = static_cast<u8>(value >> 24);
}
inline void Store16(std::span<u8> data, std::size_t at, u16 value) {
    data[at] = static_cast<u8>(value);
    data[at + 1] = static_cast<u8>(value >> 8);
}

// gba_bios.cpp: the CPU out of the BIOS it ran in, so that the target's BIOS can take over.
// Fails (with the reason) where that cannot be done.
Status LeaveBios(Machine& m);
/// The target BIOS's address for an IRQ return or dispatcher address of `from`'s.
u32 IrqAddressFor(Bios from, Bios to, u32 address);
Status MoveIrqToBios(Machine& m, Bios to);
/// Runs a DMA that was started but has units left, for a target that cannot hold one midway.
Status FinishDmas(Machine& m);

// gba_mgba.cpp: mGBA's GBASerializedState (also GBA DS's whole state)
constexpr std::size_t MGBA_STATE_SIZE = 0x61000;
Result<Machine> ReadMgba(std::span<const u8> state);
Result<std::vector<u8>> WriteMgba(const Machine& m, std::span<const u8> target);

// gba_gpsp.cpp: gpSP's BSON document
Result<Machine> ReadGpsp(std::span<const u8> state);
Result<std::vector<u8>> WriteGpsp(const Machine& m, std::span<const u8> target);

} // namespace den::states::gba
