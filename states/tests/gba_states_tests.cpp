// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Synthetic states: the conversions of real ones (games played on each core, docs/gba-states.md)
// need ROMs, so they are tried with tools/gba_state.cpp instead.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "den/states/gba.h"
#include "gba_machine.h"

using namespace den::states;
using namespace den::states::gba;

namespace {

// gpSP's document, with every field its *_write_savestate functions write (zeros).
class GpspBuilder {
public:
    std::vector<u8> Build() {
        out_.clear();
        const std::size_t root = Start();
        Int("info-magic", 0x06BAC0DE);
        Int("info-version", 0x00010004);
        Doc("cpu", [&] {
            Array("regs", 19);
            Array("spsr", 6);
            Array("regmod", 49);
            Int("bus-value", 0);
        });
        Doc("input", [&] {
            for (const char* key : {"prevkey", "turbo-a", "turbo-b", "gbp-frames", "gbp-sent"}) {
                Int(key, 0);
            }
        });
        Doc("emu", [&] {
            for (const char* key : {"frame-count", "cpu-ticks", "exec-cycles", "video-count", "sleep-cycles",
                                    "serial-irq-cycles", "rand-state", "gbp-state"}) {
                Int(key, 0);
            }
        });
        Doc("timers", [&] {
            for (const char* name : {"0", "1", "2", "3"}) {
                Doc(name, [&] {
                    for (const char* key : {"count", "reload", "prescale", "freq-step", "dsc", "irq", "status"}) {
                        Int(key, 0);
                    }
                });
            }
        });
        Doc("memory", [&] {
            Bin("iwram", IWRAM_SIZE);
            Bin("ewram", EWRAM_SIZE);
            Bin("vram", VRAM_SIZE);
            Bin("oamram", OAM_SIZE);
            Bin("palram", PALETTE_SIZE);
            Bin("ioregs", IO_SIZE);
            Int("dma-bus", 0);
        });
        Doc("backup", [&] {
            for (const char* key : {"backup-type", "flash-mode", "flash-cmd-pos", "flash-bank-num", "flash-dev-id",
                                    "flash-size", "eeprom-size", "eeprom-mode", "eeprom-addr", "eeprom-counter"}) {
                Int(key, 0);
            }
            Bin("gpio-regs", 3);
            for (const char* key :
                 {"rtc-state", "rtc-write-mode", "rtc-cmd", "rtc-status", "rtc-data-bit-cnt", "rtc-bit-cnt"}) {
                Int(key, 0);
            }
            Array("rtc-data-words", 2);
            Array("rtc-base-time", 2);
        });
        Doc("dma", [&] {
            for (const char* name : {"0", "1", "2", "3"}) {
                Doc(name, [&] {
                    for (const char* key :
                         {"src-addr", "dst-addr", "src-dir", "dst-dir", "len", "size", "repeat", "start", "dsc", "irq"}) {
                        Int(key, 0);
                    }
                });
            }
        });
        Doc("sound", [&] {
            for (const char* key : {"on", "buf-base", "gbc-buf-idx", "gbc-last-cpu-ticks", "gbc-partial-ticks",
                                    "gbc-ms-vol-left", "gbc-ms-vol-right", "gbc-ms-vol"}) {
                Int(key, 0);
            }
            Bin("wav-samples", 64);
            for (const char* name : {"ds0", "ds1"}) {
                Doc(name, [&] {
                    for (const char* key : {"status", "volume", "fifo-base", "fifo-top", "fifo-frac", "buf-idx"}) {
                        Int(key, 0);
                    }
                    Bin("fifo-bytes", 32);
                });
            }
            for (const char* name : {"gs0", "gs1", "gs2", "gs3"}) {
                Doc(name, [&] {
                    for (const char* key :
                         {"status", "rate", "freq-step", "sample-idx", "tick-cnt", "volume", "active", "enable",
                          "env-vol0", "env-vol", "env-dir", "env-status", "env-ticks0", "env-ticks", "sweep-status",
                          "sweep-dir", "sweep-ticks0", "sweep-ticks", "sweep-shift", "wav-type", "wav-bank", "wav-vol",
                          "len-status", "len-ticks", "noise-type", "sample-tbl"}) {
                        Int(key, 0);
                    }
                });
            }
        });
        const std::size_t pad = 416 * 1024 - out_.size() - (1 + 9 + 4 + 1) - 1;
        Bin("zpadding", pad);
        out_.push_back(0);
        Store32(out_, root, static_cast<u32>(out_.size() - root));
        return out_;
    }

private:
    std::size_t Start() {
        const std::size_t at = out_.size();
        out_.resize(at + 4);
        return at;
    }
    void Key(u8 type, std::string_view key) {
        out_.push_back(type);
        out_.insert(out_.end(), key.begin(), key.end());
        out_.push_back(0);
    }
    void Int(std::string_view key, u32 value) {
        Key(0x10, key);
        out_.resize(out_.size() + 4);
        Store32(out_, out_.size() - 4, value);
    }
    void Bin(std::string_view key, std::size_t size) {
        Key(0x05, key);
        out_.resize(out_.size() + 4);
        Store32(out_, out_.size() - 4, static_cast<u32>(size));
        out_.push_back(0);
        out_.resize(out_.size() + size);
    }
    template <typename F>
    void Doc(std::string_view key, F body, u8 type = 0x03) {
        Key(type, key);
        const std::size_t at = Start();
        body();
        out_.push_back(0);
        Store32(out_, at, static_cast<u32>(out_.size() - at));
    }
    void Array(std::string_view key, int count) {
        Doc(key, [&] {
            for (int i = 0; i < count; ++i) {
                Int(std::string{static_cast<char>('0' + i / 10), static_cast<char>('0' + i % 10)}, 0);
            }
        }, 0x04);
    }

    std::vector<u8> out_;
};

std::vector<u8> MgbaTemplate() {
    std::vector<u8> state(MGBA_STATE_SIZE, 0);
    Store32(state, 0, 0x0100000B);
    state[0x2E0] = 0xFF; // save type not known yet
    return state;
}

// A game in Thumb code, in system mode, with a little of everything set.
Machine Sample() {
    Machine m;
    m.cpu.cpsr = 0x6000003F;
    m.cpu.pc = 0x080001F2;
    for (int i = 0; i < 8; ++i) {
        m.cpu.low[i] = 0x100 + i;
    }
    m.cpu.high_user = {8, 9, 10, 11, 12};
    m.cpu.sp = {0x03007E00, 0x03007F00, 0x03007FA0, 0x03007FE0, 0, 0};
    m.cpu.lr = {0x08000301, 0, 0x08000400, 0x08000500, 0, 0};
    m.cpu.spsr = {0, 0x10, 0x1F, 0x3F, 0x10, 0x10};
    m.ewram[0x1234] = 0x56;
    m.iwram[0x7FF0] = 0x78;
    m.vram[0x17FFF] = 0x9A;
    m.SetIo16(REG_VCOUNT, 100);
    m.SetIo16(REG_IE, 0x0001);
    m.SetIo16(REG_IME, 1);
    m.line_cycle = 500;
    m.timers[0] = {0xFF00, 0xFC00, 0x0080};
    m.timers[1] = {0x0012, 0x0000, 0x0084}; // counts timer 0's overflows
    m.fifo[0].size = 6;
    m.fifo[0].bytes = {1, 2, 3, 4, 5, 6};
    m.wave_ram[17] = 0x5A;
    m.chip = SaveChip::Flash128;
    m.flash_bank = 1;
    m.gpio_data = 5;
    m.gpio_direction = 7;
    m.gpio_readable = 1;
    return m;
}

void CheckSame(const Machine& a, const Machine& b) {
    CHECK(a.cpu.Visible() == b.cpu.Visible());
    CHECK(a.cpu.cpsr == b.cpu.cpsr);
    CHECK(a.cpu.sp == b.cpu.sp);
    CHECK(a.cpu.lr == b.cpu.lr);
    for (int bank = BANK_IRQ; bank < BANK_COUNT; ++bank) {
        CHECK(a.cpu.spsr[bank] == b.cpu.spsr[bank]);
    }
    CHECK(a.ewram == b.ewram);
    CHECK(a.iwram == b.iwram);
    CHECK(a.vram == b.vram);
    CHECK(a.Io16(REG_VCOUNT) == b.Io16(REG_VCOUNT));
    CHECK(a.line_cycle == b.line_cycle);
    for (int i = 0; i < 2; ++i) {
        CHECK(a.timers[i].counter == b.timers[i].counter);
        CHECK(a.timers[i].reload == b.timers[i].reload);
        CHECK(a.timers[i].control == b.timers[i].control);
    }
    CHECK(a.fifo[0].size == b.fifo[0].size);
    CHECK(a.fifo[0].bytes == b.fifo[0].bytes);
    CHECK(a.wave_ram == b.wave_ram);
    CHECK(a.chip == b.chip);
    CHECK(a.flash_bank == b.flash_bank);
    CHECK(a.gpio_data == b.gpio_data);
    CHECK(a.gpio_direction == b.gpio_direction);
    CHECK(a.gpio_readable == b.gpio_readable);
}

// Halted in a BIOS's IntrWait, with what its SWI dispatcher and the routine pushed.
Machine WaitingIn(Bios bios) {
    Machine m;
    m.bios = bios;
    m.cpu.cpsr = MODE_SYSTEM;
    m.cpu.halted = true;
    const u32 caller_cpsr = 0x2000003F; // Thumb, system mode
    u32 sp = 0x03007E00;
    const auto push = [&](std::vector<u32> words) {
        sp -= 4 * static_cast<u32>(words.size());
        for (std::size_t i = 0; i < words.size(); ++i) {
            m.Write32(sp + 4 * static_cast<u32>(i), words[i]);
        }
    };
    if (bios == Bios::MGba) {
        push({0x22, 0x08000777});    // r2, lr
        push({0x22, 0x33, 0xA4});    // IntrWait: r2, r3, lr
        m.cpu.pc = 0x1F4;
        m.cpu.low[1] = 1;            // the flags
    } else {
        push({0x22, 0x33, 0x08000777}); // r2, r3, lr
        push({0x44, 0x55, 0x66, 0x77}); // IntrWait: r4-r7
        m.cpu.pc = 0x4A0;
        m.cpu.low[0] = 1;
        m.cpu.lr[BANK_USER] = 0x94;
    }
    m.cpu.sp[BANK_USER] = sp;
    m.cpu.sp[BANK_SVC] = 0x03007FE0 - 16;
    m.Write32(0x03007FE0 - 16, caller_cpsr);
    m.Write32(0x03007FE0 - 12, 0x1111); // r11
    m.Write32(0x03007FE0 - 8, 0x1212);  // r12
    m.Write32(0x03007FE0 - 4, 0x08000102); // after the SWI
    m.SetIo16(REG_IE, 1);
    return m;
}

} // namespace

TEST_CASE("GBA cores that convert") {
    CHECK(CanConvertGbaState("GBA DS", "mGBA"));
    CHECK(CanConvertGbaState("mGBA", "gpSP"));
    CHECK_FALSE(CanConvertGbaState("mGBA", "mGBA"));
    CHECK_FALSE(CanConvertGbaState("VBA Next", "mGBA"));
}

TEST_CASE("mGBA and GBA DS share their state, without mGBA's extras") {
    std::vector<u8> state = MgbaTemplate();
    state.resize(MGBA_STATE_SIZE + 0x9000, 0xEE); // the save chip appended
    const auto out = ConvertGbaState("mGBA", state, "GBA DS", {});
    REQUIRE(out);
    CHECK(out.value().size() == MGBA_STATE_SIZE + 16);
    CHECK(std::equal(out.value().begin(), out.value().begin() + MGBA_STATE_SIZE, state.begin()));
    CHECK(out.value().back() == 0);
}

TEST_CASE("A machine goes through mGBA's state and back") {
    const Machine m = Sample();
    const auto written = WriteMgba(m, MgbaTemplate());
    REQUIRE(written);
    const auto read = ReadMgba(written.value());
    REQUIRE(read);
    CheckSame(m, read.value());
    // The pipeline is filled from RAM only; the cartridge mGBA reads itself.
    CHECK(Load32(written.value(), 0x2F8) == 0);
}

TEST_CASE("A machine goes through gpSP's state and back") {
    const std::vector<u8> target = GpspBuilder().Build();
    REQUIRE(target.size() == 416 * 1024);
    const Machine m = Sample();
    const auto written = WriteGpsp(m, target);
    REQUIRE(written);
    const auto read = ReadGpsp(written.value());
    REQUIRE(read);
    CheckSame(m, read.value());
}

TEST_CASE("A CPU waiting in mGBA's BIOS goes back to the game's SWI") {
    Machine m = WaitingIn(Bios::MGba);
    REQUIRE(LeaveBios(m));
    CHECK(m.cpu.pc == 0x08000100); // the SWI, run again
    CHECK(m.cpu.cpsr == 0x2000003F);
    CHECK_FALSE(m.cpu.halted);
    CHECK(m.cpu.Get(2) == 0x22);
    CHECK(m.cpu.Get(3) == 0x33);
    CHECK(m.cpu.Get(11) == 0x1111);
    CHECK(m.cpu.Get(12) == 0x1212);
    CHECK(m.cpu.Get(13) == 0x03007E00);
    CHECK(m.cpu.Get(14) == 0x08000777);
    CHECK(m.cpu.sp[BANK_SVC] == 0x03007FE0);
    CHECK(m.cpu.lr[BANK_SVC] == 0x08000102);
    CHECK(m.Io16(REG_IME) == 1);
}

TEST_CASE("A CPU waiting in gpSP's BIOS for an interrupt that came carries on after the SWI") {
    Machine m = WaitingIn(Bios::Open);
    m.SetIo16(REG_IF, 1);
    REQUIRE(LeaveBios(m));
    CHECK(m.cpu.pc == 0x08000102);
    for (int r = 4; r < 8; ++r) {
        CHECK(m.cpu.Get(r) == 0x44 + 0x11 * static_cast<u32>(r - 4));
    }
    CHECK(m.cpu.Get(3) == 0x33);
    CHECK(m.cpu.Get(13) == 0x03007E00);
}

TEST_CASE("A BIOS stack that is not the BIOS's is refused") {
    Machine m = WaitingIn(Bios::MGba);
    m.Write32(m.cpu.sp[BANK_USER] + 8, 0x1234); // not the dispatcher's return
    CHECK_FALSE(LeaveBios(m));
    Machine other = WaitingIn(Bios::Open);
    other.cpu.pc = 0x700; // another routine
    CHECK_FALSE(LeaveBios(other));
}

TEST_CASE("The IRQ dispatcher moves between BIOSes, a running handler does not") {
    Machine m;
    m.cpu.cpsr = MODE_IRQ | 0x80;
    m.cpu.pc = 0x18C; // mGBA's dispatcher, its frame pushed
    m.cpu.sp[BANK_IRQ] = 0x03007FA0 - 24;
    m.Write32(0x03007FA0 - 4, 0x08000204); // where the interrupt came in
    REQUIRE(MoveIrqToBios(m, Bios::Open));
    CHECK(m.cpu.pc == 0x28);

    Machine handler;
    handler.cpu.cpsr = MODE_IRQ | 0x80;
    handler.cpu.pc = 0x03000100;
    handler.cpu.lr[BANK_IRQ] = 0x194;
    CHECK_FALSE(MoveIrqToBios(handler, Bios::Open));

    Machine into_bios;
    into_bios.cpu.cpsr = MODE_IRQ | 0x80;
    into_bios.cpu.pc = 0x18;
    into_bios.cpu.lr[BANK_IRQ] = 0x1F8; // came in while waiting in the BIOS
    CHECK_FALSE(MoveIrqToBios(into_bios, Bios::Open));
}

TEST_CASE("A DMA midway is finished for a core that runs them whole") {
    Machine m;
    m.SetIo16(REG_DMA0SAD + 12 * 3 + 10, 0x8000 | 0x1000); // DMA3 on at VBlank, 16-bit
    m.dma[3] = {0x02000000, 0x06000000, 2};
    m.ewram[0] = 1;
    m.ewram[3] = 4;
    REQUIRE(FinishDmas(m));
    CHECK(m.vram[0] == 1);
    CHECK(m.vram[3] == 4);
    CHECK(m.dma[3].count == 0);
    CHECK((m.Io16(REG_DMA0SAD + 12 * 3 + 10) & 0x8000) == 0);
}

TEST_CASE("GBA battery saves read the same on every core") {
    // VBA Next keeps an EEPROM at 0x20000 of its 0x22000 bytes.
    std::vector<u8> vba(0x22000, 0xFF);
    vba[0x20000] = 0x12;
    vba[0x21FFF] = 0x34;
    const std::vector<u8> portable = PortableGbaSave(vba);
    REQUIRE(portable.size() == 0x2000);
    CHECK(portable[0] == 0x12);
    CHECK(portable[0x1FFF] == 0x34);

    std::vector<u8> memory(0x22000, 0xFF);
    LoadGbaSave("VBA Next", portable, false, memory);
    CHECK(memory[0x20000] == 0x12);
    CHECK(memory[0] == 0xFF);

    // gpSP writes 128 KiB whatever the chip: the ROM says when it is an EEPROM.
    std::vector<u8> gpsp(0x20000, 0xFF);
    gpsp[0] = 0x56;
    std::fill(memory.begin(), memory.end(), 0xFF);
    LoadGbaSave("VBA Next", gpsp, true, memory);
    CHECK(memory[0x20000] == 0x56);
    std::fill(memory.begin(), memory.end(), 0xFF);
    LoadGbaSave("VBA Next", gpsp, false, memory);
    CHECK(memory[0] == 0x56);

    std::vector<u8> mgba(0x8000, 0xFF);
    LoadGbaSave("mGBA", vba, false, mgba);
    CHECK(mgba[0] == 0x12);
    const std::string rom = "....EEPROM_V124....";
    CHECK(RomUsesEeprom(std::span(reinterpret_cast<const u8*>(rom.data()), rom.size())));
}
