// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The parts of a state that only make sense with the BIOS that wrote it. A core without
// Nintendo's BIOS runs its own replacement, and mGBA's and gpSP's differ in layout: a CPU
// waiting inside one (IntrWait, Halt) is taken back out to the game's SWI, which the target
// then runs in its own BIOS; the IRQ dispatcher, the same six instructions in both, is mapped
// address for address.

#include "gba_machine.h"

namespace den::states::gba {

namespace {

constexpr u32 BIOS_END = 0x4000;
constexpr u32 LR = 14; // stands for lr in a frame's register list

// The IRQ dispatcher (push {r0-r3, r12, lr}; mov r0, #0x4000000; lr = return; ldr pc,
// [r0, #-4]; pop; subs pc, lr, #4) and the address the game's handler returns to.
struct IrqDispatcher {
    u32 start;
    u32 handler_return;
};

IrqDispatcher DispatcherOf(Bios bios) {
    switch (bios) {
    case Bios::MGba:
        return {0x184, 0x194};
    case Bios::Open:
        return {0x020, 0x030};
    case Bios::Official:
        break;
    }
    return {0x128, 0x138};
}

constexpr u32 DISPATCHER_SIZE = 0x18;

// Where a wait routine keeps what it saved, by the PC the CPU stopped at.
struct WaitFrame {
    bool found = false;
    bool intr_wait = false;     // IntrWait or VBlankIntrWait (else Halt)
    bool vblank = false;        // VBlankIntrWait before it set its arguments
    int flags_register = 1;     // where IntrWait keeps the flags it waits for
    std::vector<u32> routine;   // registers the routine pushed, lowest address first
    u32 live_lr = 0;            // when the routine pushed nothing: the lr it must hold
};

struct BiosWaits {
    std::vector<u32> swi_frame; // the SWI dispatcher's push in system mode
    u32 swi_return;             // the lr a routine is called with
};

BiosWaits WaitsOf(Bios bios) {
    if (bios == Bios::MGba) {
        return {{2, LR}, 0xA4};
    }
    return {{2, 3, LR}, 0x94};
}

WaitFrame FindWait(Bios bios, u32 pc) {
    WaitFrame f;
    if (bios == Bios::MGba) {
        // Halt 0x1B4-0x1C0; VBlankIntrWait 0x1C4 sets r0/r1; IntrWait 0x1CC pushes {r2, r3,
        // lr}, loops 0x1F0-0x20C, pops at 0x210. The flags stay in r1.
        if (pc >= 0x1B4 && pc < 0x1C4) {
            f.found = true;
            f.live_lr = 0xA4;
        } else if (pc >= 0x1C4 && pc <= 0x1CC) {
            f.found = f.intr_wait = true;
            f.vblank = pc < 0x1CC;
            f.live_lr = 0xA4;
        } else if (pc > 0x1CC && pc <= 0x210) {
            f.found = f.intr_wait = true;
            f.routine = {2, 3, LR};
        }
    } else if (bios == Bios::Open) {
        // Halt 0x3D8-0x3E4; IntrWait 0x434 pushes {r4-r7} at 0x43C, has the flags in r0 from
        // 0x484 on, loops 0x49C-0x4D0 and pops at 0x4C4; VBlankIntrWait 0x4D4-0x4DC.
        if (pc >= 0x3D8 && pc < 0x3E8) {
            f.found = true;
            f.live_lr = 0x94;
        } else if (pc >= 0x484 && pc <= 0x4D0 && pc != 0x4C8) {
            f.found = f.intr_wait = true;
            f.flags_register = 0;
            f.routine = {4, 5, 6, 7};
        } else if (pc >= 0x4D4 && pc < 0x4E0) {
            f.found = f.intr_wait = f.vblank = true;
            f.live_lr = 0x94;
        }
    }
    return f;
}

bool InBios(u32 address) {
    return address < BIOS_END;
}

Status IrqReturnsOutsideBios(const Machine& m, u32 interrupted_lr) {
    if (InBios(interrupted_lr - 4)) {
        return Status::Fail("an interrupt came in while the BIOS was running");
    }
    return {};
}

} // namespace

int BankOf(u32 mode) {
    switch (mode) {
    case MODE_USER:
    case MODE_SYSTEM:
        return BANK_USER;
    case MODE_FIQ:
        return BANK_FIQ;
    case MODE_IRQ:
        return BANK_IRQ;
    case MODE_SVC:
        return BANK_SVC;
    case MODE_ABT:
        return BANK_ABT;
    case MODE_UND:
        return BANK_UND;
    }
    return -1;
}

u32 Cpu::Get(int index) const {
    const int bank = std::max(BankOf(Mode()), 0);
    if (index < 8) {
        return low[index];
    }
    if (index < 13) {
        return bank == BANK_FIQ ? high_fiq[index - 8] : high_user[index - 8];
    }
    if (index == 13) {
        return sp[bank];
    }
    if (index == 14) {
        return lr[bank];
    }
    return pc;
}

void Cpu::Set(int index, u32 value) {
    const int bank = std::max(BankOf(Mode()), 0);
    if (index < 8) {
        low[index] = value;
    } else if (index < 13) {
        (bank == BANK_FIQ ? high_fiq : high_user)[index - 8] = value;
    } else if (index == 13) {
        sp[bank] = value;
    } else if (index == 14) {
        lr[bank] = value;
    } else {
        pc = value;
    }
}

std::array<u32, 16> Cpu::Visible() const {
    std::array<u32, 16> r{};
    for (int i = 0; i < 16; ++i) {
        r[i] = Get(i);
    }
    return r;
}

void Cpu::SetVisible(const std::array<u32, 16>& r) {
    for (int i = 0; i < 16; ++i) {
        Set(i, r[i]);
    }
}

bool Machine::Read32(u32 address, u32* value) const {
    address &= ~3u;
    if (address >> 24 == 0x02) {
        *value = Load32(ewram, address & (EWRAM_SIZE - 1));
        return true;
    }
    if (address >> 24 == 0x03) {
        *value = Load32(iwram, address & (IWRAM_SIZE - 1));
        return true;
    }
    return false;
}

bool Machine::Write32(u32 address, u32 value) {
    address &= ~3u;
    if (address >> 24 == 0x02) {
        Store32(ewram, address & (EWRAM_SIZE - 1), value);
        return true;
    }
    if (address >> 24 == 0x03) {
        Store32(iwram, address & (IWRAM_SIZE - 1), value);
        return true;
    }
    return false;
}

Status LeaveBios(Machine& m) {
    Cpu& cpu = m.cpu;
    if (!InBios(cpu.pc)) {
        return {};
    }
    const IrqDispatcher irq = DispatcherOf(m.bios);
    if (cpu.pc == 0x18 || (cpu.pc >= irq.start && cpu.pc < irq.start + DISPATCHER_SIZE)) {
        return {}; // MoveIrqToBios
    }
    if (m.bios == Bios::Official) {
        return Status::Fail("the CPU is inside Nintendo's BIOS");
    }
    const WaitFrame wait = FindWait(m.bios, cpu.pc);
    if (!wait.found) {
        return Status::Fail("the CPU is inside a BIOS routine");
    }
    if (cpu.Mode() != MODE_SYSTEM) {
        return Status::Fail("a BIOS wait outside system mode");
    }
    const BiosWaits waits = WaitsOf(m.bios);
    u32 sp = cpu.sp[BANK_USER];
    u32 restored[16] = {};
    bool has[16] = {};
    const auto pop = [&](const std::vector<u32>& regs) {
        for (const u32 reg : regs) {
            u32 value = 0;
            if (!m.Read32(sp, &value)) {
                return false;
            }
            sp += 4;
            restored[reg] = value;
            has[reg] = true;
        }
        return true;
    };
    if (!pop(wait.routine)) {
        return Status::Fail("the BIOS stack is outside RAM");
    }
    const u32 routine_lr = has[LR] ? restored[LR] : cpu.lr[BANK_USER];
    if (routine_lr != waits.swi_return) {
        return Status::Fail("the BIOS stack does not hold what the BIOS put there");
    }
    has[LR] = false;
    if (!pop(waits.swi_frame)) {
        return Status::Fail("the BIOS stack is outside RAM");
    }
    // The SWI dispatcher's frame in supervisor mode: spsr, r11, r12, lr.
    u32 svc = cpu.sp[BANK_SVC];
    u32 spsr = 0, r11 = 0, r12 = 0, lr_svc = 0;
    if (!m.Read32(svc, &spsr) || !m.Read32(svc + 4, &r11) || !m.Read32(svc + 8, &r12) ||
        !m.Read32(svc + 12, &lr_svc)) {
        return Status::Fail("the BIOS stack is outside RAM");
    }
    const u32 caller_mode = spsr & 0x1F;
    if (caller_mode != MODE_SYSTEM && caller_mode != MODE_USER) {
        return Status::Fail("a BIOS wait called from an interrupt");
    }
    const u32 flags = wait.vblank ? 1u : cpu.Get(wait.flags_register) & 0x3FFF;
    for (int reg = 0; reg < 13; ++reg) {
        if (has[reg]) {
            cpu.Set(reg, restored[reg]);
        }
    }
    cpu.high_user[11 - 8] = r11;
    cpu.high_user[12 - 8] = r12;
    cpu.lr[BANK_USER] = restored[LR];
    cpu.sp[BANK_USER] = sp;
    cpu.sp[BANK_SVC] = svc + 16;
    cpu.lr[BANK_SVC] = lr_svc;
    cpu.spsr[BANK_SVC] = spsr;
    cpu.cpsr = spsr;
    cpu.halted = false;

    // Taken back to the SWI, the target runs it again in its own BIOS. When the interrupt
    // it waits for is already flagged, the wait is over: the game carries on after the SWI
    // and the interrupt is taken there (the BIOS flag the handler sets is left for the next
    // wait, which clears it first).
    const u32 pending = m.Io16(REG_IE) & m.Io16(REG_IF) & (wait.intr_wait ? flags : 0x3FFF);
    const u32 swi_size = (spsr & 0x20) ? 2 : 4;
    cpu.pc = pending ? lr_svc : lr_svc - swi_size;
    if (wait.intr_wait) {
        cpu.low[0] = 1;
        cpu.low[1] = flags;
        m.SetIo16(REG_IME, 1);
    }
    return {};
}

u32 IrqAddressFor(Bios from, Bios to, u32 address) {
    const IrqDispatcher a = DispatcherOf(from);
    const IrqDispatcher b = DispatcherOf(to);
    if (address >= a.start && address < a.start + DISPATCHER_SIZE) {
        return address - a.start + b.start;
    }
    if (address == a.handler_return) {
        return b.handler_return;
    }
    return address;
}

Status MoveIrqToBios(Machine& m, Bios to) {
    Cpu& cpu = m.cpu;
    const IrqDispatcher irq = DispatcherOf(m.bios);
    const bool in_dispatcher = cpu.pc >= irq.start && cpu.pc < irq.start + DISPATCHER_SIZE;
    if (cpu.Mode() == MODE_IRQ) {
        if (cpu.pc == 0x18 || cpu.pc == irq.start || cpu.pc == irq.start + 0x14) {
            // Nothing pushed yet, or popped again: lr is where the interrupt came in.
            if (Status s = IrqReturnsOutsideBios(m, cpu.lr[BANK_IRQ]); !s) {
                return s;
            }
        } else if (in_dispatcher) {
            u32 stacked = 0;
            if (!m.Read32(cpu.sp[BANK_IRQ] + 20, &stacked)) {
                return Status::Fail("the interrupt stack is outside RAM");
            }
            if (Status s = IrqReturnsOutsideBios(m, stacked); !s) {
                return s;
            }
        } else {
            return Status::Fail("the game's interrupt handler is running");
        }
    } else if (cpu.lr[BANK_IRQ] == irq.handler_return) {
        return Status::Fail("the game's interrupt handler is running");
    }
    if (m.bios == to) {
        return {};
    }
    if (to == Bios::Official || m.bios == Bios::Official) {
        if (InBios(cpu.pc) || cpu.lr[BANK_IRQ] == irq.handler_return) {
            return Status::Fail("the CPU is inside another BIOS");
        }
        return {};
    }
    if (in_dispatcher) {
        cpu.pc = IrqAddressFor(m.bios, to, cpu.pc);
    }
    for (u32& lr : cpu.lr) {
        lr = IrqAddressFor(m.bios, to, lr);
    }
    m.bios = to;
    return {};
}

namespace {

u8* DmaMemory(Machine& m, u32 address) {
    switch (address >> 24) {
    case 0x02:
        return &m.ewram[address & (EWRAM_SIZE - 1)];
    case 0x03:
        return &m.iwram[address & (IWRAM_SIZE - 1)];
    case 0x05:
        return &m.palette[address & (PALETTE_SIZE - 1)];
    case 0x06: {
        u32 offset = address & 0x1FFFF;
        if (offset >= VRAM_SIZE) {
            offset -= 0x8000;
        }
        return &m.vram[offset];
    }
    case 0x07:
        return &m.oam[address & (OAM_SIZE - 1)];
    }
    return nullptr;
}

} // namespace

Status FinishDmas(Machine& m) {
    for (int i = 0; i < 4; ++i) {
        Dma& dma = m.dma[i];
        const u32 cnt = REG_DMA0SAD + 12 * i + 10;
        u16 control = m.Io16(cnt);
        const u32 timing = (control >> 12) & 3;
        if (!(control & 0x8000) || dma.count == 0 || (timing == 3 && (i == 1 || i == 2))) {
            continue;
        }
        const u32 width = (control & 0x400) ? 4 : 2;
        const int src_step = std::array<int, 4>{1, -1, 0, 1}[(control >> 7) & 3] * static_cast<int>(width);
        const int dst_step = std::array<int, 4>{1, -1, 0, 1}[(control >> 5) & 3] * static_cast<int>(width);
        for (; dma.count > 0; --dma.count) {
            u8* from = DmaMemory(m, dma.src & ~(width - 1));
            u8* to = DmaMemory(m, dma.dst & ~(width - 1));
            if (!from || !to) {
                return Status::Fail("a DMA from or to the cartridge or I/O is midway");
            }
            std::memcpy(to, from, width);
            dma.src += src_step;
            dma.dst += dst_step;
        }
        if (!(control & 0x200) || timing == 0) {
            control &= 0x7FFF;
        } else if (((control >> 5) & 3) == 3) {
            dma.dst = Load32(m.io, REG_DMA0SAD + 12 * i + 4);
        }
        m.SetIo16(cnt, control);
        if (control & 0x4000) {
            m.SetIo16(REG_IF, static_cast<u16>(m.Io16(REG_IF) | (0x100 << i)));
        }
    }
    return {};
}

} // namespace den::states::gba
