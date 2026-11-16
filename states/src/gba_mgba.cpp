// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// mGBA's GBASerializedState, version 11 (0x61000 bytes; layout in mGBA's
// include/mgba/internal/gba/serialize.h). GBA DS's libretro state is exactly this; mGBA's
// libretro core appends tagged extras (the save chip, the RTC override) that a reader skips.

#include <algorithm>

#include "gba_machine.h"

namespace den::states::gba {

namespace {

constexpr u32 MAGIC = 0x0100000B;
constexpr u32 OFFICIAL_BIOS_CHECKSUM = 0xBAAE187F;

// Offsets in the state
constexpr std::size_t VERSION = 0x000, BIOS_CHECKSUM = 0x004;
constexpr std::size_t GPRS = 0x020, CPSR = 0x060, SPSR = 0x064, CYCLES = 0x068, NEXT_EVENT = 0x06C;
constexpr std::size_t BANKED = 0x070, BANKED_SPSR = 0x118;
constexpr std::size_t PSG_FLAGS = 0x1DC;
constexpr std::size_t WAVE_BANKS = 0x154;
constexpr std::size_t FIFO_A = 0x18C, FIFO_B = 0x1AC, INTERNAL_A = 0x1CC, INTERNAL_B = 0x1D0;
constexpr std::size_t AUDIO_FLAGS = 0x1DA, AUDIO_FLAGS2 = 0x1E4;
constexpr std::size_t VIDEO_NEXT = 0x1F4, VIDEO_FLAGS = 0x1F8, FRAME = 0x1FC;
constexpr std::size_t TIMERS = 0x200, TIMER_SIZE = 0x14;
constexpr std::size_t DMAS = 0x250, DMA_SIZE = 0x10;
constexpr std::size_t PIN_STATE = 0x290, WRITE_LATCH = 0x291, PIN_DIRECTION = 0x292;
constexpr std::size_t RTC_BYTES = 0x294, RTC_BITS_READ = 0x29C, RTC_BITS = 0x2A0, RTC_ACTIVE = 0x2A4;
constexpr std::size_t RTC_COMMAND = 0x2A8, RTC_CONTROL = 0x2AC, HW_FLAGS1 = 0x2BE;
constexpr std::size_t SAVE_TYPE = 0x2E0, SAVE_COMMAND = 0x2E1, SAVE_FLAGS = 0x2E2, SAVE_READ_BITS = 0x2E3;
constexpr std::size_t SAVE_READ_ADDRESS = 0x2E8, SAVE_WRITE_ADDRESS = 0x2EC;
constexpr std::size_t BIOS_PREFETCH = 0x2F4, CPU_PREFETCH = 0x2F8, LAST_PREFETCH_PC = 0x318;
constexpr std::size_t MISC_FLAGS = 0x31C, NEXT_IRQ = 0x320, BIOS_STALL = 0x324;
constexpr std::size_t DMA_COUNT_LATCH = 0x3E0;
constexpr std::size_t IO = 0x400, PALETTE = 0x800, OAM = 0xC00, VRAM = 0x1000, IWRAM = 0x19000, EWRAM = 0x21000;

constexpr u32 HDRAW = 1008;
constexpr std::array<u32, 4> PRESCALE = {0, 6, 8, 10};

SaveChip ChipOf(int type) {
    switch (type) {
    case 0:
        return SaveChip::None;
    case 1:
    case 6:
        return SaveChip::Sram;
    case 2:
        return SaveChip::Flash64;
    case 3:
        return SaveChip::Flash128;
    case 4:
        return SaveChip::Eeprom8k;
    case 5:
        return SaveChip::Eeprom512;
    }
    return SaveChip::Unknown;
}

int TypeOf(SaveChip chip) {
    switch (chip) {
    case SaveChip::None:
        return 0;
    case SaveChip::Sram:
        return 1;
    case SaveChip::Flash64:
        return 2;
    case SaveChip::Flash128:
        return 3;
    case SaveChip::Eeprom8k:
        return 4;
    case SaveChip::Eeprom512:
        return 5;
    case SaveChip::Unknown:
        break;
    }
    return -1;
}

void ReadFifo(std::span<const u8> s, std::size_t words, u32 internal, u32 word_count, u32 internal_left, Fifo& fifo) {
    fifo.size = 0;
    for (u32 i = 0; i < internal_left && i < 4; ++i) {
        fifo.bytes[fifo.size++] = static_cast<u8>(internal >> (8 * i));
    }
    for (u32 w = 0; w < word_count && w < 8; ++w) {
        for (u32 b = 0; b < 4; ++b) {
            fifo.bytes[fifo.size++] = s[words + 4 * w + b];
        }
    }
}

// Back into words and the part-played word; returns the word count and the bytes left over.
std::pair<u32, u32> WriteFifo(std::span<u8> s, std::size_t words, std::size_t internal, const Fifo& fifo) {
    const u32 loose = fifo.size % 4;
    u32 value = 0;
    for (u32 i = 0; i < loose; ++i) {
        value |= static_cast<u32>(fifo.bytes[i]) << (8 * i);
    }
    Store32(s, internal, value);
    const u32 count = std::min<u32>((fifo.size - loose) / 4, 8);
    std::fill(s.begin() + words, s.begin() + words + 32, u8{0});
    for (u32 w = 0; w < count; ++w) {
        for (u32 b = 0; b < 4; ++b) {
            s[words + 4 * w + b] = fifo.bytes[loose + 4 * w + b];
        }
    }
    return {count, loose};
}

} // namespace

Result<Machine> ReadMgba(std::span<const u8> s) {
    if (s.size() < MGBA_STATE_SIZE || Load32(s, VERSION) != MAGIC) {
        return Result<Machine>::Fail("not an mGBA state of the version this EmuDEN knows");
    }
    Machine m;
    m.bios = Load32(s, BIOS_CHECKSUM) == OFFICIAL_BIOS_CHECKSUM ? Bios::Official : Bios::MGba;

    Cpu& cpu = m.cpu;
    cpu.cpsr = Load32(s, CPSR);
    const int bank = BankOf(cpu.Mode());
    if (bank < 0) {
        return Result<Machine>::Fail("the CPU is in no mode");
    }
    for (int b = 0; b < BANK_COUNT; ++b) {
        cpu.sp[b] = Load32(s, BANKED + 28 * b);
        cpu.lr[b] = Load32(s, BANKED + 28 * b + 4);
        cpu.spsr[b] = Load32(s, BANKED_SPSR + 4 * b);
    }
    for (int i = 0; i < 5; ++i) {
        cpu.high_user[i] = Load32(s, BANKED + 8 + 4 * i);
        cpu.high_fiq[i] = Load32(s, BANKED + 28 + 8 + 4 * i);
    }
    cpu.spsr[bank] = Load32(s, SPSR);
    std::array<u32, 16> r{};
    for (int i = 0; i < 16; ++i) {
        r[i] = Load32(s, GPRS + 4 * i);
    }
    r[15] -= cpu.Thumb() ? 2 : 4; // mGBA's pc is the one being fetched
    cpu.SetVisible(r);
    const u32 misc = Load32(s, MISC_FLAGS);
    cpu.halted = (misc & 1) != 0;

    std::copy_n(s.begin() + EWRAM, EWRAM_SIZE, m.ewram.begin());
    std::copy_n(s.begin() + IWRAM, IWRAM_SIZE, m.iwram.begin());
    std::copy_n(s.begin() + VRAM, VRAM_SIZE, m.vram.begin());
    std::copy_n(s.begin() + OAM, OAM_SIZE, m.oam.begin());
    std::copy_n(s.begin() + PALETTE, PALETTE_SIZE, m.palette.begin());
    std::copy_n(s.begin() + IO, IO_SIZE, m.io.begin());

    // Times are kept relative to now.
    const u32 next = Load32(s, VIDEO_NEXT);
    const u32 mode = Load32(s, VIDEO_FLAGS) & 3;
    const bool in_hblank = mode == 1 || (mode == 0 && (m.Io16(REG_DISPSTAT) & 2));
    const u32 line_end = in_hblank ? LINE_CYCLES : HDRAW;
    m.line_cycle = next >= line_end ? 0 : line_end - next;
    m.frame = Load32(s, FRAME);

    for (int i = 0; i < 4; ++i) {
        const std::size_t t = TIMERS + TIMER_SIZE * i;
        Timer& timer = m.timers[i];
        timer.reload = Load16(s, t);
        timer.control = m.Io16(REG_TM0CNT_L + 4 * i + 2);
        const u32 flags = Load32(s, t + 16);
        u32 counter = m.Io16(REG_TM0CNT_L + 4 * i);
        if ((flags & 0x40) && !(flags & 0x10)) {
            const auto since = -static_cast<std::int32_t>(Load32(s, t + 4));
            counter += static_cast<u32>(std::max(since, 0)) >> (flags & 0xF);
            while (counter >= 0x10000) {
                counter -= 0x10000 - timer.reload;
            }
        }
        timer.counter = static_cast<u16>(counter);
    }
    for (int i = 0; i < 4; ++i) {
        const std::size_t d = DMAS + DMA_SIZE * i;
        Dma& dma = m.dma[i];
        dma.src = Load32(s, d);
        dma.dst = Load32(s, d + 4);
        const bool on = (m.Io16(REG_DMA0SAD + 12 * i + 10) & 0x8000) != 0;
        dma.count = on ? Load32(s, d + 8) : 0;
        // The address registers read back as open bus: what they last held is where the
        // DMA got to.
        Store32(m.io, REG_DMA0SAD + 12 * i, dma.src);
        Store32(m.io, REG_DMA0SAD + 12 * i + 4, dma.dst);
    }

    const u16 audio = Load16(s, AUDIO_FLAGS);
    ReadFifo(s, FIFO_A, Load32(s, INTERNAL_A), (audio >> 7) & 7, (audio >> 5) & 3, m.fifo[0]);
    ReadFifo(s, FIFO_B, Load32(s, INTERNAL_B), (audio >> 2) & 7, audio & 3, m.fifo[1]);
    std::copy_n(s.begin() + WAVE_BANKS, 32, m.wave_ram.begin());
    m.psg_volume = {static_cast<u8>(s[PSG_FLAGS] & 0xF), static_cast<u8>(s[PSG_FLAGS + 1] & 0xF), 0,
                    static_cast<u8>(s[PSG_FLAGS + 2] & 0xF)};

    m.chip = ChipOf(static_cast<std::int8_t>(s[SAVE_TYPE]));
    m.flash_bank = (s[SAVE_FLAGS] >> 4) & 1;
    if (s[SAVE_COMMAND] != 0 || (s[SAVE_FLAGS] & 3) != 0 || s[SAVE_READ_BITS] != 0 || (s[SAVE_FLAGS] & 0x20)) {
        m.not_portable = "the save chip is midway through a command";
    }
    m.gpio_data = s[PIN_STATE] & 0xF;
    m.gpio_direction = s[PIN_DIRECTION] & 0xF;
    m.gpio_readable = Load16(s, HW_FLAGS1) & 1;
    m.rtc_control = s[RTC_CONTROL];
    if (Load32(s, RTC_ACTIVE) != 0 || Load32(s, RTC_BYTES) != 0) {
        m.not_portable = "the clock chip is midway through a command";
    }
    m.bios_bus = Load32(s, BIOS_PREFETCH);
    return m;
}

Result<std::vector<u8>> WriteMgba(const Machine& m, std::span<const u8> target) {
    if (target.size() < MGBA_STATE_SIZE || Load32(target, VERSION) != MAGIC) {
        return Result<std::vector<u8>>::Fail("the running core's state is not one this EmuDEN knows");
    }
    // The struct and an empty list of extras after it.
    std::vector<u8> out(MGBA_STATE_SIZE + 16, 0);
    std::copy_n(target.begin(), MGBA_STATE_SIZE, out.begin());
    const std::span<u8> s(out);

    const Cpu& cpu = m.cpu;
    const int bank = BankOf(cpu.Mode());
    std::array<u32, 16> r = cpu.Visible();
    r[15] = cpu.pc + (cpu.Thumb() ? 2 : 4);
    for (int i = 0; i < 16; ++i) {
        Store32(s, GPRS + 4 * i, r[i]);
    }
    Store32(s, CPSR, cpu.cpsr);
    Store32(s, SPSR, cpu.spsr[std::max(bank, 0)]);
    Store32(s, CYCLES, 0);
    Store32(s, NEXT_EVENT, 0);
    for (int b = 0; b < BANK_COUNT; ++b) {
        Store32(s, BANKED + 28 * b, cpu.sp[b]);
        Store32(s, BANKED + 28 * b + 4, cpu.lr[b]);
        Store32(s, BANKED_SPSR + 4 * b, cpu.spsr[b]);
    }
    for (int i = 0; i < 5; ++i) {
        Store32(s, BANKED + 8 + 4 * i, cpu.high_user[i]);
        Store32(s, BANKED + 28 + 8 + 4 * i, cpu.high_fiq[i]);
    }
    // The two instructions in the pipeline. Left at 0, mGBA fetches them again, but before it
    // has loaded RAM from the state: right for the cartridge and the BIOS only.
    const u32 width = cpu.Thumb() ? 2 : 4;
    u32 first = 0, second = 0;
    if (m.Read32(cpu.pc, &first) && m.Read32(cpu.pc + width, &second) && cpu.Thumb()) {
        first = (cpu.pc & 2) ? first >> 16 : first & 0xFFFF;
        second = ((cpu.pc + 2) & 2) ? second >> 16 : second & 0xFFFF;
    }
    Store32(s, CPU_PREFETCH, first);
    Store32(s, CPU_PREFETCH + 4, second);
    Store32(s, LAST_PREFETCH_PC, 0);
    if (m.bios_bus != 0) {
        Store32(s, BIOS_PREFETCH, m.bios_bus);
    }
    const bool irq = m.Io16(REG_IME) & 1 && (m.Io16(REG_IE) & m.Io16(REG_IF) & 0x3FFF) && !(cpu.cpsr & 0x80);
    Store32(s, MISC_FLAGS, (cpu.halted ? 1u : 0u) | 2u | (irq ? 4u : 0u) | 0x400u << 4); // no keypad IRQ keys yet
    Store32(s, NEXT_IRQ, 0);
    Store32(s, BIOS_STALL, 0);

    std::copy(m.ewram.begin(), m.ewram.end(), s.begin() + EWRAM);
    std::copy(m.iwram.begin(), m.iwram.end(), s.begin() + IWRAM);
    std::copy(m.vram.begin(), m.vram.end(), s.begin() + VRAM);
    std::copy(m.oam.begin(), m.oam.end(), s.begin() + OAM);
    std::copy(m.palette.begin(), m.palette.end(), s.begin() + PALETTE);
    // mGBA keeps the memory control register (0x4000800) at 0x210, where other cores have
    // nothing: the running core's stays.
    std::copy_n(m.io.begin(), 0x210, s.begin() + IO);
    if (std::any_of(m.io.begin() + 0x210, m.io.begin() + 0x214, [](u8 b) { return b != 0; })) {
        std::copy_n(m.io.begin() + 0x210, 4, s.begin() + IO + 0x210);
    }
    std::copy(m.io.begin() + 0x214, m.io.end(), s.begin() + IO + 0x214);

    const u32 vcount = m.Io16(REG_VCOUNT);
    const bool hblank = m.line_cycle >= HDRAW;
    Store32(s, VIDEO_NEXT, hblank ? LINE_CYCLES - m.line_cycle : HDRAW - m.line_cycle);
    Store32(s, VIDEO_FLAGS, hblank ? 1 : 2);
    Store32(s, FRAME, m.frame);
    u16 dispstat = m.Io16(REG_DISPSTAT) & 0xFFB8;
    if (vcount >= VISIBLE_LINES && vcount < LINES - 1) {
        dispstat |= 1;
    }
    if (hblank) {
        dispstat |= 2;
    }
    if (vcount == static_cast<u32>(dispstat >> 8)) {
        dispstat |= 4;
    }
    Store16(s, IO + REG_DISPSTAT, dispstat);
    Store16(s, IO + 0x130, 0x3FF); // no keys held

    for (int i = 0; i < 4; ++i) {
        const std::size_t t = TIMERS + TIMER_SIZE * i;
        const Timer& timer = m.timers[i];
        const u32 prescale = PRESCALE[timer.control & 3];
        const bool count_up = i > 0 && (timer.control & 4);
        const bool on = (timer.control & 0x80) != 0;
        u32 flags = prescale | (count_up ? 0x10u : 0u) | ((timer.control & 0x40) ? 0x20u : 0u) | (on ? 0x40u : 0u);
        Store16(s, t, timer.reload);
        Store32(s, t + 4, 0);
        Store32(s, t + 8, on && !count_up ? (0x10000u - timer.counter) << prescale : 0u);
        Store32(s, t + 16, flags);
        Store16(s, IO + REG_TM0CNT_L + 4 * i, timer.counter);
        Store16(s, IO + REG_TM0CNT_L + 4 * i + 2, timer.control);
    }
    for (int i = 0; i < 4; ++i) {
        const std::size_t d = DMAS + DMA_SIZE * i;
        Store32(s, d, m.dma[i].src);
        Store32(s, d + 4, m.dma[i].dst);
        Store32(s, d + 8, m.dma[i].count);
        Store32(s, d + 12, 0);
        Store16(s, DMA_COUNT_LATCH + 2 * i, m.Io16(REG_DMA0SAD + 12 * i + 8));
    }

    const auto [words_a, loose_a] = WriteFifo(s, FIFO_A, INTERNAL_A, m.fifo[0]);
    const auto [words_b, loose_b] = WriteFifo(s, FIFO_B, INTERNAL_B, m.fifo[1]);
    Store16(s, AUDIO_FLAGS, static_cast<u16>(loose_b | words_b << 2 | loose_a << 5 | words_a << 7));
    u32 flags2 = Load32(s, AUDIO_FLAGS2) & 0xF;
    for (int fifo = 0; fifo < 2; ++fifo) {
        u32 source = 0; // none
        for (int i = 1; i <= 2; ++i) {
            if (m.dma[i].dst == 0x040000A0u + 4 * fifo || Load32(m.io, REG_DMA0SAD + 12 * i + 4) == 0x040000A0u + 4 * fifo) {
                source = i;
            }
        }
        flags2 |= (source + 1) << (4 + 2 * fifo);
    }
    Store32(s, AUDIO_FLAGS2, flags2);
    std::copy(m.wave_ram.begin(), m.wave_ram.end(), s.begin() + WAVE_BANKS);

    const int known = static_cast<std::int8_t>(s[SAVE_TYPE]);
    const int type = known >= 0 ? known : TypeOf(m.chip);
    s[SAVE_TYPE] = static_cast<u8>(type);
    s[SAVE_COMMAND] = 0;
    s[SAVE_FLAGS] = static_cast<u8>(type == 3 && m.flash_bank ? 0x10 : 0);
    s[SAVE_READ_BITS] = 0;
    Store32(s, SAVE_READ_ADDRESS, 0);
    Store32(s, SAVE_WRITE_ADDRESS, 0);

    s[PIN_STATE] = m.gpio_data;
    s[WRITE_LATCH] = m.gpio_data;
    s[PIN_DIRECTION] = m.gpio_direction;
    Store16(s, HW_FLAGS1, static_cast<u16>((Load16(s, HW_FLAGS1) & ~1u) | (m.gpio_readable & 1)));
    for (const std::size_t field : {RTC_BYTES, RTC_BITS_READ, RTC_BITS, RTC_ACTIVE, RTC_COMMAND}) {
        Store32(s, field, 0);
    }
    s[RTC_CONTROL] = m.rtc_control;
    return out;
}

} // namespace den::states::gba
