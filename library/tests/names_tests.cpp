// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/library/artwork.h"
#include "den/library/names.h"
#include "den/library/scan.h"
#include "den/library/systems.h"

using namespace den::library;

TEST_CASE("ROM names are cleaned up") {
    CHECK(CleanDisplayName("Tetris (World) (Rev 1).gb") == "Tetris");
    CHECK(CleanDisplayName("Legend of Zelda, The - Ocarina of Time (USA) (Rev 2).z64") ==
          "The Legend of Zelda - Ocarina of Time");
    CHECK(CleanDisplayName("Wario_Land_3 (World) (En,Ja) [!].gbc") == "Wario Land 3");
    CHECK(CleanDisplayName("Dr. Mario") == "Dr. Mario");
    CHECK(CleanDisplayName("Super Mario Bros. Deluxe (Europe) (Rev 2).gbc") == "Super Mario Bros. Deluxe");
    CHECK(CleanDisplayName("(USA)") == "(USA)");
    CHECK(CleanStoreTitle("Portal\xE2\x84\xA2  2") == "Portal 2");
}

TEST_CASE("disc tags are read and kept apart from the name") {
    const ParsedName parsed = ParseGameName("Final Quest (USA) (Disc 2) (Rev 1).cue");
    CHECK(parsed.base == "Final Quest");
    CHECK(parsed.disc == 2);
    CHECK(parsed.display == "Final Quest (Disc 2)");
    CHECK(ParseGameName("Game (Disc B).chd").disc == 2);
    CHECK(ParseGameName("Game (CD1).cue").disc == 1);
    CHECK(ParseGameName("Game (Discovery Edition).iso").disc == 0);
    CHECK(NameBeforeDisc("Final Quest (USA) (Disc 2) (Rev 1).cue") == "Final Quest (USA)");
}

TEST_CASE("match keys and regions") {
    CHECK(MatchKey("Pok\xC3\xA9mon - Emerald Version (USA, Europe)") == "pokemonemeraldversion");
    CHECK(MatchKey("Mario & Luigi (Europe)") == "marioandluigi");
    CHECK(RegionTags("Game (USA, Europe) (Rev 1)") == std::vector<std::string>{"USA", "Europe"});
    CHECK(RegionTags("Game (En,Fr,De)").empty());
    const std::vector<std::string> order = DefaultRegionOrder();
    CHECK(RegionRank({"Europe"}, order) < RegionRank({"USA"}, order));
    CHECK(RegionRank({"Spain"}, order) == RegionRank({"Europe"}, order));
    CHECK(RegionRank({"World"}, order) == 0);
    CHECK(RegionRank({}, order) > RegionRank({"Japan"}, order));
}

TEST_CASE("ids are stable and tell games apart") {
    CHECK(RomId("gba", "Pokemon - Emerald.gba") == RomId("gba", "POKEMON - EMERALD.GBA"));
    CHECK(RomId("psx", "Game\\Game (Disc 1).cue") == RomId("psx", "game/game (disc 1).cue"));
    CHECK(RomId("gb", "Tetris.gb") != RomId("gbc", "Tetris.gb"));
    CHECK(RomId("gb", "Tetris.gb") != RomId("gb", "Tetris 2.gb"));
    CHECK(RomId("gb", "Tetris.gb").starts_with("gb:"));
    CHECK(RomId("gb", "Tetris.gb").size() == 3 + 16);
    CHECK(Switch2Id(0x0400486024DEC000) == "switch2:0400486024DEC000");
    CHECK(ProgramIdFromName("Game (v1.0) [0400486024DEC000]") == 0x0400486024DEC000);
    CHECK(ProgramIdFromName("Game [04004860224DEC000]") == 0);
    CHECK(ProgramIdFromName("Game [!]") == 0);
    CHECK(KnownSwitch2Title(0x0400C3F00006E000) == "Mario Kart World");
    CHECK(KnownSwitch2Title(1).empty());
}

TEST_CASE("the built-in consoles") {
    const SystemTable table;
    REQUIRE(table.Find("gba") != nullptr);
    CHECK(table.Find("gba")->name == "Game Boy Advance");
    CHECK(table.Find("gba")->short_name == "GBA");
    CHECK(table.Find("gba")->libretro_thumbnails == "Nintendo - Game Boy Advance");
    REQUIRE(table.Find("psx") != nullptr);
    REQUIRE(table.Find("psx")->multi_file);
    CHECK(table.Find("psx")->multi_file->loose_members);
    CHECK(table.FromFolderName("PSX") == table.Find("psx"));
    CHECK(table.FromFolderName("ps1") == table.Find("psx"));
    CHECK(table.FromFolderName("Game Boy Advance") == table.Find("gba"));
    CHECK(table.FromFolderName("SWITCH 2") == table.Find("switch2"));
    CHECK(table.FromFolderName("documents") == nullptr);
    CHECK(table.FromUniqueExtension("gba") == table.Find("gba"));
    CHECK(table.FromUniqueExtension("iso") == nullptr); // several consoles
    CHECK(table.FromUniqueExtension("md") == nullptr);  // only inside its folder
    CHECK(table.FromUniqueExtension("nup") == table.Find("switch2"));
    CHECK(table.RomExtension("celeste.p8.png") == "p8.png");
    CHECK(table.RomExtension("tetris (world).gb") == "gb");
}

TEST_CASE("plugin manifests extend the consoles") {
    SystemTable table;
    table.Merge(ParseSystems(R"({"id":"x.lynx","systems":[
        {"id":"lynx","name":"Atari Lynx","order":50,"content":{"extensions":["LNX"],"folder_aliases":["atarilynx"]}},
        {"id":"switch2","name":"Nintendo Switch 2","content":{"extensions":[],"scanner":"switch2"}}]})"));
    REQUIRE(table.Find("lynx") != nullptr);
    CHECK(table.FromUniqueExtension("lnx") == table.Find("lynx"));
    CHECK(table.FromFolderName("AtariLynx") == table.Find("lynx"));
    // A manifest without extensions keeps the built-in ones.
    CHECK(table.FromUniqueExtension("nup") == table.Find("switch2"));
    CHECK(table.Find("switch2")->short_name == "Switch 2");
}

TEST_CASE("a console whose plugin is removed leaves the table") {
    SystemTable table;
    table.Set(ParseSystems(R"json({"id":"x.steam","systems":[
        {"id":"steam","name":"Steam","content":{"extensions":["steam"]}},
        {"id":"gba","name":"Game Boy Advance (plugin)","content":{"extensions":["gba"]}}]})json"));
    REQUIRE(table.Find("steam") != nullptr);
    CHECK(table.Find("gba")->name == "Game Boy Advance (plugin)");
    table.Set({});
    CHECK(table.Find("steam") == nullptr);
    CHECK(table.Find("gba")->name == "Game Boy Advance");
}

TEST_CASE("box art is matched by name, region and base game") {
    const ThumbnailIndex index = BuildThumbnailIndex({
        "Tetris (World) (Rev 1)",
        "Tetris (Japan)",
        "Pokemon - Emerald Version (USA, Europe)",
        "Pokemon - Crystal Version (USA, Europe) (Rev 1)",
        "Pokemon Mystery Dungeon - Explorers of Sky (USA)",
        "Mario Golf (Europe)",
        "Mario Golf 2 (Japan)",
    });
    CHECK(MatchThumbnail(index, "Tetris (World) (Rev 1)") == "Tetris (World) (Rev 1)");
    CHECK(MatchThumbnail(index, "Tetris (Japan) (Beta)") == "Tetris (Japan)");
    CHECK(MatchThumbnail(index, "Mario Golf (USA)") == "Mario Golf (Europe)");
    CHECK(MatchThumbnail(index, "Pokemon Mystery Dungeon - Explorers of Skies") ==
          "Pokemon Mystery Dungeon - Explorers of Sky (USA)");
    CHECK(MatchThumbnail(index, "Pokemon - Lazarus").empty());
    CHECK(MatchBaseGame(index, "Pokemon - Lazarus", "POKEMON EMER") ==
          "Pokemon - Emerald Version (USA, Europe)");
    CHECK(MatchBaseGame(index, "Pokemon - Polished Crystal", "") ==
          "Pokemon - Crystal Version (USA, Europe) (Rev 1)");
    CHECK(MatchBaseGame(index, "Zelda - Something", "ZELDA").empty());

    const std::vector<std::string> names = ParseThumbnailIndex(
        R"(<a href="../">../</a><a href="Tetris%20%28World%29.png">x</a><a href="notes.txt">n</a>)");
    CHECK(names == std::vector<std::string>{"Tetris (World)"});
    CHECK(UrlEncodeSegment("Sony - PlayStation") == "Sony%20-%20PlayStation");
    CHECK(ThumbnailFileName("Mario & Luigi: Partners") == "Mario _ Luigi_ Partners");
}
