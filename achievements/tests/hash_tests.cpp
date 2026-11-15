// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <vector>

#include "den/achievements/games.h"
#include "den/base/files.h"
#include "den/base/hash.h"

using namespace den;
using namespace den::achievements;

namespace {

std::vector<std::uint8_t> Bytes(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<std::uint8_t>((i * 31 + seed) & 0xff);
    }
    return out;
}

} // namespace

TEST_CASE("a Game Boy ROM is hashed whole") {
    const auto rom = Bytes(32 * 1024, 7);
    const auto hash = HashBuffer(ConsoleFor("gb"), rom);
    REQUIRE(hash.ok());
    CHECK(hash.value() == Md5Hex(rom.data(), rom.size()));
}

TEST_CASE("an NES ROM is hashed without its iNES header") {
    std::vector<std::uint8_t> rom = {'N', 'E', 'S', 0x1A, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto body = Bytes(48 * 1024, 3);
    rom.insert(rom.end(), body.begin(), body.end());
    const auto hash = HashBuffer(ConsoleFor("nes"), rom);
    REQUIRE(hash.ok());
    CHECK(hash.value() == Md5Hex(body.data(), body.size()));
    CHECK(hash.value() != Md5Hex(rom.data(), rom.size()));
}

TEST_CASE("a ROM file with a non-ASCII name hashes like its bytes") {
    const auto rom = Bytes(16 * 1024, 11);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "den-achievements-hash";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / PathFromUtf8("Pok\xC3\xA9mon \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88.gba");
    REQUIRE(WriteFileAtomic(file, std::string_view(reinterpret_cast<const char*>(rom.data()), rom.size())));
    const auto from_file = HashFile(ConsoleFor("gba"), file);
    const auto from_bytes = HashBuffer(ConsoleFor("gba"), rom);
    std::filesystem::remove_all(dir);
    REQUIRE(from_file.ok());
    REQUIRE(from_bytes.ok());
    CHECK(from_file.value() == from_bytes.value());
}

TEST_CASE("a console RetroAchievements does not cover is refused") {
    const auto rom = Bytes(1024, 1);
    CHECK_FALSE(HashBuffer(ConsoleFor("switch2"), rom).ok());
    CHECK_FALSE(HashFile(ConsoleFor("gb"), std::filesystem::temp_directory_path() / "no-such-rom.gb").ok());
}
