// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/jobs.h"
#include "den/library/artwork.h"
#include "den/library/library.h"
#include "den/library/systems.h"

using namespace den;
using namespace den::library;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "emuden-library-test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void Put(const fs::path& file, std::string_view data = "rom") {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << data;
}

// The thumbnail collection, made of a map.
HttpFetcher FakeCollection(int* requests) {
    return [requests](const std::string& url) -> HttpAnswer {
        ++*requests;
        if (url == "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy/Named_Boxarts/") {
            return {200, R"(<a href="Tetris%20%28World%29%20%28Rev%201%29.png">Tetris</a>)"};
        }
        if (url == "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy/Named_Boxarts/"
                   "Tetris%20%28World%29%20%28Rev%201%29.png") {
            return {200, "\x89PNG\r\n\x1a\npicture"};
        }
        return {404, {}};
    };
}

} // namespace

TEST_CASE("the library scans, keeps records and remembers them") {
    const fs::path dir = FreshDir("library");
    const fs::path roms = dir / "roms";
    Put(roms / "gb" / "Tetris (World).gb");
    Put(roms / "gb" / "Homebrew Thing.gb");
    Put(roms / "gba" / "Advance Game (Europe).gba");
    Jobs jobs(2);
    int requests = 0;
    Config config;
    config.data_dir = dir / "user";
    config.jobs = &jobs;
    config.http = FakeCollection(&requests);
    std::string tetris_id;
    {
        auto library = CreateLibrary(config);
        // The first run adds no folder.
        CHECK(library->Folders().empty());
        CHECK(library->Entries().empty());
        CHECK_FALSE(library->AddFolder(PathToUtf8(dir / "missing")));
        const std::uint64_t before = library->Revision();
        REQUIRE(library->AddFolder(PathToUtf8(roms)));
        CHECK_FALSE(library->AddFolder(PathToUtf8(roms)));
        jobs.Wait(library->Scan());

        const std::vector<Entry> entries = library->Entries();
        REQUIRE(entries.size() == 3);
        CHECK(library->Revision() > before);
        CHECK(entries[0].name == "Advance Game");
        CHECK(entries[1].name == "Homebrew Thing");
        CHECK(entries[2].name == "Tetris");
        for (const Entry& entry : entries) {
            // Every game has a cover: a download, or a drawn one.
            CHECK(IsRegularFile(PathFromUtf8(entry.cover)));
        }
        CHECK_FALSE(entries[2].placeholder_cover);
        CHECK(ReadTextFile(PathFromUtf8(entries[2].cover))->ends_with("picture"));
        CHECK(entries[1].placeholder_cover);
        CHECK(entries[0].placeholder_cover);
        const std::vector<SystemInfo> systems = library->Systems();
        REQUIRE(systems.size() == 2);
        CHECK(systems[0].id == "gb");
        CHECK(systems[0].short_name == "GB");
        CHECK(systems[0].game_count == 2);
        CHECK(systems[1].name == "Game Boy Advance");

        tetris_id = entries[2].id;
        library->SetFavourite(tetris_id, true);
        library->RecordPlay(tetris_id, 1700000000, 120);
        library->RecordPlay(tetris_id, 1700000500, 30);
        library->RecordPlay(entries[0].id, 1700000100, 5);
        const auto tetris = library->Find(tetris_id);
        REQUIRE(tetris);
        CHECK(tetris->favourite);
        CHECK(tetris->play_seconds == 150);
        CHECK(tetris->play_count == 2);
        CHECK(tetris->last_played == 1700000500);
        REQUIRE(library->RecentlyPlayed(5).size() == 2);
        CHECK(library->RecentlyPlayed(5)[0].id == tetris_id);
        CHECK(library->RecentlyPlayed(1).size() == 1);
        CHECK(library->Favourites().size() == 1);
        // Every game has the date it was added, and every play is in the session log.
        CHECK(tetris->added > 0);
        CHECK(library->RecentlyAdded(2).size() == 2);
        REQUIRE(library->Sessions().size() == 3);
        CHECK(library->Sessions()[1].started == 1700000500);
        CHECK(library->Sessions(1700000400).size() == 1);
        // A game opened over another one: the first game's session ends after the second
        // one's and does not count its time.
        const std::string homebrew = entries[1].id;
        library->RecordPlay(tetris_id, 1700001100, 60);
        library->RecordPlay(homebrew, 1700001000, 300);
        CHECK(library->Find(homebrew)->play_seconds == 240);
        CHECK(library->Sessions().back().seconds == 240);
        CHECK(library->Find(tetris_id)->play_seconds == 210);
        CHECK_FALSE(library->Find("gb:none"));
    }
    {
        // A second run shows the last scan at once, with its records, and asks nothing again.
        const int asked = requests;
        Config offline = config;
        offline.jobs = nullptr;
        auto library = CreateLibrary(offline);
        CHECK(library->Folders().size() == 1);
        REQUIRE(library->Entries().size() == 3);
        const auto tetris = library->Find(tetris_id);
        REQUIRE(tetris);
        CHECK(tetris->favourite);
        CHECK(tetris->play_seconds == 210);
        CHECK(tetris->added > 0);
        CHECK(library->Sessions().size() == 5);
        CHECK_FALSE(tetris->cover.empty());
        CHECK(requests == asked);
        REQUIRE(library->RemoveFolder(PathToUtf8(roms)));
        CHECK(library->Entries().empty());
        CHECK_FALSE(library->RemoveFolder(PathToUtf8(roms)));
    }
    {
        // The records outlive the folder: the game comes back as it was.
        auto library = CreateLibrary(config);
        REQUIRE(library->AddFolder(PathToUtf8(roms / "gb"), "gb"));
        jobs.Wait(library->Scan());
        const auto tetris = library->Find(tetris_id);
        REQUIRE(tetris);
        CHECK(tetris->favourite);
        CHECK(library->Entries().size() == 2);
    }
}

TEST_CASE("the game folders of an earlier profile are taken over on the first run") {
    const fs::path dir = FreshDir("import");
    Put(dir / "games" / "gb" / "Tetris.gb");
    const std::string games = PathToUtf8(dir / "games");
    std::string forward = games;
    for (char& c : forward) {
        c = c == '\\' ? '/' : c;
    }
    Put(dir / "old" / "config" / "qt-config.ini",
        "[UI]\nPaths\\gamedirs\\size=4\nPaths\\gamedirs\\1\\path=SDMC\n"
        "Paths\\gamedirs\\1\\deep_scan=false\nPaths\\gamedirs\\2\\path=UserNAND\n"
        "Paths\\gamedirs\\3\\path=" + forward + "\nPaths\\gamedirs\\4\\path=" + forward + "/gone\n");
    Jobs jobs(2);
    Config config;
    config.data_dir = dir / "user";
    config.jobs = &jobs;
    config.offline = true;
    config.import_profile = dir / "old";
    {
        auto library = CreateLibrary(config);
        REQUIRE(library->Folders().size() == 1);
        jobs.Wait(library->Scan());
        REQUIRE(library->Entries().size() == 1);
        CHECK(library->Entries()[0].placeholder_cover);
        CHECK_FALSE(library->ImportFolders(dir / "nothing"));
        REQUIRE(library->RemoveFolder(games));
    }
    // Only the first run imports.
    auto library = CreateLibrary(config);
    CHECK(library->Folders().empty());
    const Status again = library->ImportFolders(dir / "old");
    CHECK(again.ok);
    CHECK(library->Folders().size() == 1);
}

TEST_CASE("placeholder covers are images") {
    const fs::path dir = FreshDir("placeholder");
    REQUIRE(WritePlaceholderCover(dir / "cover.png", "The Legend of Zelda", "snes"));
    const auto data = ReadBinaryFile(dir / "cover.png");
    REQUIRE(data);
    CHECK(data->size() > 200u * 300u * 3u);
    CHECK((*data)[1] == 'P');
}

TEST_CASE("a game file naming an id gets its cover by that id") {
    const fs::path dir = FreshDir("cover-by-id");
    Put(dir / "Some Game.steam", "620\n");
    Put(dir / "Odd.steam", "not an id!");
    std::vector<std::string> asked;
    Artwork art(dir / "cache", [&asked](const std::string& url) -> HttpAnswer {
        asked.push_back(url);
        if (url == "https://img.example/620/header.jpg") {
            return {200, "\xFF\xD8\xFFpicture"};
        }
        return {404, {}};
    });
    const std::vector<SystemDef> systems = ParseSystems(R"({"id":"steam","name":"Steam","extensions":["steam"],
        "artwork":{"cover_by_id":["https://img.example/{id}/tall.jpg","https://img.example/{id}/header.jpg",
                                  "http://plain.example/{id}.jpg"]}})");
    REQUIRE(systems.size() == 1);
    CHECK(systems[0].cover_by_id.size() == 2);
    Entry entry;
    entry.id = "a";
    entry.name = "Some Game";
    entry.system = "steam";
    entry.path = PathToUtf8(dir / "Some Game.steam");
    const ArtworkResult first = art.Resolve(entry, &systems[0], {});
    CHECK(first.source == ArtworkSource::Downloaded);
    CHECK(first.path.extension() == ".jpg");
    CHECK(asked.size() == 2);
    CHECK(art.Resolve(entry, &systems[0], {}).source == ArtworkSource::Cache);
    CHECK(asked.size() == 2);
    entry.path = PathToUtf8(dir / "Odd.steam");
    CHECK(art.Resolve(entry, &systems[0], {}).source == ArtworkSource::Placeholder);
    CHECK(asked.size() == 2);
}

// Prints what the library finds in real folders: den_library_tests "[.live]".
// EMUDEN_TEST_ROMS names the folder (D:/ROMS otherwise), EMUDEN_TEST_DATA the data folder.
TEST_CASE("live scan", "[.live]") {
    std::string roms = GetEnv("EMUDEN_TEST_ROMS");
    if (roms.empty()) {
        roms = "D:/ROMS";
    }
    const std::string data = GetEnv("EMUDEN_TEST_DATA");
    Jobs jobs;
    Config config;
    config.data_dir = data.empty() ? FreshDir("live") : PathFromUtf8(data);
    config.jobs = &jobs;
    config.offline = !GetEnv("EMUDEN_TEST_OFFLINE").empty();
    auto library = CreateLibrary(config);
    library->AddFolder(roms);
    jobs.Wait(library->Scan());
    std::map<std::string, std::vector<Entry>> by_system;
    for (Entry& entry : library->Entries()) {
        by_system[entry.system].push_back(std::move(entry));
    }
    for (const SystemInfo& system : library->Systems()) {
        std::printf("%s (%s): %d\n", system.name.c_str(), system.short_name.c_str(), system.game_count);
        for (const Entry& entry : by_system[system.id]) {
            std::printf("  %-45s %s  cover: %s\n      %s%s%s\n", entry.name.c_str(), entry.id.c_str(),
                        entry.placeholder_cover ? "placeholder"
                                                : PathToUtf8(PathFromUtf8(entry.cover).filename()).c_str(),
                        entry.path.c_str(), entry.archive_member.empty() ? "" : " > ",
                        entry.archive_member.c_str());
        }
    }
    CHECK(library->Folders().size() == 1);
}
