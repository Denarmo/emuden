// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// gpSP's state: one BSON document of 416 KiB (gpSP's savestate.c and the *_write_savestate
// functions), version 0x10004. Writing patches a state of the running gpSP in place: every
// field has a fixed size, and what is gpSP's own (sound mixing, the RTC's base time, the
// flash chip's id) stays as that state has it.

#include <algorithm>
#include <optional>

#include "gba_machine.h"

namespace den::states::gba {

namespace {

constexpr u32 MAGIC = 0x06BAC0DE;
constexpr u32 VERSION = 0x00010004;
constexpr std::size_t STATE_SIZE = 416 * 1024;
constexpr u32 HDRAW = 960;
constexpr std::array<u32, 4> PRESCALE = {0, 6, 8, 10};

constexpr u8 TYPE_DOC = 0x03, TYPE_ARRAY = 0x04, TYPE_BIN = 0x05, TYPE_INT = 0x10;

// gpSP's mode numbers and its banks (reg_mode rows, spsr entries: mode & 0xF)
constexpr u32 GPSP_USER = 0x00, GPSP_SYSTEM = 0x10, GPSP_IRQ = 0x11, GPSP_FIQ = 0x12, GPSP_SVC = 0x13,
              GPSP_ABT = 0x14, GPSP_UND = 0x15;
constexpr std::array<int, 6> ROW_OF_BANK = {0, 2, 1, 3, 4, 5}; // indexed by Bank

u32 GpspMode(u32 mode) {
    switch (mode) {
    case MODE_USER:
        return GPSP_USER;
    case MODE_FIQ:
        return GPSP_FIQ;
    case MODE_IRQ:
        return GPSP_IRQ;
    case MODE_SVC:
        return GPSP_SVC;
    case MODE_ABT:
        return GPSP_ABT;
    case MODE_UND:
        return GPSP_UND;
    }
    return GPSP_SYSTEM;
}

// A document inside the state: the offset of its size word.
template <typename Byte>
class Doc {
public:
    Doc(std::span<Byte> data, std::size_t at) : data_(data), at_(at) {}

    // The offset and type of `key`'s value.
    std::optional<std::pair<std::size_t, u8>> Find(std::string_view key) const {
        if (at_ + 4 > data_.size()) {
            return std::nullopt;
        }
        const std::size_t end = std::min<std::size_t>(at_ + Load32(data_, at_), data_.size());
        std::size_t p = at_ + 4;
        while (p < end && data_[p] != 0) {
            const u8 type = data_[p];
            const std::size_t name = p + 1;
            std::size_t zero = name;
            while (zero < end && data_[zero] != 0) {
                ++zero;
            }
            if (zero >= end) {
                return std::nullopt;
            }
            const std::size_t value = zero + 1;
            const std::string_view found(reinterpret_cast<const char*>(&data_[name]), zero - name);
            if (found == key) {
                return std::pair{value, type};
            }
            if (type == TYPE_DOC || type == TYPE_ARRAY) {
                p = value + Load32(data_, value);
            } else if (type == TYPE_BIN) {
                p = value + 5 + Load32(data_, value);
            } else if (type == TYPE_INT) {
                p = value + 4;
            } else {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    std::optional<Doc> Child(std::string_view key) const {
        const auto found = Find(key);
        if (!found || (found->second != TYPE_DOC && found->second != TYPE_ARRAY)) {
            return std::nullopt;
        }
        return Doc(data_, found->first);
    }
    std::optional<std::size_t> Int(std::string_view key) const {
        const auto found = Find(key);
        if (!found || found->second != TYPE_INT) {
            return std::nullopt;
        }
        return found->first;
    }
    // The bytes of a binary field, when it has `size`.
    std::optional<std::size_t> Bin(std::string_view key, std::size_t size) const {
        const auto found = Find(key);
        if (!found || found->second != TYPE_BIN || Load32(data_, found->first) != size) {
            return std::nullopt;
        }
        return found->first + 5;
    }

private:
    std::span<Byte> data_;
    std::size_t at_;
};

std::string Index(int i) {
    return {static_cast<char>('0' + i / 10), static_cast<char>('0' + i % 10)};
}

// Reads fields, remembering the first that is missing.
struct Reader {
    std::span<const u8> s;
    bool ok = true;

    u32 Int(const std::optional<Doc<const u8>>& doc, std::string_view key) {
        const auto at = doc ? doc->Int(key) : std::nullopt;
        ok = ok && at.has_value();
        return at ? Load32(s, *at) : 0;
    }
    void Bin(const std::optional<Doc<const u8>>& doc, std::string_view key, std::span<u8> out) {
        const auto at = doc ? doc->Bin(key, out.size()) : std::nullopt;
        ok = ok && at.has_value();
        if (at) {
            std::copy_n(s.begin() + *at, out.size(), out.begin());
        }
    }
};

struct Writer {
    std::span<u8> s;
    bool ok = true;

    u32 Get(const std::optional<Doc<u8>>& doc, std::string_view key) {
        const auto at = doc ? doc->Int(key) : std::nullopt;
        ok = ok && at.has_value();
        return at ? Load32(s, *at) : 0;
    }
    void Int(const std::optional<Doc<u8>>& doc, std::string_view key, u32 value) {
        const auto at = doc ? doc->Int(key) : std::nullopt;
        ok = ok && at.has_value();
        if (at) {
            Store32(s, *at, value);
        }
    }
    void Bin(const std::optional<Doc<u8>>& doc, std::string_view key, std::span<const u8> in) {
        const auto at = doc ? doc->Bin(key, in.size()) : std::nullopt;
        ok = ok && at.has_value();
        if (at) {
            std::copy(in.begin(), in.end(), s.begin() + *at);
        }
    }
};

template <typename Byte>
bool IsGpsp(std::span<Byte> s) {
    if (s.size() < STATE_SIZE || Load32(s, 0) != STATE_SIZE) {
        return false;
    }
    const Doc<Byte> root(s, 0);
    const auto magic = root.Int("info-magic");
    const auto version = root.Int("info-version");
    return magic && version && Load32(s, *magic) == MAGIC && Load32(s, *version) == VERSION;
}

// gpSP mixes at 2^15 or 2^16 Hz (its gpsp_sound_rate option) and keeps steps derived from
// that, which it does not work out again after a load. The running gpSP's state tells which:
// a timer feeding sound has its step, and the PSG clock's fraction advances by 2^(bits - 8)
// per CPU cycle.
u32 SoundBits(Writer& w, const Doc<u8>& root) {
    const auto timers = root.Child("timers");
    for (int i = 0; i < 2; ++i) {
        const auto t = timers ? timers->Child(Index(i).substr(1)) : std::nullopt;
        const u64 step = w.Get(t, "freq-step");
        const u64 reload = static_cast<u64>(w.Get(t, "reload")) << w.Get(t, "prescale");
        if (step != 0 && reload != 0 && w.Get(t, "status") != 0) {
            const u64 rate = (u64{1} << 48) / (step * reload);
            return rate > 49152 ? 16 : 15;
        }
    }
    const auto sound = root.Child("sound");
    const u32 ticks = w.Get(sound, "gbc-last-cpu-ticks");
    const u32 partial = w.Get(sound, "gbc-partial-ticks");
    const bool fits16 = ((ticks << 8) & 0xFFFF) == partial;
    const bool fits15 = ((ticks << 7) & 0xFFFF) == partial;
    return fits16 && !fits15 ? 16 : 15;
}

} // namespace

Result<Machine> ReadGpsp(std::span<const u8> s) {
    if (!IsGpsp(s)) {
        return Result<Machine>::Fail("not a gpSP state of the version this EmuDEN knows");
    }
    Reader r{s};
    const Doc<const u8> root(s, 0);
    Machine m;
    m.bios = Bios::Open;
    Cpu& cpu = m.cpu;

    const auto cpu_doc = root.Child("cpu");
    const auto regs = cpu_doc ? cpu_doc->Child("regs") : std::nullopt;
    const auto regmod = cpu_doc ? cpu_doc->Child("regmod") : std::nullopt;
    const auto spsr = cpu_doc ? cpu_doc->Child("spsr") : std::nullopt;
    std::array<u32, 19> reg{};
    for (int i = 0; i < 19; ++i) {
        reg[i] = r.Int(regs, Index(i));
    }
    cpu.cpsr = reg[16];
    const int bank = BankOf(cpu.Mode());
    if (bank < 0) {
        return Result<Machine>::Fail("the CPU is in no mode");
    }
    if (bank == BANK_FIQ) {
        m.not_portable = "the CPU is in FIQ mode";
    }
    for (int b = 0; b < BANK_COUNT; ++b) {
        const int row = ROW_OF_BANK[b];
        cpu.sp[b] = r.Int(regmod, Index(row * 7 + 5));
        cpu.lr[b] = r.Int(regmod, Index(row * 7 + 6));
        if (b != BANK_USER) {
            cpu.spsr[b] = r.Int(spsr, Index(row));
        }
    }
    for (int i = 0; i < 5; ++i) {
        cpu.high_fiq[i] = r.Int(regmod, Index(ROW_OF_BANK[BANK_FIQ] * 7 + i));
    }
    std::array<u32, 16> visible{};
    std::copy_n(reg.begin(), 16, visible.begin());
    cpu.SetVisible(visible); // the current mode's registers are live in reg[]
    cpu.halted = reg[18] == 1;
    if (reg[18] == 2) {
        m.not_portable = "the CPU is stopped";
    }
    m.bios_bus = r.Int(cpu_doc, "bus-value");

    const auto memory = root.Child("memory");
    r.Bin(memory, "iwram", m.iwram);
    r.Bin(memory, "ewram", m.ewram);
    r.Bin(memory, "vram", m.vram);
    r.Bin(memory, "oamram", m.oam);
    r.Bin(memory, "palram", m.palette);
    r.Bin(memory, "ioregs", m.io);

    const auto emu = root.Child("emu");
    const auto video_count = static_cast<std::int32_t>(r.Int(emu, "video-count"));
    const bool hblank = (m.Io16(REG_DISPSTAT) & 2) != 0;
    const std::int32_t line_cycle = (hblank ? static_cast<std::int32_t>(LINE_CYCLES) : HDRAW) - video_count;
    m.line_cycle = static_cast<u32>(std::clamp<std::int32_t>(line_cycle, 0, LINE_CYCLES - 1));
    m.frame = r.Int(emu, "frame-count");

    const auto timers = root.Child("timers");
    for (int i = 0; i < 4; ++i) {
        const auto t = timers ? timers->Child(Index(i).substr(1)) : std::nullopt;
        Timer& timer = m.timers[i];
        const u32 count = r.Int(t, "count");
        const u32 prescale = r.Int(t, "prescale");
        const u32 status = r.Int(t, "status");
        timer.reload = static_cast<u16>(0x10000 - r.Int(t, "reload"));
        timer.control = m.Io16(REG_TM0CNT_L + 4 * i + 2);
        if (status == 1) {
            timer.counter = static_cast<u16>(0x10000 - (count >> prescale));
        } else if (status == 2) {
            timer.counter = static_cast<u16>(0x10000 - count);
        } else {
            timer.counter = m.Io16(REG_TM0CNT_L + 4 * i);
        }
    }
    const auto dmas = root.Child("dma");
    for (int i = 0; i < 4; ++i) {
        const auto d = dmas ? dmas->Child(Index(i).substr(1)) : std::nullopt;
        m.dma[i].src = r.Int(d, "src-addr");
        m.dma[i].dst = r.Int(d, "dst-addr");
        m.dma[i].count = 0; // gpSP runs a DMA whole when it starts
    }

    const auto sound = root.Child("sound");
    for (int c = 0; c < 2; ++c) {
        const auto ds = sound ? sound->Child(c ? "ds1" : "ds0") : std::nullopt;
        std::array<u8, 32> ring{};
        r.Bin(ds, "fifo-bytes", ring);
        const u32 base = r.Int(ds, "fifo-base") & 31;
        const u32 top = r.Int(ds, "fifo-top") & 31;
        Fifo& fifo = m.fifo[c];
        fifo.size = (top - base) & 31;
        for (u32 i = 0; i < fifo.size; ++i) {
            fifo.bytes[i] = ring[(base + i) & 31];
        }
    }
    std::array<u8, 64> samples{};
    r.Bin(sound, "wav-samples", samples);
    for (int i = 0; i < 32; ++i) {
        m.wave_ram[i] = static_cast<u8>(((samples[2 * i] + 8) & 0xF) << 4 | ((samples[2 * i + 1] + 8) & 0xF));
    }
    for (int c : {0, 1, 3}) {
        const auto gs = sound ? sound->Child("gs" + std::to_string(c)) : std::nullopt;
        m.psg_volume[c] = static_cast<u8>(r.Int(gs, "env-vol") & 0xF);
    }

    const auto backup = root.Child("backup");
    switch (r.Int(backup, "backup-type")) {
    case 0:
        m.chip = SaveChip::Sram;
        break;
    case 1:
        m.chip = r.Int(backup, "flash-size") == 2 ? SaveChip::Flash128 : SaveChip::Flash64;
        break;
    case 2:
        m.chip = r.Int(backup, "eeprom-size") == 16 ? SaveChip::Eeprom8k : SaveChip::Eeprom512;
        break;
    default:
        m.chip = SaveChip::Unknown;
    }
    m.flash_bank = static_cast<u8>(r.Int(backup, "flash-bank-num") & 1);
    if (r.Int(backup, "flash-mode") != 0 || r.Int(backup, "eeprom-mode") != 0) {
        m.not_portable = "the save chip is midway through a command";
    }
    std::array<u8, 3> gpio{};
    r.Bin(backup, "gpio-regs", gpio);
    m.gpio_data = gpio[0] & 0xF;
    m.gpio_direction = gpio[1] & 0xF;
    m.gpio_readable = gpio[2] & 1;
    m.rtc_control = static_cast<u8>(r.Int(backup, "rtc-status"));
    if (r.Int(backup, "rtc-state") >= 2 && (m.gpio_data & 4)) {
        m.not_portable = "the clock chip is midway through a command";
    }
    if (!r.ok) {
        return Result<Machine>::Fail("the gpSP state is missing fields");
    }
    return m;
}

Result<std::vector<u8>> WriteGpsp(const Machine& m, std::span<const u8> target) {
    if (!IsGpsp(target)) {
        return Result<std::vector<u8>>::Fail("the running core's state is not one this EmuDEN knows");
    }
    std::vector<u8> out(target.begin(), target.begin() + STATE_SIZE);
    const std::span<u8> s(out);
    Writer w{s};
    const Doc<u8> root(s, 0);
    const Cpu& cpu = m.cpu;

    const auto cpu_doc = root.Child("cpu");
    const auto regs = cpu_doc ? cpu_doc->Child("regs") : std::nullopt;
    const auto regmod = cpu_doc ? cpu_doc->Child("regmod") : std::nullopt;
    const auto spsr = cpu_doc ? cpu_doc->Child("spsr") : std::nullopt;
    const std::array<u32, 16> visible = cpu.Visible();
    for (int i = 0; i < 16; ++i) {
        w.Int(regs, Index(i), visible[i]);
    }
    w.Int(regs, Index(16), cpu.cpsr);
    w.Int(regs, Index(17), GpspMode(cpu.Mode()));
    w.Int(regs, Index(18), cpu.halted ? 1 : 0);
    for (int b = 0; b < BANK_COUNT; ++b) {
        const int row = ROW_OF_BANK[b];
        for (int i = 0; i < 5; ++i) {
            w.Int(regmod, Index(row * 7 + i), b == BANK_FIQ ? cpu.high_fiq[i] : cpu.high_user[i]);
        }
        w.Int(regmod, Index(row * 7 + 5), cpu.sp[b]);
        w.Int(regmod, Index(row * 7 + 6), cpu.lr[b]);
        if (b != BANK_USER) {
            w.Int(spsr, Index(row), cpu.spsr[b]);
        }
    }
    if (m.bios_bus != 0) {
        w.Int(cpu_doc, "bus-value", m.bios_bus);
    }

    std::vector<u8> io = m.io;
    const auto io16 = [&](u32 reg) { return static_cast<u16>(io[reg] | io[reg + 1] << 8); };
    const auto set16 = [&](u32 reg, u16 value) { Store16(io, reg, value); };
    const u32 vcount = io16(REG_VCOUNT);
    const bool hblank = m.line_cycle >= HDRAW;
    u16 dispstat = io16(REG_DISPSTAT) & 0xFFB8;
    if (vcount >= VISIBLE_LINES && vcount < LINES - 1) {
        dispstat |= 1;
    }
    if (hblank) {
        dispstat |= 2;
    }
    if (vcount == static_cast<u32>(dispstat >> 8)) {
        dispstat |= 4;
    }
    set16(REG_DISPSTAT, dispstat);
    set16(0x130, 0x3FF);
    for (int i = 0; i < 4; ++i) {
        set16(REG_TM0CNT_L + 4 * i, m.timers[i].counter);
        set16(REG_TM0CNT_L + 4 * i + 2, m.timers[i].control);
    }
    const u16 sound_h = io16(REG_SOUNDCNT_H);
    const u8 bank = (io[0x70] >> 6) & 1;
    std::copy_n(m.wave_ram.begin() + 16 * bank, 16, io.begin() + REG_WAVE_RAM);

    const auto memory = root.Child("memory");
    w.Bin(memory, "iwram", m.iwram);
    w.Bin(memory, "ewram", m.ewram);
    w.Bin(memory, "vram", m.vram);
    w.Bin(memory, "oamram", m.oam);
    w.Bin(memory, "palram", m.palette);
    w.Bin(memory, "ioregs", io);

    const u32 bits = SoundBits(w, root);
    const u32 video_count = hblank ? LINE_CYCLES - m.line_cycle : HDRAW - m.line_cycle;
    u32 exec = video_count;
    const auto timers = root.Child("timers");
    for (int i = 0; i < 4; ++i) {
        const auto t = timers ? timers->Child(Index(i).substr(1)) : std::nullopt;
        const Timer& timer = m.timers[i];
        const u32 prescale = PRESCALE[timer.control & 3];
        const bool on = (timer.control & 0x80) != 0;
        const bool cascade = i > 0 && (timer.control & 4);
        const u32 left = 0x10000u - timer.counter;
        const u32 count = cascade ? left : left << prescale;
        w.Int(t, "count", on ? count : 0x10000);
        w.Int(t, "reload", 0x10000u - timer.reload);
        w.Int(t, "prescale", prescale);
        w.Int(t, "status", !on ? 0 : cascade ? 2 : 1);
        w.Int(t, "irq", (timer.control >> 6) & 1);
        u32 channels = 0;
        if (i < 2) {
            const u32 side = i; // which timer feeds FIFO A (bit 10) and B (bit 14)
            channels = ((sound_h >> 10 & 1u) == side ? 1u : 0u) | ((sound_h >> 14 & 1u) == side ? 2u : 0u);
        }
        w.Int(t, "dsc", channels);
        const u64 period = static_cast<u64>(0x10000u - timer.reload) << prescale;
        if (i < 2 && period != 0) {
            w.Int(t, "freq-step", static_cast<u32>((u64{1} << 48) / ((u64{1} << bits) * period)));
        }
        if (on && !cascade) {
            exec = std::min(exec, count);
        }
    }
    const auto emu = root.Child("emu");
    w.Int(emu, "video-count", video_count);
    w.Int(emu, "exec-cycles", exec);
    w.Int(emu, "sleep-cycles", 0);
    w.Int(emu, "frame-count", m.frame);

    const auto dmas = root.Child("dma");
    for (int i = 0; i < 4; ++i) {
        const auto d = dmas ? dmas->Child(Index(i).substr(1)) : std::nullopt;
        const u16 control = m.Io16(REG_DMA0SAD + 12 * i + 10);
        const u32 timing = (control >> 12) & 3;
        // As gpSP's trigger_dma sets one up: a sound FIFO DMA moves four words to a fixed
        // address.
        const bool fifo = timing == 3 && (i == 1 || i == 2);
        u32 length = m.Io16(REG_DMA0SAD + 12 * i + 8) & (i == 3 ? 0xFFFF : 0x3FFF);
        if (length == 0) {
            length = i == 3 ? 0x10000 : 0x4000;
        }
        w.Int(d, "src-addr", m.dma[i].src);
        w.Int(d, "dst-addr", m.dma[i].dst);
        w.Int(d, "src-dir", (control >> 7) & 3);
        w.Int(d, "dst-dir", fifo ? 2 : (control >> 5) & 3);
        w.Int(d, "len", fifo ? 4 : length);
        w.Int(d, "size", fifo ? 1 : (control >> 10) & 1);
        w.Int(d, "repeat", (control >> 9) & 1);
        w.Int(d, "start", (control & 0x8000) ? timing : 4);
        w.Int(d, "dsc", !fifo ? 2 : (m.dma[i].dst & 0x0FFFFFFF) == 0x040000A4 ? 1 : 0);
        w.Int(d, "irq", (control >> 14) & 1);
    }

    const auto backup = root.Child("backup");
    switch (m.chip) {
    case SaveChip::Sram:
        w.Int(backup, "backup-type", 0);
        break;
    case SaveChip::Flash64:
    case SaveChip::Flash128:
        w.Int(backup, "backup-type", 1);
        w.Int(backup, "flash-size", m.chip == SaveChip::Flash128 ? 2 : 1);
        break;
    case SaveChip::Eeprom512:
    case SaveChip::Eeprom8k:
        w.Int(backup, "backup-type", 2);
        w.Int(backup, "eeprom-size", m.chip == SaveChip::Eeprom8k ? 16 : 1);
        break;
    case SaveChip::None:
    case SaveChip::Unknown:
        break;
    }
    for (const char* field : {"flash-mode", "flash-cmd-pos", "eeprom-mode", "eeprom-counter", "rtc-write-mode",
                              "rtc-cmd", "rtc-data-bit-cnt", "rtc-bit-cnt"}) {
        w.Int(backup, field, 0);
    }
    w.Int(backup, "flash-bank-num", m.flash_bank);
    w.Int(backup, "rtc-state", 1);
    w.Int(backup, "rtc-status", m.rtc_control);
    const std::array<u8, 3> gpio = {m.gpio_data, m.gpio_direction, m.gpio_readable};
    w.Bin(backup, "gpio-regs", gpio);

    const auto sound = root.Child("sound");
    const u16 sound_l = io16(REG_SOUNDCNT_L);
    const u16 sound_x = io16(REG_SOUNDCNT_X);
    w.Int(sound, "on", (sound_x >> 7) & 1);
    w.Int(sound, "gbc-ms-vol-right", sound_l & 7);
    w.Int(sound, "gbc-ms-vol-left", (sound_l >> 4) & 7);
    w.Int(sound, "gbc-ms-vol", sound_h & 3);
    std::array<u8, 64> samples{};
    for (int i = 0; i < 32; ++i) {
        samples[2 * i] = static_cast<u8>(((m.wave_ram[i] >> 4) & 0xF) - 8);
        samples[2 * i + 1] = static_cast<u8>((m.wave_ram[i] & 0xF) - 8);
    }
    w.Bin(sound, "wav-samples", samples);
    for (int c = 0; c < 2; ++c) {
        const auto ds = sound ? sound->Child(c ? "ds1" : "ds0") : std::nullopt;
        std::array<u8, 32> ring{};
        const u32 size = std::min<u32>(m.fifo[c].size, 31);
        std::copy_n(m.fifo[c].bytes.begin(), size, ring.begin());
        w.Bin(ds, "fifo-bytes", ring);
        w.Int(ds, "fifo-base", 0);
        w.Int(ds, "fifo-top", size);
        w.Int(ds, "status", (sound_h >> (c ? 12 : 8)) & 3);
        w.Int(ds, "volume", (~(sound_h >> (c ? 3 : 2))) & 1);
    }
    // The PSG channels as gpSP's register handlers set them up (gba_memory.c); where a note
    // had got to is lost, the next note plays normally.
    for (int c = 0; c < 4; ++c) {
        const auto gs = sound ? sound->Child("gs" + std::to_string(c)) : std::nullopt;
        w.Int(gs, "status", ((sound_l >> (c + 8)) & 1) | ((sound_l >> (c + 11)) & 3));
        w.Int(gs, "active", (sound_x >> 7) & (sound_x >> c) & 1);
        if (c == 2) {
            const u16 control = io16(0x70), length = io16(0x72), freq = io16(0x74);
            static constexpr std::array<u32, 4> WAVE_VOLUME = {0, 16384, 8192, 4096};
            w.Int(gs, "wav-type", (control >> 5) & 1);
            w.Int(gs, "wav-bank", (control >> 6) & 1);
            w.Int(gs, "enable", (control >> 7) & 1);
            w.Int(gs, "len-ticks", 256 - (length & 0xFF));
            w.Int(gs, "wav-vol", (length >> 15) & 1 ? 12288 : WAVE_VOLUME[(length >> 13) & 3]);
            w.Int(gs, "rate", freq & 0x7FF);
            w.Int(gs, "freq-step", (1u << (37 - bits)) / (2048 - (freq & 0x7FF)));
            w.Int(gs, "len-status", (freq >> 14) & 1);
            continue;
        }
        const u16 envelope = io16(c == 0 ? 0x62 : c == 1 ? 0x68 : 0x78);
        const u16 freq = io16(c == 0 ? 0x64 : c == 1 ? 0x6C : 0x7C);
        const u32 ticks = ((envelope >> 8) & 7) * 4;
        const bool active = ((sound_x >> 7) & (sound_x >> c) & 1) != 0;
        w.Int(gs, "env-vol0", (envelope >> 12) & 0xF);
        w.Int(gs, "env-vol", active ? m.psg_volume[c] : (envelope >> 12) & 0xF);
        w.Int(gs, "env-dir", (envelope >> 11) & 1);
        w.Int(gs, "env-ticks0", ticks);
        w.Int(gs, "env-ticks", ticks);
        w.Int(gs, "env-status", ticks != 0);
        w.Int(gs, "len-ticks", 64 - (envelope & 0x3F));
        w.Int(gs, "len-status", (freq >> 14) & 1);
        if (c == 3) {
            const u32 ratio = freq & 7, shift = (freq >> 4) & 0xF;
            w.Int(gs, "noise-type", (freq >> 3) & 1);
            w.Int(gs, "freq-step", ratio == 0 ? (1u << (36 - bits)) >> (shift + 1)
                                              : (1u << (35 - bits)) / (ratio << (shift + 1)));
            continue;
        }
        w.Int(gs, "sample-tbl", (envelope >> 6) & 3);
        w.Int(gs, "rate", freq & 0x7FF);
        w.Int(gs, "freq-step", (1u << (36 - bits)) / (2048 - (freq & 0x7FF)));
        if (c == 0) {
            const u16 sweep = io16(0x60) & 0x7F;
            w.Int(gs, "sweep-shift", sweep & 7);
            w.Int(gs, "sweep-dir", (sweep >> 3) & 1);
            w.Int(gs, "sweep-status", sweep != 8);
            w.Int(gs, "sweep-ticks0", ((sweep >> 4) & 7) * 2);
            w.Int(gs, "sweep-ticks", ((sweep >> 4) & 7) * 2);
        }
    }
    if (!w.ok) {
        return Result<std::vector<u8>>::Fail("the running gpSP's state is missing fields");
    }
    return out;
}

} // namespace den::states::gba
