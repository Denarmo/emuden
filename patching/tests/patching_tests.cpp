// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/patching/patch.h"
#include "encoders.h"
#include "vcdiff_writer.h"

using namespace den;
using namespace den::patching;
using namespace den::patching::testing;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "emuden-patching-test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// Applies `patch` to `source` through files and gives the result, or the error.
struct Applied {
    Status status;
    Bytes bytes;
    PatchInfo info;
};

Applied Run(const char* name, const Bytes& source, const Bytes& patch, bool verify = true) {
    const fs::path dir = FreshDir(name);
    WriteFile(dir / "source.rom", source);
    WriteFile(dir / "patch.bin", patch);
    Applied applied;
    if (auto info = Inspect(dir / "patch.bin")) {
        applied.info = *info;
    }
    ApplyOptions options;
    options.verify_source = verify;
    applied.status = Apply(dir / "patch.bin", dir / "source.rom", dir / "out.rom", options);
    applied.bytes = ReadFile(dir / "out.rom");
    // The source is only read, and nothing is left beside the result.
    REQUIRE(ReadFile(dir / "source.rom") == source);
    REQUIRE(!fs::exists(dir / "out.rom.part"));
    REQUIRE(fs::exists(dir / "out.rom") == applied.status.ok);
    return applied;
}

const Bytes ROM = MakeRom(200000, 1);

Bytes Slice(const Bytes& from, std::size_t at, std::size_t size) {
    return Bytes(from.begin() + at, from.begin() + at + size);
}

} // namespace

TEST_CASE("IPS: records, runs, growth and the cut size") {
    const Bytes hack = MakeHack(ROM, 2);
    Applied applied = Run("ips", ROM, MakeIps(ROM, hack));
    REQUIRE(applied.status.ok);
    REQUIRE(applied.info.format == Format::Ips);
    REQUIRE(applied.info.format_name == "ips");
    REQUIRE(applied.bytes == hack);

    const Bytes longer = MakeHack(ROM, 3, 5000);
    REQUIRE(Run("ips-grow", ROM, MakeIps(ROM, longer)).bytes == longer);

    const Bytes shorter = MakeHack(ROM, 4, -70000);
    const Bytes cut = MakeIps(ROM, shorter);
    REQUIRE(std::string(cut.end() - 6, cut.end() - 3) == "EOF");
    REQUIRE(Run("ips-cut", ROM, cut).bytes == shorter);
}

TEST_CASE("IPS: a record at the offset that spells EOF, and a patch without its end") {
    // 0x454F46 is "EOF": there it is a record's offset, not the end.
    Bytes source(0x454F46 + 16, 0x11);
    Bytes target = source;
    target[0x454F46] = 0xAB;
    target[0x454F46 + 1] = 0xCD;
    target[0x454F46 + 9] = 0x77;
    Bytes patch;
    Put(patch, "PATCH");
    PutBe(patch, 0x454F46, 3);
    PutBe(patch, 2, 2);
    patch.push_back(0xAB);
    patch.push_back(0xCD);
    PutBe(patch, 0x454F46 + 9, 3);
    PutBe(patch, 1, 2);
    patch.push_back(0x77);
    Put(patch, "EOF");
    REQUIRE(Run("ips-eof", source, patch).bytes == target);

    patch.resize(patch.size() - 3);
    const Applied broken = Run("ips-open", source, patch);
    REQUIRE(!broken.status.ok);
    REQUIRE(broken.status.error.find("end mark") != std::string::npos);
}

TEST_CASE("UPS: both checksums are held to") {
    const Bytes hack = MakeHack(ROM, 5, 1234);
    const Bytes patch = MakeUps(ROM, hack);
    Applied applied = Run("ups", ROM, patch);
    REQUIRE(applied.status.ok);
    REQUIRE(applied.bytes == hack);
    REQUIRE(applied.info.format == Format::Ups);
    REQUIRE(*applied.info.source_size == ROM.size());
    REQUIRE(*applied.info.target_size == hack.size());
    REQUIRE(*applied.info.source_crc32 == Crc32(ROM.data(), ROM.size()));
    REQUIRE(*applied.info.target_crc32 == Crc32(hack.data(), hack.size()));

    const Bytes shorter = MakeHack(ROM, 6, -999);
    REQUIRE(Run("ups-short", ROM, MakeUps(ROM, shorter)).bytes == shorter);

    // Another ROM: named by both checksums.
    Bytes other = ROM;
    other[77] ^= 0x40;
    const Applied wrong = Run("ups-wrong", other, patch);
    REQUIRE(!wrong.status.ok);
    REQUIRE(wrong.status.error.find(CrcText(Crc32(ROM.data(), ROM.size()))) != std::string::npos);
    REQUIRE(wrong.status.error.find(CrcText(Crc32(other.data(), other.size()))) != std::string::npos);

    // The patched game given as the base.
    REQUIRE(Run("ups-again", hack, patch).status.error.find("already") != std::string::npos);

    // A damaged patch fails its own checksum.
    Bytes damaged = patch;
    damaged[damaged.size() / 2] ^= 1;
    REQUIRE(!Run("ups-damaged", ROM, damaged).status.ok);
}

TEST_CASE("BPS: the four actions and the checks") {
    const Bytes hack = MakeHack(ROM, 7, 4096);
    const Bytes patch = MakeBps(ROM, hack, "<hack name=\"test\"/>");
    Applied applied = Run("bps", ROM, patch);
    REQUIRE(applied.status.ok);
    REQUIRE(applied.bytes == hack);
    REQUIRE(applied.info.format == Format::Bps);
    REQUIRE(*applied.info.source_crc32 == Crc32(ROM.data(), ROM.size()));
    REQUIRE(*applied.info.target_size == hack.size());

    // By hand: source read, patch bytes, a block from elsewhere in the source going forward
    // then back, a copy of the result so far and a run that repeats its own bytes.
    Bytes target;
    Put(target, ROM.data(), 1000);
    const Bytes fresh = MakeRom(300, 99);
    Put(target, fresh.data(), fresh.size());
    Put(target, ROM.data() + 150000, 5000);
    Put(target, ROM.data() + 20000, 2500);
    const Bytes again = Slice(target, 500, 700);
    Put(target, again.data(), again.size());
    const std::size_t run_at = target.size();
    target.push_back(0x42);
    target.push_back(0x43);
    for (int i = 0; i < 100000; ++i) {
        target.push_back(target[run_at + i]);
    }
    BpsWriter w(ROM, target);
    w.SourceRead(1000);
    w.TargetRead(fresh.data(), fresh.size());
    w.SourceCopy(150000, 5000);
    w.SourceCopy(20000, 2500);
    w.TargetCopy(500, 700);
    w.TargetRead(target.data() + run_at, 2);
    w.TargetCopy(run_at, 100000);
    const Bytes by_hand = w.Finish(ROM, target);
    REQUIRE(Run("bps-hand", ROM, by_hand).bytes == target);

    Bytes other = ROM;
    other.back() ^= 1;
    const Applied wrong = Run("bps-wrong", other, patch);
    REQUIRE(!wrong.status.ok);
    REQUIRE(wrong.status.error.find("CRC32 " + CrcText(Crc32(ROM.data(), ROM.size()))) != std::string::npos);
    // Without the check the patch still applies, and then fails the target's checksum.
    REQUIRE(!Run("bps-forced", other, patch, false).status.ok);

    Bytes damaged = patch;
    damaged[40] ^= 0x10;
    REQUIRE(!Run("bps-damaged", ROM, damaged).status.ok);
    Bytes cut_short(patch.begin(), patch.begin() + 30);
    REQUIRE(!Run("bps-short", ROM, cut_short).status.ok);
}

TEST_CASE("APS for the GBA: blocks are checked by their CRC-16") {
    const Bytes hack = MakeHack(ROM, 8, 70000);
    const Bytes patch = MakeAps(ROM, hack);
    Applied applied = Run("aps", ROM, patch);
    REQUIRE(applied.status.ok);
    REQUIRE(applied.bytes == hack);
    REQUIRE(applied.info.format == Format::ApsGba);
    REQUIRE(*applied.info.source_size == ROM.size());
    REQUIRE(*applied.info.target_size == hack.size());

    Bytes other = ROM;
    other[100] ^= 0xFF;
    REQUIRE(!Run("aps-wrong", other, patch).status.ok);
    Bytes resized = ROM;
    resized.resize(ROM.size() - 4);
    REQUIRE(Run("aps-size", resized, patch).status.error.find(std::to_string(ROM.size())) != std::string::npos);
    // The N64 flavour is another format.
    Bytes n64 = patch;
    n64[4] = '0';
    const fs::path dir = FreshDir("aps-n64");
    WriteFile(dir / "p.aps", n64);
    REQUIRE(!Inspect(dir / "p.aps").ok());
}

TEST_CASE("PPF 1, 2 and 3") {
    const Bytes hack = MakeHack(ROM, 9);
    for (int version = 1; version <= 3; ++version) {
        const Bytes patch = MakePpf(ROM, hack, version);
        Applied applied = Run("ppf", ROM, patch);
        INFO("PPF " << version << ": " << applied.status.error);
        REQUIRE(applied.status.ok);
        REQUIRE(applied.bytes == hack);
        REQUIRE(applied.info.format == Format::Ppf);
        REQUIRE(applied.info.source_size.has_value() == (version == 2));

        Bytes other = ROM;
        other[0x9320 + 5] ^= 1;
        REQUIRE(Run("ppf-wrong", other, patch).status.ok == (version == 1));
        REQUIRE(Run("ppf-forced", other, patch, false).status.ok);
    }
    Bytes cut_short = MakePpf(ROM, hack, 1);
    cut_short.resize(cut_short.size() - 2);
    REQUIRE(!Run("ppf-short", ROM, cut_short).status.ok);
}

TEST_CASE("VCDIFF: whole patches as xdelta3 lays them out") {
    const Bytes hack = MakeHack(ROM, 10, 3000);
    const Bytes patch = MakeVcdiff(ROM, hack);
    Applied applied = Run("vcdiff", ROM, patch);
    INFO(applied.status.error);
    REQUIRE(applied.status.ok);
    REQUIRE(applied.bytes == hack);
    REQUIRE(applied.info.format == Format::Vcdiff);
    REQUIRE(applied.info.format_name == "xdelta");
    REQUIRE(*applied.info.target_size == hack.size());
    REQUIRE(!applied.info.source_crc32);

    // Another ROM gives other bytes: the window's Adler-32 says so.
    Bytes other = ROM;
    for (std::size_t i = 0; i < other.size(); i += 5000) {
        other[i] ^= 0x21;
    }
    const Applied wrong = Run("vcdiff-wrong", other, patch);
    REQUIRE(!wrong.status.ok);
    REQUIRE(wrong.status.error.find("another ROM") != std::string::npos);

    Bytes smaller(ROM.begin(), ROM.begin() + 1000);
    REQUIRE(!Run("vcdiff-small", smaller, patch).status.ok);
}

TEST_CASE("VCDIFF: address modes, paired instructions, copies inside the window and the result") {
    Bytes target;
    VcdiffWriter w;
    // Window 1: 40000 source bytes from 1000.
    w.Begin(1 | 4, 40000, 1000);
    const Bytes fresh = MakeRom(64, 5);
    const auto copy = [&](std::uint64_t address, std::size_t size, CopyMode mode) {
        w.Copy(address, ROM.data() + 1000 + address, size, mode);
        Put(target, ROM.data() + 1000 + address, size);
    };
    copy(0, 30, CopyMode::Self);
    copy(5000, 12, CopyMode::Here);
    copy(5100, 200, CopyMode::Recent);
    copy(5000, 9, CopyMode::Exact);
    copy(5100, 4, CopyMode::Exact);
    w.Add(fresh.data(), 3);
    Put(target, fresh.data(), 3);
    w.Add(fresh.data(), 40);
    Put(target, fresh.data(), 40);
    w.Run(0xC3, 500);
    target.insert(target.end(), 500, 0xC3);
    w.AddThenCopy(fresh.data() + 10, 2, 777, ROM.data() + 1777, 5);
    Put(target, fresh.data() + 10, 2);
    Put(target, ROM.data() + 1777, 5);
    w.CopyThenAdd(20000, ROM.data() + 21000, 0x99);
    Put(target, ROM.data() + 21000, 4);
    target.push_back(0x99);
    // A copy from this window: the first 30 bytes again.
    const Bytes start = Slice(target, 0, 30);
    w.Copy(40000, start.data(), 30, CopyMode::Self);
    Put(target, start.data(), 30);
    // And one that repeats the bytes it is writing.
    const std::size_t at = w.Written();
    Bytes repeated;
    for (int i = 0; i < 300; ++i) {
        repeated.push_back(i < 3 ? target[at - 3 + i] : repeated[i - 3]);
    }
    w.Copy(40000 + at - 3, repeated.data(), repeated.size(), CopyMode::Here);
    Put(target, repeated.data(), repeated.size());
    w.End();
    // Window 2: no source at all.
    w.Begin(4);
    w.Add(fresh.data(), 64);
    Put(target, fresh.data(), 64);
    w.End();
    // Window 3: copies from the result so far.
    const std::size_t so_far = target.size();
    w.Begin(2 | 4, 600, 100);
    const Bytes earlier = Slice(target, 150, 400);
    w.Copy(50, earlier.data(), 400, CopyMode::Self);
    Put(target, earlier.data(), 400);
    w.End();
    REQUIRE(so_far > 700);

    Applied applied = Run("vcdiff-hand", ROM, w.out);
    INFO(applied.status.error);
    REQUIRE(applied.status.ok);
    REQUIRE(applied.bytes == target);
    REQUIRE(*applied.info.target_size == target.size());
}

TEST_CASE("VCDIFF: what is refused") {
    // The header names a compressor, yet no section uses it: that applies.
    VcdiffWriter named(2);
    named.Begin(1 | 4, ROM.size(), 0);
    named.Copy(0, ROM.data(), 100);
    named.End();
    REQUIRE(Run("vcdiff-named", ROM, named.out).bytes == Bytes(ROM.begin(), ROM.begin() + 100));

    VcdiffWriter bad_check;
    bad_check.Begin(4);
    bad_check.Run(1, 50);
    bad_check.End(0, true);
    REQUIRE(!Run("vcdiff-check", ROM, bad_check.out).status.ok);

    Bytes cut_short = MakeVcdiff(ROM, MakeHack(ROM, 11));
    cut_short.resize(cut_short.size() - 7);
    REQUIRE(!Run("vcdiff-short", ROM, cut_short).status.ok);
}

TEST_CASE("files that are no patch, and cancelling") {
    const fs::path dir = FreshDir("not");
    WriteFile(dir / "notes.ips", Bytes{'h', 'e', 'l', 'l', 'o'});
    const auto info = Inspect(dir / "notes.ips");
    REQUIRE(!info.ok());
    REQUIRE(info.error().find("not a patch") != std::string::npos);
    REQUIRE(!Inspect(dir / "missing.bps").ok());
    REQUIRE(IsPatchExtension("BPS"));
    REQUIRE(IsPatchExtension("xdelta"));
    REQUIRE(!IsPatchExtension("gba"));

    const Bytes hack = MakeHack(ROM, 12);
    WriteFile(dir / "rom", ROM);
    WriteFile(dir / "p.bps", MakeBps(ROM, hack));
    ApplyOptions options;
    int calls = 0;
    options.progress = [&](float) {
        ++calls;
        return false;
    };
    const Status cancelled = Apply(dir / "p.bps", dir / "rom", dir / "out", options);
    REQUIRE(!cancelled.ok);
    REQUIRE(calls == 1);
    REQUIRE(!fs::exists(dir / "out"));
    REQUIRE(!fs::exists(dir / "out.part"));
    // The source cannot be the output.
    REQUIRE(!Apply(dir / "p.bps", dir / "rom", dir / "rom").ok);
    REQUIRE(ReadFile(dir / "rom") == ROM);
}

TEST_CASE("a large result is streamed") {
    // 48 MiB through the page cache and the write buffer, with far copies in both files.
    Bytes source = MakeRom(48u << 20, 21);
    Bytes target = source;
    std::memcpy(target.data() + (40u << 20), source.data() + (1u << 20), 2u << 20);
    std::memset(target.data() + (10u << 20), 0, 3u << 20);
    target[12345] ^= 0xFF;
    const fs::path dir = FreshDir("large");
    WriteFile(dir / "source", source);
    BpsWriter w(source, target);
    w.SourceRead(12345);
    w.TargetRead(target.data() + 12345, 1);
    w.SourceRead((10u << 20) - 12346);
    w.TargetRead(target.data() + (10u << 20), 1);
    w.TargetCopy(10u << 20, (3u << 20) - 1);
    w.SourceRead(27u << 20);
    w.SourceCopy(1u << 20, 2u << 20);
    w.SourceRead(6u << 20);
    WriteFile(dir / "p.bps", w.Finish(source, target));
    float last = 0;
    ApplyOptions options;
    options.progress = [&](float fraction) {
        REQUIRE(fraction >= last);
        last = fraction;
        return true;
    };
    const Status status = Apply(dir / "p.bps", dir / "source", dir / "out", options);
    INFO(status.error);
    REQUIRE(status.ok);
    REQUIRE(last == 1.0f);
    REQUIRE(ReadFile(dir / "out") == target);
    WriteFile(dir / "p.xdelta", MakeVcdiff(source, target, 1u << 20));
    REQUIRE(Apply(dir / "p.xdelta", dir / "source", dir / "out2").ok);
    REQUIRE(ReadFile(dir / "out2") == target);
    fs::remove_all(dir);
}
