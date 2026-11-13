// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/base/hash.h"
#include "den/patching/checksum.h"
#include "encoders.h"

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

Checksums SumsOf(const Bytes& data) {
    return {Crc32(data.data(), data.size()), Md5Hex(data.data(), data.size()),
            Sha1Hex(data.data(), data.size()), data.size()};
}

void RequireSame(const Checksums& a, const Checksums& b) {
    REQUIRE(a.crc32 == b.crc32);
    REQUIRE(a.md5 == b.md5);
    REQUIRE(a.sha1 == b.sha1);
    REQUIRE(a.size == b.size);
}

Bytes Join(const Bytes& head, const Bytes& body) {
    Bytes all = head;
    all.insert(all.end(), body.begin(), body.end());
    return all;
}

} // namespace

TEST_CASE("SHA-1 of the standard vectors") {
    REQUIRE(Sha1Hex("", 0) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    REQUIRE(Sha1Hex("abc", 3) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    REQUIRE(Sha1Hex(two_blocks.data(), two_blocks.size()) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    Sha1 pieces;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        pieces.Update(chunk.data(), chunk.size());
    }
    REQUIRE(pieces.FinishHex() == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE("a plain ROM has one form") {
    const fs::path dir = FreshDir("plain");
    const Bytes rom = MakeRom(3u << 20, 3);
    WriteFile(dir / "game.gba", rom);
    const auto forms = RomChecksums(dir / "game.gba", "gba");
    REQUIRE(forms.ok());
    REQUIRE(forms.value().size() == 1);
    REQUIRE(forms.value()[0].form == "raw");
    RequireSame(forms.value()[0].sums, SumsOf(rom));
    WriteFile(dir / "check.bin", Bytes{'1', '2', '3', '4', '5', '6', '7', '8', '9'});
    REQUIRE(RomChecksums(dir / "check.bin", "gb").value()[0].sums.crc32 == 0xCBF43926u);
    REQUIRE(!RomChecksums(dir / "missing.gba", "gba").ok());
    bool asked = false;
    REQUIRE(!RomChecksums(dir / "game.gba", "gba", [&] { return asked = true; }).ok());
    REQUIRE(asked);
}

TEST_CASE("NES, FDS, SNES and Lynx headers are left out of the second form and put back") {
    struct Case {
        const char* system;
        Bytes header;
        std::size_t body;
    };
    Bytes ines = {'N', 'E', 'S', 0x1A, 2, 1, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    Bytes fds = {'F', 'D', 'S', 0x1A, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    Bytes lynx(64, 0);
    std::memcpy(lynx.data(), "LYNX", 4);
    const Case cases[] = {
        {"nes", ines, 40976}, {"nes", fds, 65500}, {"snes", Bytes(512, 0), 1u << 20}, {"lynx", lynx, 131072}};
    for (const Case& c : cases) {
        const fs::path dir = FreshDir("headers");
        const Bytes body = MakeRom(c.body, 17);
        const Bytes whole = Join(c.header, body);
        WriteFile(dir / "game.rom", whole);
        const auto forms = RomChecksums(dir / "game.rom", c.system);
        REQUIRE(forms.ok());
        REQUIRE(forms.value().size() == 2);
        RequireSame(forms.value()[0].sums, SumsOf(whole));
        const RomForm& bare = forms.value()[1];
        REQUIRE(bare.form == "headerless");
        REQUIRE(bare.header_bytes == c.header.size());
        REQUIRE(!bare.reordered);
        RequireSame(bare.sums, SumsOf(body));

        REQUIRE(WriteForm(dir / "game.rom", c.system, "headerless", dir / "bare.rom").ok);
        REQUIRE(ReadFile(dir / "bare.rom") == body);
        REQUIRE(WriteForm(dir / "game.rom", c.system, "raw", dir / "copy.rom").ok);
        REQUIRE(ReadFile(dir / "copy.rom") == whole);
        // A patch changed the bare ROM; the header goes back in front of the result.
        Bytes patched = body;
        patched[100] ^= 0xFF;
        patched.push_back(7);
        WriteFile(dir / "patched.rom", patched);
        REQUIRE(RestoreHeader(dir / "game.rom", bare, dir / "patched.rom").ok);
        REQUIRE(ReadFile(dir / "patched.rom") == Join(c.header, patched));
    }
    // Without a header there is nothing to leave out.
    const fs::path dir = FreshDir("headers-none");
    WriteFile(dir / "plain.sfc", MakeRom(1u << 20, 4));
    REQUIRE(RomChecksums(dir / "plain.sfc", "snes").value().size() == 1);
    WriteFile(dir / "plain.nes", MakeRom(40976, 4));
    REQUIRE(RomChecksums(dir / "plain.nes", "nes").value().size() == 1);
    REQUIRE(!WriteForm(dir / "plain.nes", "nes", "headerless", dir / "x").ok);
}

TEST_CASE("N64 ROMs in any byte order have the big-endian form") {
    const fs::path dir = FreshDir("n64");
    Bytes z64 = MakeRom(2u << 20, 64);
    const std::uint8_t magic[] = {0x80, 0x37, 0x12, 0x40};
    std::memcpy(z64.data(), magic, 4);
    Bytes v64 = z64;
    for (std::size_t i = 0; i < v64.size(); i += 2) {
        std::swap(v64[i], v64[i + 1]);
    }
    Bytes n64 = z64;
    for (std::size_t i = 0; i < n64.size(); i += 4) {
        std::swap(n64[i], n64[i + 3]);
        std::swap(n64[i + 1], n64[i + 2]);
    }
    REQUIRE(v64[0] == 0x37);
    REQUIRE(v64[1] == 0x80);
    REQUIRE(n64[0] == 0x40);
    REQUIRE(n64[3] == 0x80);
    WriteFile(dir / "game.z64", z64);
    WriteFile(dir / "game.v64", v64);
    WriteFile(dir / "game.n64", n64);

    REQUIRE(RomChecksums(dir / "game.z64", "n64").value().size() == 1);
    for (const char* name : {"game.v64", "game.n64"}) {
        const auto forms = RomChecksums(dir / name, "n64");
        REQUIRE(forms.value().size() == 2);
        const RomForm& big = forms.value()[1];
        REQUIRE(big.form == "z64");
        REQUIRE(big.reordered);
        REQUIRE(big.header_bytes == 0);
        RequireSame(big.sums, SumsOf(z64));
        REQUIRE(WriteForm(dir / name, "n64", "z64", dir / "out.z64").ok);
        REQUIRE(ReadFile(dir / "out.z64") == z64);
        // Nothing to put back: the result stays big-endian.
        REQUIRE(RestoreHeader(dir / name, big, dir / "out.z64").ok);
        REQUIRE(ReadFile(dir / "out.z64") == z64);
    }
    RequireSame(RomChecksums(dir / "game.v64", "n64").value()[0].sums, SumsOf(v64));
}
