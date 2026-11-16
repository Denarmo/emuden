// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "../../patching/tests/encoders.h"
#include "den/base/files.h"
#include "den/library/rom_codes.h"

using namespace den;
using namespace den::library;
using namespace den::patching::testing;
namespace fs = std::filesystem;

namespace {

fs::path Dir() {
    const fs::path dir = fs::temp_directory_path() / "emuden-rom-codes-test";
    fs::create_directories(dir);
    return dir;
}

void Put(Bytes& rom, std::size_t at, std::string_view text) {
    std::memcpy(rom.data() + at, text.data(), text.size());
}

fs::path Gba(const char* file, std::string_view title, std::string_view code) {
    Bytes rom = MakeRom(0x8000, 5);
    std::memset(rom.data() + 0xA0, 0, 0x12);
    Put(rom, 0xA0, title);
    Put(rom, 0xAC, code);
    Put(rom, 0xB0, "01");
    const fs::path path = Dir() / file;
    WriteFile(path, rom);
    return path;
}

const ThumbnailIndex& Index() {
    static const ThumbnailIndex index = BuildThumbnailIndex({
        "Pokemon - FireRed Version (USA, Europe)",
        "Pokemon - FireRed Version (USA, Europe) (Rev 1)",
        "Pokemon - LeafGreen Version (USA, Europe)",
        "Pokemon - Emerald Version (USA, Europe)",
        "Pokemon - Ruby Version (USA, Europe) (Rev 2)",
        "Super Mario World (USA)",
    });
    return index;
}

} // namespace

TEST_CASE("rom codes: a hack renamed in its header keeps its base game's code") {
    const RomCode header = ReadRomCode(Gba("gen3evolved.gba", "GEN3EVOLVED", "BPRE"), "gba");
    CHECK(header.title == "GEN3EVOLVED");
    CHECK(header.code == "BPRE");
    CHECK(GameOfCode("gba", header.code) == "Pokemon - FireRed Version (USA, Europe)");
    CHECK(FindBaseGame(Index(), "gba", "Gen 3 Evolved", header) == "Pokemon - FireRed Version (USA, Europe)");
    // A Japanese or European code of a listed game: the same game's box.
    CHECK(GameOfCode("gba", "BPEJ") == "Pokemon - Emerald Version (USA, Europe)");
    // The collection's own name is used when its region or revision differs.
    CHECK(FindBaseGame(Index(), "gba", "Ruby Remix", {"POKEMON RUBY", "AXVE"}) ==
          "Pokemon - Ruby Version (USA, Europe) (Rev 2)");
}

TEST_CASE("rom codes: a changed code, a title that still names the game") {
    const RomCode header = ReadRomCode(Gba("odd.gba", "POKEMON LEAF", "ZQXE"), "gba");
    CHECK(GameOfCode("gba", header.code).empty());
    CHECK(FindBaseGame(Index(), "gba", "Crystal Dust", header) == "Pokemon - LeafGreen Version (USA, Europe)");
}

TEST_CASE("rom codes: nothing in the header, nothing found") {
    const RomCode header = ReadRomCode(Gba("blank.gba", "", ""), "gba");
    CHECK(header.title.empty());
    CHECK(header.code.empty());
    CHECK(FindBaseGame(Index(), "gba", "Some Homebrew", header).empty());
    CHECK(ReadRomCode(Dir() / "missing.gba", "gba").code.empty());
}

TEST_CASE("rom codes: Game Boy Color, N64 in any byte order, SNES with a copier header") {
    Bytes gbc = MakeRom(0x8000, 6);
    std::memset(gbc.data() + 0x134, 0, 0x10);
    Put(gbc, 0x134, "PM_CRYSTAL");
    Put(gbc, 0x13F, "BYTE");
    gbc[0x143] = 0xC0;
    WriteFile(Dir() / "crystal.gbc", gbc);
    const RomCode crystal = ReadRomCode(Dir() / "crystal.gbc", "gbc");
    CHECK(crystal.title == "PM_CRYSTAL");
    CHECK(crystal.code == "BYTE");
    CHECK(GameOfCode("gbc", crystal.code) == "Pokemon - Crystal Version (USA, Europe)");
    CHECK(GameOfTitle("gb", "POKEMON RED") == "Pokemon - Red Version (USA, Europe) (SGB Enhanced)");

    Bytes z64 = MakeRom(0x1000, 7);
    z64[0] = 0x80, z64[1] = 0x37, z64[2] = 0x12, z64[3] = 0x40;
    std::memset(z64.data() + 0x20, ' ', 20);
    Put(z64, 0x20, "SUPER MARIO 64");
    Put(z64, 0x3B, "NSME");
    Bytes v64 = z64;
    for (std::size_t i = 0; i + 1 < v64.size(); i += 2) {
        std::swap(v64[i], v64[i + 1]);
    }
    WriteFile(Dir() / "sm64.v64", v64);
    const RomCode sm64 = ReadRomCode(Dir() / "sm64.v64", "n64");
    CHECK(sm64.title == "SUPER MARIO 64");
    CHECK(sm64.code == "NSME");
    CHECK(GameOfCode("n64", sm64.code) == "Super Mario 64 (USA)");

    Bytes snes(512 + 0x10000, 0);
    const std::size_t at = 512 + 0x7FC0;
    Put(snes, at, "SUPER MARIOWORLD     ");
    snes[at + 0x1C] = 0x5F, snes[at + 0x1D] = 0x5F; // complement
    snes[at + 0x1E] = 0xA0, snes[at + 0x1F] = 0xA0; // checksum
    WriteFile(Dir() / "kaizo.smc", snes);
    const RomCode smw = ReadRomCode(Dir() / "kaizo.smc", "snes");
    CHECK(smw.title == "SUPER MARIOWORLD");
    CHECK(FindBaseGame(Index(), "snes", "Kaizo Mario World", smw) == "Super Mario World (USA)");
}

TEST_CASE("rom codes: only a name that names the game itself is the same game") {
    CHECK(NameOfGame("Pokemon FireRed", "Pokemon - FireRed Version (USA, Europe)"));
    CHECK(NameOfGame("pokemon_firered_usa", "Pokemon - FireRed Version (USA, Europe) (Rev 1)"));
    CHECK(NameOfGame("Pokemon FireRed v1.1", "Pokemon FireRed"));
    CHECK_FALSE(NameOfGame("Pokemon FireRed Plus", "Pokemon - FireRed Version (USA, Europe)"));
    CHECK_FALSE(NameOfGame("Pokemon Radical Red", "Pokemon FireRed"));
    CHECK_FALSE(NameOfGame("", "Pokemon FireRed"));
}

TEST_CASE("rom codes: a patched ROM is a hack of the library's game with its code") {
    const auto entry = [](const char* name, const fs::path& path) {
        Entry e;
        e.system = "gba";
        e.name = name;
        e.path = PathToUtf8(path);
        e.id = "gba:" + std::string(name);
        return e;
    };
    const std::vector<Entry> games = {
        entry("Pokemon - FireRed Version", Gba("Pokemon - FireRed Version (USA, Europe).gba", "POKEMON FIRE", "BPRE")),
        entry("Space Quest", Gba("Space Quest.gba", "SPACEQUEST", "ASQE")),
        entry("Homebrew One", Gba("Homebrew One.gba", "HOMEBREW1", "0000")),
    };
    // Renamed in its header, the code kept.
    const fs::path radical = Gba("radical_red_v4.1.gba", "RADICALRED", "BPRE");
    const auto base = FindHackBase(games, "gba", radical, "Pokemon Radical Red", ReadRomCode(radical, "gba"));
    REQUIRE(base);
    CHECK(base->name == "Pokemon - FireRed Version");
    // Another dump of the same game is not a hack.
    const fs::path rev = Gba("Pokemon FireRed (Rev 1).gba", "POKEMON FIRE", "BPRE");
    CHECK_FALSE(FindHackBase(games, "gba", rev, "Pokemon FireRed", ReadRomCode(rev, "gba")));
    // Its own file, another code, a placeholder code, no header: none.
    CHECK_FALSE(FindHackBase(games, "gba", PathFromUtf8(games[1].path), "Space Quest Deluxe",
                             ReadRomCode(PathFromUtf8(games[1].path), "gba")));
    const fs::path other = Gba("Other.gba", "OTHERGAME", "AOTE");
    CHECK_FALSE(FindHackBase(games, "gba", other, "Other", ReadRomCode(other, "gba")));
    const fs::path brew = Gba("Homebrew Two.gba", "HOMEBREW2", "0000");
    CHECK_FALSE(FindHackBase(games, "gba", brew, "Homebrew Two", ReadRomCode(brew, "gba")));
    CHECK_FALSE(FindHackBase(games, "gba", radical, "Pokemon Radical Red", RomCode{}));
}

TEST_CASE("rom codes: with a code no list knows, the base is the game named within the hack's name") {
    Entry quest;
    quest.system = "gba";
    quest.name = "Space Quest";
    quest.id = "gba:quest";
    quest.path = PathToUtf8(Gba("Space Quest (USA).gba", "SPACEQUEST", "ASQE"));
    const fs::path redux = Gba("Space Quest Redux.gba", "SPACEQUEST", "ASQE");
    const auto base = FindHackBase({quest}, "gba", redux, "Space Quest Redux", ReadRomCode(redux, "gba"));
    REQUIRE(base);
    CHECK(base->id == "gba:quest");
    // Renamed outright: which of the two is the hack cannot be told.
    const fs::path galaxy = Gba("Galaxy Run.gba", "GALAXYRUN", "ASQE");
    CHECK_FALSE(FindHackBase({quest}, "gba", galaxy, "Galaxy Run", ReadRomCode(galaxy, "gba")));
}
