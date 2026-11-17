// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <string>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_loadso.h>
#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/player/cheats.h"
#include "den/player/session.h"

using namespace den;
using namespace den::player;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "den_player_tests" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void WriteText(const fs::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
}

// What the test core was given since its last cheat reset ("0:on:CODE;...").
struct CoreCheats {
    SDL_SharedObject* library = SDL_LoadObject(DEN_TESTCORE_PATH);
    ~CoreCheats() {
        SDL_UnloadObject(library);
    }
    std::string Set() const {
        auto get = reinterpret_cast<const char* (*)()>(SDL_LoadFunction(library, "testcore_cheats"));
        return get ? get() : "?";
    }
    int Resets() const {
        auto get = reinterpret_cast<int (*)()>(SDL_LoadFunction(library, "testcore_cheat_resets"));
        return get ? get() : -1;
    }
};

constexpr const char* CHT = "# made by hand\r\n"
                            "cheats = 3\r\n"
                            "\r\n"
                            "cheat0_desc = \"Infinite lives\"\r\n"
                            "cheat0_code = \"7E0DBF:09 + 7E0DC0:01\"\r\n"
                            "cheat0_enable = true\r\n"
                            "cheat1_desc = \"Found by searching\"\r\n"
                            "cheat1_handler = \"1\"\r\n"
                            "cheat1_address = \"1234\"\r\n"
                            "cheat2_desc = \"Moon jump\"\r\n"
                            "cheat2_code = 8009C6E4 0063\r\n"
                            "cheat2_enable = false\r\n"
                            "cheat3_desc = \"Past the count\"\r\n"
                            "cheat3_code = \"FFFF\"\r\n";

} // namespace

TEST_CASE("RetroArch cheat files are read", "[player][cheats]") {
    auto parsed = ParseCht(CHT);
    REQUIRE(parsed);
    REQUIRE(parsed->size() == 2);
    CHECK((*parsed)[0] == Cheat{"Infinite lives", "7E0DBF:09+7E0DC0:01", true});
    CHECK((*parsed)[1] == Cheat{"Moon jump", "8009C6E4 0063", false});

    // A cheat without a name is called by its code.
    auto unnamed = ParseCht("cheats = 1\ncheat0_code = \"ABCD-1234\"\n");
    REQUIRE(unnamed);
    CHECK((*unnamed)[0].name == "ABCD-1234");

    CHECK_FALSE(ParseCht("this is no cheat file"));
    CHECK_FALSE(ParseCht("cheats = 0\n"));
    CHECK_FALSE(ParseCht("cheats = 1\ncheat0_desc = \"Nothing\"\n"));
}

TEST_CASE("cheat codes are tidied and checked", "[player][cheats]") {
    CHECK(CleanCode("  7E0DBF:09 +7E0DC0:01\n7E0DC1:02 ") == "7E0DBF:09+7E0DC0:01+7E0DC1:02");
    CHECK(CleanCode("+ 8009C6E4  0063 ++") == "8009C6E4 0063");
    CHECK_FALSE(CheatProblem({"Name", " + ", false}).empty());
    CHECK(CheatProblem({"", "SXIOPO", true}).empty());

    std::vector<Cheat> cheats = {{"", " SXIOPO ", true}};
    REQUIRE(TidyCheats(cheats));
    CHECK(cheats[0] == Cheat{"SXIOPO", "SXIOPO", true});
    std::vector<Cheat> empty = {{"Nothing", "", true}};
    CHECK_FALSE(TidyCheats(empty));

    // An import leaves out the codes the game already has.
    std::vector<Cheat> have = {{"Lives", "7E0DBF:09+7E0DC0:01", false}};
    CHECK(MergeCheats(have, ParseCht(CHT).value()) == 1);
    REQUIRE(have.size() == 2);
    CHECK(have[1].name == "Moon jump");
}

TEST_CASE("a game's cheats are kept in its own file", "[player][cheats]") {
    const fs::path data = FreshDir("cheat_files");
    const fs::path file = CheatFile(data, "snes", "game-1");
    CHECK(file == data / "cheats" / "snes" / "game-1.json");
    REQUIRE(ReadCheats(file));
    CHECK(ReadCheats(file)->empty());

    const std::vector<Cheat> cheats = {{"Lives", "7E0DBF:09", true}, {"Jump \"high\"", "8009C6E4 0063", false}};
    REQUIRE(WriteCheats(file, cheats));
    auto read = ReadCheats(file);
    REQUIRE(read);
    CHECK(*read == cheats);

    REQUIRE(WriteCheats(file, {}));
    CHECK_FALSE(IsRegularFile(file));

    WriteText(file, "not json");
    CHECK_FALSE(ReadCheats(file));
}

TEST_CASE("a session gives the core its cheats", "[player][cheats]") {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const fs::path data = FreshDir("cheat_session");
    const fs::path rom = data / "Game.tst";
    WriteText(rom, "A");
    WriteText(data / "Game.cht", CHT);
    Config config;
    config.data_dir = data;
    auto session = CreateSession(config);
    OpenRequest request;
    request.core = PathFromUtf8(DEN_TESTCORE_PATH);
    request.content = rom;
    request.game_id = "game-1";
    request.system = "test";
    request.resume = false;
    const CoreCheats core;

    // The .cht beside the ROM is taken in, and the enabled cheat is given after the game loaded.
    REQUIRE(session->Open(request));
    Snapshot snapshot = session->GetSnapshot();
    CHECK(snapshot.can_cheats);
    CHECK(snapshot.cheats_off.empty());
    CHECK(snapshot.cheats_active == 1);
    REQUIRE(session->Cheats().size() == 2);
    CHECK(IsRegularFile(CheatFile(data, "test", "game-1")));
    CHECK(core.Set() == "0:on:7E0DBF:09+7E0DC0:01;");

    // A change resets the core's cheats and sets the enabled ones again.
    std::vector<Cheat> cheats = session->Cheats();
    cheats[0].enabled = false;
    cheats[1].enabled = true;
    cheats.push_back({"Added", " 1234:56 ", true});
    int resets = core.Resets();
    REQUIRE(session->SetCheats(cheats));
    CHECK(core.Resets() == resets + 1);
    CHECK(core.Set() == "0:on:8009C6E4 0063;1:on:1234:56;");
    CHECK(session->GetSnapshot().cheats_active == 2);
    cheats.push_back({"Empty", "  ", true});
    CHECK_FALSE(session->SetCheats(cheats));
    CHECK(session->Cheats().size() == 3);

    // Hardcore turns them off, and they are refused until it ends.
    session->SetHardcore(true);
    CHECK(core.Set().empty());
    snapshot = session->GetSnapshot();
    CHECK_FALSE(snapshot.cheats_off.empty());
    CHECK(snapshot.cheats_active == 0);
    CHECK_FALSE(session->SetCheats({}));
    session->SetHardcore(false);
    CHECK(core.Set() == "0:on:8009C6E4 0063;1:on:1234:56;");

    // A reset of the game keeps them.
    session->Reset();
    CHECK(core.Set() == "0:on:8009C6E4 0063;1:on:1234:56;");
    session->Close();

    // The next start has them from the game's file, not the .cht again.
    REQUIRE(session->Open(request));
    CHECK(session->Cheats().size() == 3);
    CHECK(core.Set() == "0:on:8009C6E4 0063;1:on:1234:56;");
    session->Close();

    // A console whose manifest says no cheats has none.
    request.allowed.cheats = false;
    REQUIRE(session->Open(request));
    CHECK_FALSE(session->GetSnapshot().can_cheats);
    CHECK(session->Cheats().empty());
    CHECK_FALSE(session->SetCheats({{"Lives", "7E0DBF:09", true}}));
    session->Close();
}
