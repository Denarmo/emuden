// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <filesystem>
#include <map>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/jobs.h"
#include "den/library/artwork.h"
#include "den/library/hack_covers.h"
#include "den/library/library.h"

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

const std::string PNG = "\x89PNG\r\n\x1a\npicture";
const std::string GBA_LIST = "https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/";

// The sources, made of a map; every request is noted.
struct FakeWeb {
    std::map<std::string, std::string> pages;
    std::vector<std::string> asked;
    std::vector<std::string> authorised;

    HeaderFetcher Fetcher() {
        return [this](const std::string& url, const HttpHeaders& headers) -> HttpAnswer {
            asked.push_back(url);
            for (const auto& [name, value] : headers) {
                if (name == "Authorization") {
                    authorised.push_back(value);
                }
            }
            const auto found = pages.find(url);
            return found == pages.end() ? HttpAnswer{404, {}} : HttpAnswer{200, found->second};
        };
    }
    HttpFetcher Plain() {
        return [fetch = Fetcher()](const std::string& url) { return fetch(url, {}); };
    }
};

FakeWeb WebWithLazarus() {
    FakeWeb web;
    web.pages[GBA_LIST] = R"(<a href="Pokemon%20-%20Emerald%20Version%20%28USA%2C%20Europe%29.png">x</a>)";
    web.pages[GBA_LIST + "Pokemon%20-%20Emerald%20Version%20%28USA%2C%20Europe%29.png"] = PNG;
    web.pages["https://api.github.com/repos/tttrash/pokehackcovers/git/trees/HEAD?recursive=1"] =
        R"({"tree":[{"path":"README.md"},{"path":"gba/Pokemon - Lazarus (alt).png"},)"
        R"({"path":"gba/Pokemon - Lazarus.png"},{"path":"gba/Pokemon - Emerald Seaglass.png"}]})";
    web.pages["https://raw.githubusercontent.com/tttrash/pokehackcovers/HEAD/gba/Pokemon%20-%20Lazarus.png"] = PNG;
    return web;
}

Entry Game(const char* name) {
    Entry entry;
    entry.id = std::string("gba:") + name;
    entry.name = name;
    entry.original_name = name;
    entry.system = "gba";
    return entry;
}

} // namespace

TEST_CASE("hack names are matched loosely, originals are not taken for hacks") {
    CHECK(MatchHackName("Pokemon - Lazarus", "Pok\xC3\xA9mon Lazarus").sure);
    CHECK(MatchHackName("Pokemon - Heart & Soul", "Pokemon - Heart and Soul (v1.2)").score == 100);
    CHECK(MatchHackName("Pokemon - Brown", "~Hack~ Pokemon - Brown Version [Subset]").sure);
    CHECK(MatchHackName("Pokemon - Polished Crystal (v3.0)", "Pokemon Polished Crystal").sure);
    // More words than the file name: sure only when the cartridge names them.
    CHECK(MatchHackName("Pokemon - Seaglass", "Pokemon - Emerald Seaglass", "POKEMON EMER").sure);
    CHECK_FALSE(MatchHackName("Pokemon - Seaglass", "Pokemon - Emerald Seaglass").sure);
    CHECK_FALSE(MatchHackName("Pokemon - Brown", "Pokemon - Whisky Brown").sure);
    // The game a hack was made from, and other games of the series.
    CHECK_FALSE(MatchHackName("Pokemon - Crystal Awakening", "Pokemon - Crystal Version").sure);
    CHECK(MatchHackName("Pokemon - Lazarus", "Pokemon - Emerald Version").score == 0);
    CHECK(MatchHackName("Pokemon - Ambrosia", "Pokemon - Lazarus").score == 0);
    CHECK(HasHackTag("Metroid - SR387 [Hack by OneOf99]"));
    CHECK(HasHackTag("Pokemon - Glazed Version (Hack)"));
    CHECK_FALSE(HasHackTag("Tetris (World)"));
}

TEST_CASE("a hack gets its own art, else the box of its original, else a placeholder") {
    const fs::path dir = FreshDir("hack-chain");
    FakeWeb web = WebWithLazarus();
    const SystemTable systems;
    const SystemDef* gba = systems.Find("gba");
    REQUIRE(gba != nullptr);
    Artwork artwork(dir, web.Plain(), web.Fetcher());

    const ArtworkResult own = artwork.Resolve(Game("Pokemon - Lazarus"), gba);
    CHECK(own.source == ArtworkSource::HackArt);
    CHECK(PathToUtf8(own.path.filename()).ends_with(".own.png"));
    // The second time nothing is asked.
    const std::size_t asked = web.asked.size();
    CHECK(artwork.Resolve(Game("Pokemon - Lazarus"), gba).source == ArtworkSource::Cache);
    CHECK(web.asked.size() == asked);

    // "Glazed" is known to be made from Emerald; no source has it.
    const ArtworkResult base = artwork.Resolve(Game("Pokemon - Glazed"), gba);
    CHECK(base.source == ArtworkSource::BaseGame);
    CHECK(fs::exists(dir / "hacks" / "gba"));
    // The miss is remembered: no source is asked again.
    const std::size_t after = web.asked.size();
    artwork.Resolve(Game("Pokemon - Glazed"), gba);
    CHECK(web.asked.size() == after);

    CHECK(artwork.Resolve(Game("Zelda - Something"), gba).source == ArtworkSource::Placeholder);

    // Online hack covers switched off: the cached own art stays, nothing new is asked.
    HackCoverOptions off;
    off.online = false;
    artwork.SetHackOptions(off);
    const std::size_t before_off = web.asked.size();
    CHECK(artwork.Resolve(Game("Pokemon - Emerald Seaglass"), gba).source == ArtworkSource::BaseGame);
    for (std::size_t i = before_off; i < web.asked.size(); ++i) {
        CHECK(web.asked[i].find("github") == std::string::npos);
    }
}

TEST_CASE("sources with a key are asked only with a key, and choices are downloaded") {
    const fs::path dir = FreshDir("hack-keys");
    FakeWeb web = WebWithLazarus();
    const std::string ra = "https://retroachievements.org/API/";
    web.pages[ra + "API_GetGameList.php?i=5&y=KEY"] =
        R"([{"Title":"~Hack~ Pokemon - Ambrosia","ID":77},{"Title":"Pokemon - Emerald Version","ID":3}])";
    web.pages[ra + "API_GetGame.php?i=77&y=KEY"] = R"({"Title":"x","ImageBoxArt":"/Images/012345.png"})";
    web.pages["https://media.retroachievements.org/Images/012345.png"] = PNG;
    const std::string grid = "https://www.steamgriddb.com/api/v2/";
    web.pages[grid + "search/autocomplete/Pokemon%20-%20Ambrosia"] =
        R"({"success":true,"data":[{"id":9,"name":"Pok\u00e9mon Ambrosia"}]})";
    web.pages[grid + "grids/game/9?dimensions=600x900,342x482,660x930&types=static"] =
        R"({"success":true,"data":[{"url":"https://cdn2.steamgriddb.com/grid/a.png"}]})";
    web.pages["https://cdn2.steamgriddb.com/grid/a.png"] = PNG;

    const SystemTable systems;
    const SystemDef* gba = systems.Find("gba");
    Artwork artwork(dir, web.Plain(), web.Fetcher());
    // No key: neither site is asked.
    CHECK(artwork.Resolve(Game("Pokemon - Ambrosia"), gba).source == ArtworkSource::Placeholder);
    for (const std::string& url : web.asked) {
        CHECK(url.find("retroachievements") == std::string::npos);
        CHECK(url.find("steamgriddb") == std::string::npos);
    }

    HackCoverOptions options;
    options.collections = DefaultHackCollections();
    options.retroachievements_key = "KEY";
    artwork.SetHackOptions(options);
    const std::vector<CoverChoice> choices = artwork.Choices(Game("Pokemon - Ambrosia"), gba);
    REQUIRE(choices.size() == 1);
    CHECK(choices[0].source == "retroachievements");
    CHECK(fs::exists(PathFromUtf8(choices[0].image)));

    options.retroachievements_key.clear();
    options.steamgriddb_key = "GRID";
    artwork.SetHackOptions(options);
    const std::vector<CoverChoice> grids = artwork.Choices(Game("Pokemon - Ambrosia"), gba);
    REQUIRE(grids.size() == 1);
    CHECK(grids[0].source == "steamgriddb");
    REQUIRE_FALSE(web.authorised.empty());
    CHECK(web.authorised.back() == "Bearer GRID");

    // A hack whose original is known gets the original as a last choice.
    const std::vector<CoverChoice> lazarus = artwork.Choices(Game("Pokemon - Glazed"), gba);
    REQUIRE_FALSE(lazarus.empty());
    CHECK(lazarus.back().source == "base");
}

TEST_CASE("the box of the original is tagged, and a picked cover is the game's") {
    const fs::path dir = FreshDir("hack-mark");
    REQUIRE(WritePlaceholderCover(dir / "box.png", "Pokemon Emerald", "gba"));
    REQUIRE(WriteHackMarkedCover(dir / "box.png", dir / "marked.png"));
    const auto plain = ReadBinaryFile(dir / "box.png");
    const auto marked = ReadBinaryFile(dir / "marked.png");
    REQUIRE(marked);
    CHECK((*marked)[1] == 'P');
    CHECK(plain);
    CHECK(IsHackBox("covers/x.hackbox.png"));
    CHECK_FALSE(IsHackBox("covers/x.png"));
    CHECK_FALSE(WriteHackMarkedCover(dir / "none.png", dir / "out.png"));

    const fs::path roms = dir / "roms";
    fs::create_directories(roms / "gba");
    WriteFileAtomic(roms / "gba" / "Pokemon - Lazarus.gba", "rom");
    Jobs jobs;
    Config config;
    config.data_dir = dir / "data";
    config.jobs = &jobs;
    config.offline = true;
    auto library = CreateLibrary(config);
    REQUIRE(library->AddFolder(PathToUtf8(roms)));
    jobs.Wait(library->Scan());
    REQUIRE(library->Entries().size() == 1);
    const std::string id = library->Entries()[0].id;
    CHECK(library->Entries()[0].placeholder_cover);
    CHECK_FALSE(library->SetCover(id, PathToUtf8(roms / "gba" / "Pokemon - Lazarus.gba")).ok);
    REQUIRE(library->SetCover(id, PathToUtf8(dir / "marked.png")).ok);
    CHECK_FALSE(library->Find(id)->placeholder_cover);
    // The pick outlives a scan: it lies where the scanner looks first.
    jobs.Wait(library->Scan());
    CHECK(PathFromUtf8(library->Find(id)->cover).filename() == "Pokemon - Lazarus.png");
    library->ResetCover(id);
    jobs.Wait(library->Scan());
    CHECK(library->Find(id)->placeholder_cover);
}

// What every unrecognised game of a real folder gets: den_library_tests "[.livehacks]".
// EMUDEN_TEST_ROMS names the folder (D:/ROMS otherwise). Asks the real sources, politely.
TEST_CASE("live hack covers", "[.livehacks]") {
    std::string roms = GetEnv("EMUDEN_TEST_ROMS");
    if (roms.empty()) {
        roms = "D:/ROMS";
    }
    const std::string data = GetEnv("EMUDEN_TEST_DATA");
    Jobs jobs;
    Config config;
    config.data_dir = data.empty() ? FreshDir("livehacks") : PathFromUtf8(data);
    config.jobs = &jobs;
    auto library = CreateLibrary(config);
    REQUIRE(library->AddFolder(roms));
    jobs.Wait(library->Scan());
    for (const Entry& entry : library->Entries()) {
        const std::string file = PathToUtf8(PathFromUtf8(entry.cover).filename());
        const bool hack = PathToUtf8(PathFromUtf8(entry.cover)).find("hacks") != std::string::npos;
        std::printf("%-5s %-34s %s\n", entry.system.c_str(), entry.original_name.c_str(),
                    entry.placeholder_cover ? "placeholder" : file.c_str());
        if (hack || entry.placeholder_cover) {
            for (const CoverChoice& choice : library->CoverChoices(entry.id)) {
                std::printf("        choice: %-18s %s\n", choice.source.c_str(), choice.title.c_str());
            }
        }
    }
    CHECK(library->Folders().size() == 1);
}
