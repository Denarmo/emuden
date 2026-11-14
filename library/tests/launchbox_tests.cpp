// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <map>
#include <catch2/catch_test_macros.hpp>
#include "den/base/files.h"
#include "den/library/artwork.h"
#include "den/library/launchbox_covers.h"
#include "den/library/systems.h"

using namespace den;
using namespace den::library;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "den_launchbox_tests" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

const std::string DATA(LAUNCHBOX_DATA);

std::map<std::string, std::string> FakeData() {
    return {
        {DATA + "manifest.json",
         R"({"format":1,"version":"2026-11-14","images":"https://images.example/","face_kinds":["back"],"detail_kinds":["overview"],
             "libretro":{"Nintendo - Game Boy Advance":"Nintendo Game Boy Advance"},
             "platforms":{"Nintendo Game Boy Advance":{"dir":"nintendo-game-boy-advance","games":2,"buckets":["c","l"],
                                                       "device":"Platforms/gba.png"}}})"},
        {DATA + "covers/nintendo-game-boy-advance/c.json",
         R"({"castlevaniaariaofsorrow":[[3349,"Castlevania: Aria of Sorrow",
             [["Europe","eu.jpg"],["Japan","jp.png"],["North America","na.jpg"]]]]})"},
        {DATA + "covers/nintendo-game-boy-advance/l.json",
         R"({"legendofzeldaalinktothepastandfourswords":[[5,"The Legend of Zelda: A Link to the Past & Four Swords",[["","z.jpg"]]]]})"},
        {"https://images.example/eu.jpg", "\xFF\xD8\xFF" "europe"},
        {"https://images.example/na.jpg", "\xFF\xD8\xFF" "america"},
    };
}

SystemDef Gba() {
    SystemDef gba;
    gba.id = "gba";
    gba.libretro_thumbnails = "Nintendo - Game Boy Advance";
    return gba;
}

} // namespace

TEST_CASE("LaunchBox keys fold titles the way the data does") {
    CHECK(LaunchBoxKey("The Legend of Zelda: A Link to the Past") == "legendofzeldaalinktothepast");
    CHECK(LaunchBoxKey("Legend of Zelda, The - A Link to the Past") == "legendofzeldaalinktothepast");
    CHECK(LaunchBoxKey("Pokémon Emerald") == "pokemonemerald");
    CHECK(LaunchBoxKey("Mario & Luigi") == "marioandluigi");
    CHECK(LaunchBoxKey("Theme Park") == "themepark");
    CHECK(LaunchBoxKey("1942") == "1942");
}

TEST_CASE("LaunchBox covers come from the letter file the title needs, in the ROM's region") {
    const fs::path dir = FreshDir("find");
    const auto data = FakeData();
    std::vector<std::string> asked;
    LaunchBoxCovers covers(dir, [&](const std::string& url) -> HttpAnswer {
        asked.push_back(url);
        const auto found = data.find(url);
        return found == data.end() ? HttpAnswer{404, {}} : HttpAnswer{200, found->second};
    });
    bool answered = true;
    const auto europe = covers.Find(Gba(), {"Castlevania - Aria of Sorrow"}, {"Europe"}, answered);
    REQUIRE(europe);
    CHECK(europe->url == "https://images.example/eu.jpg");
    CHECK(europe->title == "Castlevania: Aria of Sorrow");
    const auto usa = covers.Find(Gba(), {"Castlevania - Aria of Sorrow"}, {"USA"}, answered);
    REQUIRE(usa);
    CHECK(usa->url == "https://images.example/na.jpg");
    // No region tag: North America first.
    CHECK(covers.Find(Gba(), {"Castlevania - Aria of Sorrow"}, {}, answered)->region == "North America");
    // A letter the manifest does not list is not asked for.
    CHECK_FALSE(covers.Find(Gba(), {"Metroid Fusion"}, {}, answered));
    CHECK(answered);
    CHECK(asked.size() == 2); // the manifest and c.json, each once
    CHECK(covers.Find(Gba(), {"Legend of Zelda, The - A Link to the Past & Four Swords"}, {}, answered));

    SystemDef unknown = Gba();
    unknown.libretro_thumbnails = "Sega - Saturn";
    CHECK_FALSE(covers.Find(unknown, {"Castlevania - Aria of Sorrow"}, {}, answered));

    // A later run reads the files it already has.
    asked.clear();
    LaunchBoxCovers again(dir, [&](const std::string& url) -> HttpAnswer {
        asked.push_back(url);
        return {0, {}};
    });
    CHECK(again.Find(Gba(), {"Castlevania - Aria of Sorrow"}, {"Europe"}, answered));
    CHECK(asked.empty());
    // The console's picture, from the manifest.
    CHECK(again.FindDevice(Gba(), answered) == "https://images.example/Platforms/gba.png");
    CHECK(again.FindDevice(unknown, answered).empty());
}

TEST_CASE("the artwork takes a LaunchBox cover before the libretro collection") {
    const fs::path dir = FreshDir("artwork");
    const auto data = FakeData();
    int requests = 0;
    Artwork art(dir / "cache", [&](const std::string& url) -> HttpAnswer {
        ++requests;
        const auto found = data.find(url);
        return found == data.end() ? HttpAnswer{404, {}} : HttpAnswer{200, found->second};
    });
    Entry entry;
    entry.id = "aos";
    entry.name = "Castlevania - Aria of Sorrow";
    entry.original_name = "Castlevania - Aria of Sorrow (USA)";
    entry.system = "gba";
    const SystemDef gba = Gba();
    const ArtworkResult first = art.Resolve(entry, &gba, {});
    CHECK(first.source == ArtworkSource::Downloaded);
    CHECK(ReadTextFile(first.path).value_or("").ends_with("america"));
    const int after = requests;
    CHECK(art.Resolve(entry, &gba, {}).source == ArtworkSource::Cache);
    CHECK(requests == after);
}

TEST_CASE("the other faces of a game come from its face file, the front cover's region first") {
    const fs::path dir = FreshDir("faces");
    auto data = FakeData();
    data[DATA + "manifest.json"] =
        R"({"format":1,"version":"2026-11-14","images":"https://images.example/","face_kinds":["back"],"detail_kinds":["overview"],
            "libretro":{"Nintendo - Game Boy Advance":"Nintendo Game Boy Advance"},
            "platforms":{"Nintendo Game Boy Advance":{"dir":"nintendo-game-boy-advance","games":2,
                         "buckets":["c","l"],"faces":["c"]}}})";
    data[DATA + "faces/nintendo-game-boy-advance/c.json"] =
        R"({"3349":{"back":[["Europe","eb.jpg"],["North America","nb.jpg"]],"cart":[["Japan","jc.png"]]}})";
    data["https://images.example/nb.jpg"] = "\xFF\xD8\xFF" "back";
    data["https://images.example/jc.png"] = "\x89PNG\r\n\x1A\n" "cart";
    int requests = 0;
    Artwork art(dir / "cache", [&](const std::string& url) -> HttpAnswer {
        ++requests;
        const auto found = data.find(url);
        return found == data.end() ? HttpAnswer{404, {}} : HttpAnswer{200, found->second};
    });
    Entry entry;
    entry.id = "aos";
    entry.name = "Castlevania - Aria of Sorrow";
    entry.original_name = "Castlevania - Aria of Sorrow (USA)";
    entry.system = "gba";
    const SystemDef gba = Gba();
    const GameFaces faces = art.Faces(entry, &gba, {});
    CHECK(ReadTextFile(PathFromUtf8(faces.back)).value_or("").ends_with("back"));
    CHECK(ReadTextFile(PathFromUtf8(faces.cart)).value_or("").ends_with("cart"));
    CHECK(faces.spine.empty());
    CHECK(faces.disc.empty());
    const int after = requests;
    CHECK(art.Faces(entry, &gba, {}).back == faces.back);
    CHECK(requests == after);

    // A game the face file does not list has none, and is not asked about again.
    Entry other = entry;
    other.name = "Legend of Zelda, The - A Link to the Past & Four Swords";
    other.original_name = other.name;
    CHECK(art.Faces(other, &gba, {}).back.empty());
    const int missed = requests;
    CHECK(art.Faces(other, &gba, {}).back.empty());
    CHECK(requests == missed);
}

TEST_CASE("a game's details come from its detail file and are kept") {
    const fs::path dir = FreshDir("details");
    auto data = FakeData();
    data[DATA + "manifest.json"] =
        R"({"format":1,"version":"2026-11-14","images":"https://images.example/","face_kinds":["back"],
            "detail_kinds":["overview"],"libretro":{"Nintendo - Game Boy Advance":"Nintendo Game Boy Advance"},
            "platforms":{"Nintendo Game Boy Advance":{"dir":"nintendo-game-boy-advance","games":2,
                         "buckets":["c","l"],"details":["c"]}}})";
    data[DATA + "details/nintendo-game-boy-advance/c.json"] =
        R"({"3349":{"overview":"Soma Cruz.","date":"2003-05-06","genres":["Action","Platform"],
                    "developer":"Konami","players":1,"rating":4.61,"votes":389}})";
    int requests = 0;
    const auto fetch = [&](const std::string& url) -> HttpAnswer {
        ++requests;
        const auto found = data.find(url);
        return found == data.end() ? HttpAnswer{404, {}} : HttpAnswer{200, found->second};
    };
    Entry entry;
    entry.id = "aos";
    entry.name = "Castlevania - Aria of Sorrow";
    entry.original_name = "Castlevania - Aria of Sorrow (USA)";
    entry.system = "gba";
    const SystemDef gba = Gba();
    {
        Artwork art(dir / "cache", fetch);
        const GameDetails details = art.Details(entry, &gba, {});
        CHECK(details.found);
        CHECK(details.title == "Castlevania: Aria of Sorrow");
        CHECK(details.overview == "Soma Cruz.");
        CHECK(details.year == 2003);
        CHECK(details.genres == std::vector<std::string>{"Action", "Platform"});
        CHECK(details.developer == "Konami");
        CHECK(details.publisher.empty());
        CHECK(details.players == 1);
        CHECK(details.votes == 389);
    }
    // Kept: a new run reads it without asking.
    const int after = requests;
    Artwork again(dir / "cache", fetch);
    const GameDetails kept = again.Details(entry, &gba, {});
    CHECK(kept.found);
    CHECK(kept.date == "2003-05-06");
    CHECK(kept.rating > 4.6);
    CHECK(requests == after);

    // A game without details has none, and is not asked about again.
    Entry other = entry;
    other.name = "Legend of Zelda, The - A Link to the Past & Four Swords";
    other.original_name = other.name;
    CHECK_FALSE(again.Details(other, &gba, {}).found);
    const int missed = requests;
    CHECK_FALSE(again.Details(other, &gba, {}).found);
    CHECK(requests == missed);
}
