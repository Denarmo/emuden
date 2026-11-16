# GBA save states across cores

The GBA plugin offers mGBA, GBA DS, gpSP and VBA Next. A state saved with one of the first
three loads in the others: the `states` module reads it into a neutral machine
(`states/src/gba_machine.h`) and writes that out in the target core's format. Battery saves
(`.srm`) are made to read the same on all four.

Versions studied (from the shipped DLLs): mGBA 0.11-212-7a12d6d (libretro/mgba), GBA DS 1.5.2
(Denarmo/gba-ds 9a7dce1, libmgba 143adf5), gpSP v1.1.0-5819380 (libretro/gpsp), VBA Next v1.0.2
(libretro/vba-next 788192f).

## The formats

| | mGBA | GBA DS | gpSP | VBA Next |
|---|---|---|---|---|
| Container | `GBASerializedState` v11, magic `0x0100000B`, 0x61000 bytes, then extdata (save chip, RTC) | the same struct, no extdata | one BSON document, 416 KiB, magic `0x06BAC0DE`, version `0x10004` | flat `utilWriteState` fields, no magic |
| r15 | next instruction + 4 (ARM) / + 2 (Thumb), prefetch words stored | as mGBA | next instruction | next instruction + pipeline |
| Banks | `bankedRegisters[6][7]`, `bankedSPSRs[6]` | as mGBA | `regmod[49]` rows by `mode & 0xF`, `spsr[6]` | `reg[45]` |
| Times | relative to now (video, timers, DMA, audio events) | as mGBA | absolute counters (`video-count`, `exec-cycles`, timer counts) | absolute counters |
| Frame ends at | VBlank start (line 160) | as mGBA | line 0 | line 160 |
| I/O | 0x400 bytes, replayed through `GBAIOWrite` on load | as mGBA | `ioregs` plus per-device docs (timers, dma, sound) | `ioMem` plus device fields |
| Timers | counter, reload, control, overflow time | as mGBA | count, reload, prescale, `freq-step` (not recomputed on load) | as registers |
| DMA | source, dest, count left, next time | as mGBA | runs whole at trigger; only setup kept (FIFO DMAs special) | runs whole |
| Audio | FIFO A/B (up to 32 bytes + internal), PSG state per channel | as mGBA | `ds0/ds1` ring buffers, `gs0`-`gs3` with frequency steps | Blargg PSG state, FIFOs |
| Cart backup | type, flash bank and command state, EEPROM state | as mGBA | `backup` doc | flash/EEPROM state |
| GPIO / RTC | pin state, RTC registers | as mGBA | `gpio-regs`, `rtc-*` | RTC state |
| BIOS | mGBA's HLE BIOS unless a BIOS file is set | as mGBA | Normmatt's open BIOS built in | own HLE |
| Second screen | | rebuilt after load, not in the state | | |

## How a conversion works

- **mGBA ↔ GBA DS** is a copy: the struct is the same, mGBA's extdata is dropped (GBA DS keeps
  the save chip in its battery save). Tested both ways: pixel- and RAM-identical.
- **Either ↔ gpSP** goes through the machine:
  - CPU registers move between bank layouts; r15 and the prefetch are recomputed. mGBA refetches
    its pipeline before it loads RAM, so the prefetch is filled from the machine's RAM.
  - Line position: mGBA's video event time plus its mode (in HDraw or HBlank) gives the cycle in
    the line; gpSP's `video-count` is set to the same point. Timers keep their counters; gpSP's
    `freq-step` for timers and PSG channels is computed from the registers, because gpSP leaves
    it 0 after a load and hangs.
  - gpSP's sound rate (15 or 16 bits) is read from the target state.
  - FIFO DMAs are set up the way gpSP's `trigger_dma` does. A RAM-to-RAM DMA midway in mGBA is
    run to the end before writing gpSP.
  - FIFO bytes move oldest first; the PSG registers and wave RAM (both banks) move, and gpSP's
    channels are derived from them.
- **The BIOS.** The two HLE BIOSes put their code and stack frames in different places, so a CPU
  inside one is moved out before it can run on the other:
  - halted in `Halt`, `IntrWait` or `VBlankIntrWait`: the frames the SWI dispatcher and routine
    pushed are popped. If the interrupt it waits for has come (`IE & IF & flags`), it resumes after
    the SWI; otherwise the SWI runs again.
  - in IRQ mode at the vector or inside the BIOS dispatcher: the address is mapped to the other
    BIOS's dispatcher.
  - The player saves its resume state after up to 30 muted frames that bring the CPU out of a
    place it cannot be moved from, so leaving a game nearly always gives a portable state.
- **The target's own state is the template**: fields with no neutral meaning (scheduler details,
  core internals) keep the target's values.

## In the player

- Loading a state (resume or a slot) made with another GBA core converts it. If that fails the
  player says so and, for resume, starts from the game's save with the existing toast.
- `MatchResumeCore` keeps the game's own core when the resume state converts, instead of
  switching cores.

## Battery saves

| Core | `.srm` layout |
|---|---|
| mGBA, GBA DS | the chip's size at offset 0 (128 KiB while autodetecting) |
| gpSP | always 128 KiB at offset 0, same EEPROM bit order |
| VBA Next | 0x22000 bytes: SRAM and flash at 0, EEPROM at 0x20000 |

mGBA, GBA DS and gpSP read each other's saves as they are. VBA Next's EEPROM saves did not: the
player now writes the EEPROM part alone, and when VBA Next loads a save of 8 KiB or less, or one
for a ROM with `EEPROM_V` in it, the data goes to 0x20000. No core writes an RTC trailer to the
libretro `.srm`.

## What is lossy or refused

- Refused: VBA Next states (not converted), the official BIOS (checksum `0xBAAE187F`), a game IRQ
  handler running, FIQ mode, an mGBA DMA still copying from the cartridge or I/O.
- Approximate: PSG channel phase and envelope timing (mGBA targets keep the template's), the FIFO
  sample's fractional position, gpSP's sound rate when the template does not show it (15 bits).
  A completed `IntrWait` leaves the BIOS's IF copy as it was.
- Timing: games that read timers or count frames during loading drift by a few cycles. Golden Sun
  states taken mid-load differ only in loading progress.

## Tests

- `den_states_tests`: synthetic states through mGBA and gpSP and back, both BIOS unwinds,
  refused stacks, IRQ dispatcher mapping, DMAs midway, `.srm` normalization.
- `den_gba_state convert|check` converts real states. Checked with states from Minish Cap
  (EEPROM), Mario Kart Super Circuit (flash), Emerald (flash 1M and RTC), Wario Land 4 (SRAM),
  Aria of Sorrow and Golden Sun (flash 64K): after 120 to 180 frames of the same input, the
  converted run's EWRAM matched the source core's own run (Aria of Sorrow: up to 10 bytes of
  RNG drift) for mGBA→gpSP, gpSP→mGBA, gpSP→GBA DS, GBA DS→gpSP and mGBA↔GBA DS.
