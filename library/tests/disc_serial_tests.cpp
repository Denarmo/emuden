// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "../../patching/tests/encoders.h"
#include "den/library/rom_codes.h"

using namespace den;
using namespace den::library;
using namespace den::patching::testing;
namespace fs = std::filesystem;

namespace {

fs::path Dir() {
    const fs::path dir = fs::temp_directory_path() / "emuden-disc-serial-test";
    fs::create_directories(dir);
    return dir;
}

void Le32Both(Bytes& out, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
        out[at + 7 - i] = static_cast<std::uint8_t>(value >> (8 * i)); // the big-endian copy
    }
}

// One directory record at `at` in a sector: a file or folder at `lba` of `size` bytes.
std::size_t Record(Bytes& sector, std::size_t at, std::string_view name, std::uint32_t lba, std::uint32_t size, bool folder) {
    const std::size_t length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
    sector[at] = static_cast<std::uint8_t>(length);
    Le32Both(sector, at + 2, lba);
    Le32Both(sector, at + 10, size);
    sector[at + 25] = folder ? 0x02 : 0x00;
    sector[at + 32] = static_cast<std::uint8_t>(name.size());
    std::memcpy(sector.data() + at + 33, name.data(), name.size());
    return at + length;
}

// A tiny ISO 9660 image of 2048-byte sectors: the volume descriptor at 16, the root folder at
// 18 holding a README and (when `cnf` is not empty) SYSTEM.CNF at 19.
std::vector<Bytes> IsoSectors(std::string_view cnf) {
    std::vector<Bytes> sectors(21, Bytes(2048, 0));
    Bytes& pvd = sectors[16];
    pvd[0] = 1;
    std::memcpy(pvd.data() + 1, "CD001", 5);
    pvd[6] = 1;
    Record(pvd, 156, std::string_view("\0", 1), 18, 2048, true);
    sectors[17][0] = 255; // the set's terminator
    std::memcpy(sectors[17].data() + 1, "CD001", 5);
    Bytes& root = sectors[18];
    std::size_t at = Record(root, 0, std::string_view("\0", 1), 18, 2048, true);
    at = Record(root, at, std::string_view("\1", 1), 18, 2048, true);
    at = Record(root, at, "README.TXT;1", 20, 5, false);
    if (!cnf.empty()) {
        Record(root, at, "SYSTEM.CNF;1", 19, static_cast<std::uint32_t>(cnf.size()), false);
        std::memcpy(sectors[19].data(), cnf.data(), cnf.size());
    }
    std::memcpy(sectors[20].data(), "hello", 5);
    return sectors;
}

fs::path Iso(const char* file, std::string_view cnf) {
    Bytes image;
    for (const Bytes& sector : IsoSectors(cnf)) {
        image.insert(image.end(), sector.begin(), sector.end());
    }
    const fs::path path = Dir() / file;
    WriteFile(path, image);
    return path;
}

// The same as raw mode 2 CD sectors (sync, header, subheader, data, error codes), with a cue.
fs::path BinAndCue(const char* name, std::string_view cnf) {
    Bytes image;
    for (const Bytes& sector : IsoSectors(cnf)) {
        Bytes raw(2352, 0);
        raw[0] = 0x00;
        std::memset(raw.data() + 1, 0xFF, 10);
        raw[15] = 2;
        std::memcpy(raw.data() + 24, sector.data(), 2048);
        image.insert(image.end(), raw.begin(), raw.end());
    }
    const std::string bin = std::string(name) + " (Track 1).bin";
    WriteFile(Dir() / bin, image);
    const std::string cue = "FILE \"" + bin + "\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n";
    const fs::path path = Dir() / (std::string(name) + ".cue");
    WriteFile(path, Bytes(cue.begin(), cue.end()));
    return path;
}

} // namespace

TEST_CASE("disc serials: a PlayStation 2 ISO's BOOT2 line") {
    const fs::path iso = Iso("sa.iso", "BOOT2 = cdrom0:\\SLUS_209.46;1\r\nVER = 1.03\r\nVMODE = NTSC\r\n");
    CHECK(ReadDiscSerial(iso) == "SLUS-20946");
    const RomCode header = ReadRomCode(iso, "ps2");
    CHECK(header.code == "SLUS-20946");
    CHECK(GameOfCode("ps2", header.code) == "Grand Theft Auto - San Andreas (USA)");
    CHECK(GameOfCode("ps2", "SLES-20946").empty()); // a serial's region is in its letters
}

TEST_CASE("disc serials: a PlayStation track of raw sectors through its cue sheet") {
    const fs::path cue = BinAndCue("Final Fantasy VII Remix", "BOOT = cdrom:\\SCUS_941.63;1\nTCB = 4\nEVENT = 10\n");
    CHECK(ReadDiscSerial(cue) == "SCUS-94163");
    CHECK(ReadRomCode(cue, "psx").code == "SCUS-94163");
    CHECK(GameOfCode("psx", "SCUS-94163") == "Final Fantasy VII (USA) (Disc 1)");
}

TEST_CASE("disc serials: nothing without SYSTEM.CNF, a file system or a boot line") {
    CHECK(ReadDiscSerial(Iso("audio.iso", {})).empty());
    CHECK(ReadDiscSerial(Iso("noboot.iso", "VMODE = PAL\r\n")).empty());
    WriteFile(Dir() / "junk.bin", MakeRom(50000, 9));
    CHECK(ReadDiscSerial(Dir() / "junk.bin").empty());
    CHECK(ReadDiscSerial(Dir() / "missing.iso").empty());
}
