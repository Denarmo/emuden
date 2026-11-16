// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <tuple>

#include <catch2/catch_test_macros.hpp>

#include "../../patching/tests/encoders.h"
#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/jobs.h"
#include "den/library/downloads.h"
#include "den/library/hacks.h"

using namespace den;
using namespace den::library;
using namespace den::library::downloads;
using namespace den::patching::testing;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "emuden-downloads-test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// A zip of stored (uncompressed) files.
Bytes Zip(const std::vector<std::pair<std::string, Bytes>>& files) {
    Bytes out, central;
    for (const auto& [name, data] : files) {
        const std::uint32_t crc = Crc32(data.data(), data.size());
        const std::size_t offset = out.size();
        PutLe(out, 0x04034b50, 4), PutLe(out, 20, 2), PutLe(out, 0, 2), PutLe(out, 0, 2), PutLe(out, 0, 4);
        PutLe(out, crc, 4), PutLe(out, data.size(), 4), PutLe(out, data.size(), 4);
        PutLe(out, name.size(), 2), PutLe(out, 0, 2);
        Put(out, name);
        Put(out, data.data(), data.size());
        PutLe(central, 0x02014b50, 4), PutLe(central, 20, 2), PutLe(central, 20, 2), PutLe(central, 0, 2);
        PutLe(central, 0, 2), PutLe(central, 0, 4), PutLe(central, crc, 4);
        PutLe(central, data.size(), 4), PutLe(central, data.size(), 4), PutLe(central, name.size(), 2);
        PutLe(central, 0, 2), PutLe(central, 0, 2), PutLe(central, 0, 2), PutLe(central, 0, 2), PutLe(central, 0, 4);
        PutLe(central, offset, 4);
        Put(central, name);
    }
    const std::size_t start = out.size();
    Put(out, central.data(), central.size());
    PutLe(out, 0x06054b50, 4), PutLe(out, 0, 2), PutLe(out, 0, 2), PutLe(out, files.size(), 2);
    PutLe(out, files.size(), 2), PutLe(out, central.size(), 4), PutLe(out, start, 4), PutLe(out, 0, 2);
    return out;
}

Bytes Text(std::string_view text) {
    Bytes out;
    Put(out, text);
    return out;
}

// A raw (2352-byte sector) PlayStation data track: the sync pattern and the volume descriptor.
Bytes PlayStationTrack() {
    Bytes track(20 * 2352, 0);
    const std::uint8_t sync[] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    std::copy(std::begin(sync), std::end(sync), track.begin());
    const std::size_t pvd = 16 * 2352 + 24;
    track[pvd] = 1;
    std::memcpy(track.data() + pvd + 1, "CD001", 5);
    std::memcpy(track.data() + pvd + 8, "PLAYSTATION                     ", 32);
    return track;
}

struct Fixture {
    fs::path dir, roms, downloads;
    Jobs jobs{2};
    std::unique_ptr<Library> library;
    std::unique_ptr<Setup> setup;

    explicit Fixture(const char* name) : dir(FreshDir(name)), roms(dir / "roms"), downloads(dir / "user" / "downloads") {
        fs::create_directories(roms / "gba");
        fs::create_directories(roms / "psx");
    }
    void Open() {
        Config config;
        config.data_dir = dir / "user";
        config.jobs = &jobs;
        config.offline = true;
        library = CreateLibrary(config);
        REQUIRE(library->AddFolder(PathToUtf8(roms)).ok);
        jobs.Wait(library->Scan());
        setup = std::make_unique<Setup>(*library, downloads, dir / "user");
    }
    Outcomes Run(const std::string& name, const std::string& title = {}, const std::string& base = {}) {
        Request request;
        request.file = downloads / name;
        request.page_title = title;
        request.base_id = base;
        request.plugin = "emuden.browser";
        return setup->Run(request);
    }
    std::optional<Entry> Named(const std::string& name) {
        for (const Entry& entry : library->Entries()) {
            if (entry.name == name) {
                return entry;
            }
        }
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("downloads: the console of a file from its name and its bytes") {
    const fs::path dir = FreshDir("identify");
    const SystemTable systems;
    WriteFile(dir / "Space Game.gba", MakeRom(4096, 1));
    REQUIRE(Identify(dir / "Space Game.gba", systems).system == "gba");

    Bytes genesis = MakeRom(0x4000, 2);
    std::memcpy(genesis.data() + 0x100, "SEGA GENESIS    ", 16);
    WriteFile(dir / "Racer.bin", genesis);
    REQUIRE(Identify(dir / "Racer.bin", systems).system == "genesis");

    WriteFile(dir / "Track.bin", PlayStationTrack());
    REQUIRE(Identify(dir / "Track.bin", systems).system == "psx");

    Bytes cube(0x400, 0);
    const std::uint8_t magic[] = {0xC2, 0x33, 0x9F, 0x3D};
    std::copy(std::begin(magic), std::end(magic), cube.begin() + 0x1C);
    WriteFile(dir / "Cube.iso", cube);
    REQUIRE(Identify(dir / "Cube.iso", systems).system == "gamecube");

    // An image nothing in it names: the words around it decide, or nobody does.
    WriteFile(dir / "Mystery.iso", Bytes(0x10000, 0));
    REQUIRE(Identify(dir / "Mystery.iso", systems).kind == Kind::Unknown);
    REQUIRE(Identify(dir / "Mystery.iso", systems, {"PSP homebrew"}).system == "psp");

    WriteFile(dir / "notes.txt", Text("hello"));
    REQUIRE(Identify(dir / "notes.txt", systems).kind == Kind::Unknown);
    WriteFile(dir / "hack.bin.patch", MakeIps(MakeRom(100, 1), MakeRom(100, 2)));
    REQUIRE(Identify(dir / "hack.bin.patch", systems).kind == Kind::Patch);
}

TEST_CASE("downloads: BIOS files are known by name") {
    REQUIRE(IsBiosName("scph1001.bin"));
    REQUIRE(IsBiosName("SCPH-5501.BIN"));
    REQUIRE(IsBiosName("gba_bios.bin"));
    REQUIRE(IsBiosName("[BIOS] Nintendo Game Boy Advance (World).gba"));
    REQUIRE(IsBiosName("bios7.bin"));
    REQUIRE(IsBiosName("neogeo.zip"));
    REQUIRE_FALSE(IsBiosName("Space Game (World).gba"));
    REQUIRE_FALSE(IsBiosName("Biosphere (USA).bin"));
}

TEST_CASE("downloads: a hack's name and version from the page and the file") {
    REQUIRE(HackTitle("Pokemon Unbound | Hackdex", "Pokemon_Unbound_v2.1.1.bps") == "Pokemon Unbound");
    REQUIRE(HackTitle("Super Mario Odyssey 64 - Romhacking.net - Hacks", "SMO64_v1.2.zip") == "Super Mario Odyssey 64");
    REQUIRE(HackTitle("", "Crystal Clear (v2.0.0).bps") == "Crystal Clear");
    // A file host's page says nothing of the hack: the file does.
    REQUIRE(HackTitle("Radical_Red_4.1.zip - MediaFire", "Radical Red 4.1.bps") == "Radical Red");
    REQUIRE(HackTitle("Glazed - PokeCommunity Forums", "patch.ips") == "Glazed");
    REQUIRE(HackTitle("Release Release 1.1.4 \xC2\xB7 someone/Space_Quest_Legacy \xC2\xB7 GitHub",
                      "Space Quest Legacy V1.1.4 Patch.bps") == "Space Quest Legacy");
    REQUIRE(HackVersion("", "Pokemon_Unbound_v2.1.1.bps") == "2.1.1");
    REQUIRE(HackVersion("", "Space Quest Legacy V1.1.4 Patch.bps") == "1.1.4");
    REQUIRE(HackVersion("Gaia Version 3.2", "gaia.ips") == "3.2");
    REQUIRE(HackVersion("", "Radical Red 4.1.bps") == "4.1");
    REQUIRE(HackVersion("", "hack.ips").empty());
}

TEST_CASE("downloads: a game's clean name from the page, the file, the archive and the header") {
    const auto name = [](std::string page, std::string file, std::string archive = {}, std::string header = {}) {
        return GameName({std::move(page), std::move(file), std::move(archive), std::move(header)});
    };
    // A pre-patched hack: the page names it; the header names the game it was made from.
    CHECK(name("Pok\xC3\xA9mon Generation 3 Evolved | Pok\xC3\xA9" "Community", "gen3evolved-v1.0.17.gba",
               "gen3evolved-v1.0.17.zip", "Pokemon Fire") == "Pok\xC3\xA9mon Generation 3 Evolved");
    CHECK(name("", "gen3evolved-v1.0.17.gba") == "Gen 3 Evolved");
    CHECK(name("Pokemon Unbound - Hackdex", "pokemonunbound.gba") == "Pokemon Unbound");
    CHECK(name("Release Version 1.3 \xC2\xB7 AntonioND/ucity \xC2\xB7 GitHub", "ucity.gbc") == "Ucity");
    // File-name noise.
    CHECK(name("", "Super_Mario_Bros_Deluxe_(1).gbc") == "Super Mario Bros Deluxe");
    CHECK(name("", "[romsite.net] Tetris DX (patched).gbc") == "Tetris DX");
    CHECK(name("", "romsite.com_Shantae_Advance.gba") == "Shantae Advance");
    CHECK(name("", "Kirby%27s%20Dream%20Land%202.gb") == "Kirby's Dream Land 2");
    CHECK(name("", "Back.to.the.Future.nes") == "Back to the Future");
    CHECK(name("", "space_quest_remix_v2.1.gba") == "Space Quest Remix");
    // A page about something else does not name the game.
    CHECK(name("Top 10 GBA games of 2025 - Some Blog", "Space_Quest_Remix.gba") == "Space Quest Remix");
    // The archive when the file says nothing, the header when nothing else does.
    CHECK(name("", "rom.gba", "Lunar_Knights_Remix-v3.zip") == "Lunar Knights Remix");
    CHECK(name("", "3f2a9c1b7e.gba", "", HeaderName("POKEMON EMER")) == "Pokemon Emer");
    CHECK(name("", "3f2a9c1b7e.gba").size() > 0);

    CHECK(TidyFileName("Wario Land 3 (World) (En,Ja).gbc"));
    CHECK(TidyFileName("Pokemon - Emerald Version (USA, Europe).gba"));
    CHECK(TidyFileName("Tetris.gb"));
    CHECK_FALSE(TidyFileName("gen3evolved-v1.0.17.gba"));
    CHECK_FALSE(TidyFileName("Super_Mario.gbc"));
    CHECK_FALSE(TidyFileName("Tetris (1).gb"));
    CHECK_FALSE(TidyFileName("Game ab12cd34ef.gba"));
    CHECK_FALSE(TidyFileName("Kirby%20Dream.gb"));
    CHECK_FALSE(TidyFileName("Zelda DX (patched).gbc"));
    CHECK_FALSE(TidyFileName("[romsite.net] Tetris.gb"));
    CHECK_FALSE(TidyFileName("._Tetris.gb"));
    CHECK(HeaderName("POKEMON FIRE") == "Pokemon Fire");
    CHECK(HeaderName("    ").empty());
}

TEST_CASE("downloads: which of the user's games a hack's name fits") {
    REQUIRE(NameScore("Pokemon - Emerald Version", "Pokemon Emerald Rogue") == 1);
    REQUIRE(NameScore("Pokemon - Fire Red Version", "Gaia (FireRed hack)") == 1);
    REQUIRE(NameScore("Pokemon - Ruby Version", "Pokemon Emerald Rogue") == 0);
}

TEST_CASE("downloads: a game goes into its console's folder and can be taken back") {
    Fixture f("game");
    f.Open();
    WriteFile(f.downloads / "Space Game (World).gba", MakeRom(8192, 3));
    const Outcomes done = f.Run("Space Game (World).gba");
    REQUIRE(done.record.outcome == Outcome::Added);
    REQUIRE(done.record.message == "Space Game has been added to the library");
    REQUIRE(fs::exists(f.roms / "gba" / "Space Game (World).gba"));
    REQUIRE_FALSE(fs::exists(f.downloads / "Space Game (World).gba"));
    REQUIRE(f.Named("Space Game"));
    REQUIRE(f.setup->History().size() == 1);

    REQUIRE(f.setup->Undo(done.record.id).ok);
    REQUIRE(fs::exists(f.downloads / "Space Game (World).gba"));
    REQUIRE_FALSE(fs::exists(f.roms / "gba" / "Space Game (World).gba"));
    REQUIRE(f.setup->History().front().undone);
    REQUIRE_FALSE(f.setup->Undo(done.record.id).ok);
}

TEST_CASE("downloads: a game is filed under its clean name, a Mac's resource forks are not games") {
    Fixture f("clean");
    f.Open();
    const Bytes rom = MakeRom(65536, 21);
    WriteFile(f.downloads / "gen3evolved-v1.0.17.zip",
              Zip({{"gen3evolved-v1.0.17.gba", rom},
                   {"._gen3evolved-v1.0.17.gba", Text("Mac resource fork")},
                   {"__MACOSX/._gen3evolved-v1.0.17.gba", Text("Mac resource fork")}}));
    const Outcomes done = f.Run("gen3evolved-v1.0.17.zip", "Pokemon Generation 3 Evolved | PokeCommunity");
    INFO(done.record.message);
    REQUIRE(done.record.outcome == Outcome::Added);
    REQUIRE(done.record.games.size() == 1);
    CHECK(done.record.games.front().name == "Pokemon Generation 3 Evolved");
    CHECK(done.record.message == "Pokemon Generation 3 Evolved has been added to the library");
    CHECK(ReadFile(f.roms / "gba" / "Pokemon Generation 3 Evolved.gba") == rom);
    CHECK_FALSE(fs::exists(f.roms / "gba" / "gen3evolved-v1.0.17.gba"));
    CHECK_FALSE(fs::exists(f.roms / "gba" / "._gen3evolved-v1.0.17.gba"));

    // A tidy name stays as it is; a second copy of a messy one gets a free name.
    WriteFile(f.downloads / "Space Game (Europe).gba", MakeRom(8192, 22));
    CHECK(f.Run("Space Game (Europe).gba").record.outcome == Outcome::Added);
    CHECK(fs::exists(f.roms / "gba" / "Space Game (Europe).gba"));
    WriteFile(f.downloads / "Space_Game_(1).gba", MakeRom(8192, 23));
    CHECK(f.Run("Space_Game_(1).gba").record.outcome == Outcome::Added);
    CHECK(fs::exists(f.roms / "gba" / "Space Game.gba"));
}

TEST_CASE("downloads: undo removes the console folder the setup made") {
    Fixture f("made");
    f.Open();
    WriteFile(f.downloads / "Tiny Town.gbc", MakeRom(32768, 4));
    const Outcomes done = f.Run("Tiny Town.gbc");
    REQUIRE(done.record.outcome == Outcome::Added);
    REQUIRE(fs::exists(f.roms / "gbc" / "Tiny Town.gbc"));
    REQUIRE(f.setup->Undo(done.record.id).ok);
    REQUIRE(fs::exists(f.downloads / "Tiny Town.gbc"));
    CHECK_FALSE(fs::exists(f.roms / "gbc"));
    CHECK(fs::exists(f.roms / "gba")); // there before: kept
}

TEST_CASE("downloads: a zipped cue sheet stays with its tracks") {
    Fixture f("cue");
    f.Open();
    const std::string cue = "FILE \"Disc Game (USA) (Track 1).bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n"
                            "FILE \"Disc Game (USA) (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n";
    WriteFile(f.downloads / "Disc Game (USA).zip",
              Zip({{"Disc Game (USA).cue", Text(cue)},
                   {"Disc Game (USA) (Track 1).bin", PlayStationTrack()},
                   {"Disc Game (USA) (Track 2).bin", MakeRom(4704, 9)},
                   {"scph1001.bin", MakeRom(1024, 4)}}));
    const Outcomes done = f.Run("Disc Game (USA).zip");
    INFO(done.record.message);
    REQUIRE(done.record.outcome == Outcome::Added);
    const fs::path set = f.roms / "psx" / "Disc Game (USA)";
    REQUIRE(fs::exists(set / "Disc Game (USA).cue"));
    REQUIRE(fs::exists(set / "Disc Game (USA) (Track 1).bin"));
    REQUIRE(fs::exists(set / "Disc Game (USA) (Track 2).bin"));
    // The BIOS file is left out, the archive gone (its games are in the library).
    REQUIRE_FALSE(fs::exists(set / "scph1001.bin"));
    REQUIRE(done.record.message.find("BIOS") != std::string::npos);
    REQUIRE_FALSE(fs::exists(f.downloads / "Disc Game (USA).zip"));

    REQUIRE(f.setup->Undo(done.record.id).ok);
    REQUIRE(fs::exists(f.downloads / "Disc Game (USA).cue"));
    REQUIRE_FALSE(fs::exists(set));
}

TEST_CASE("downloads: a cue sheet waits for its track") {
    Fixture f("cue-wait");
    f.Open();
    WriteFile(f.downloads / "Lone.cue", Text("FILE \"Lone.bin\" BINARY\n  TRACK 01 MODE2/2352\n"));
    REQUIRE(f.Run("Lone.cue").record.outcome == Outcome::Waiting);
    WriteFile(f.downloads / "Lone.bin", PlayStationTrack());
    const Outcomes done = f.Run("Lone.bin");
    INFO(done.record.message);
    REQUIRE(done.record.outcome == Outcome::Added);
    REQUIRE(fs::exists(f.roms / "psx" / "Lone" / "Lone.cue"));
    REQUIRE(fs::exists(f.roms / "psx" / "Lone" / "Lone.bin"));
    // The wait was answered by the setup of the whole set.
    REQUIRE(f.setup->History().size() == 1);
}

TEST_CASE("downloads: BIOS and unknown files stay in Downloads") {
    Fixture f("left");
    f.Open();
    WriteFile(f.downloads / "gba_bios.bin", MakeRom(16384, 5));
    const Outcomes bios = f.Run("gba_bios.bin");
    REQUIRE(bios.record.outcome == Outcome::Bios);
    REQUIRE(bios.record.message.find("doesn't use BIOS") != std::string::npos);
    REQUIRE(fs::exists(f.downloads / "gba_bios.bin"));
    WriteFile(f.downloads / "readme.txt", Text("thanks for downloading"));
    REQUIRE(f.Run("readme.txt").record.outcome == Outcome::Unknown);
    REQUIRE(fs::exists(f.downloads / "readme.txt"));
}

TEST_CASE("downloads: a patch with a checksum becomes a hack of the user's game") {
    Fixture f("bps");
    const Bytes base = MakeRom(65536, 7);
    WriteFile(f.roms / "gba" / "Monster Quest (USA).gba", base);
    f.Open();
    WriteFile(f.downloads / "MQ_Remix_v1.0.bps", MakeBps(base, MakeHack(base, 1)));
    const Outcomes first = f.Run("MQ_Remix_v1.0.bps", "Monster Quest Remix | Hackdex");
    INFO(first.record.message);
    REQUIRE(first.record.outcome == Outcome::HackAdded);
    REQUIRE(first.record.games.size() == 1);
    const auto hack = f.library->Find(first.record.games.front().id);
    REQUIRE(hack);
    REQUIRE(hack->name == "Monster Quest Remix");
    REQUIRE(hack->hack_of == f.Named("Monster Quest")->id);
    // The patch stays; the original is untouched.
    REQUIRE(fs::exists(f.downloads / "MQ_Remix_v1.0.bps"));
    REQUIRE(ReadFile(f.roms / "gba" / "Monster Quest (USA).gba") == base);

    // A newer version replaces the hack and keeps its entry (and so its saves).
    WriteFile(f.downloads / "MQ_Remix_v1.1.bps", MakeBps(base, MakeHack(base, 2)));
    const Outcomes second = f.Run("MQ_Remix_v1.1.bps", "Monster Quest Remix | Hackdex");
    REQUIRE(second.record.outcome == Outcome::HackAdded);
    REQUIRE(second.record.replaced);
    REQUIRE(second.record.games.front().id == hack->id);
    REQUIRE(second.record.message == "Monster Quest Remix has been updated to 1.1");

    // Taking the hack back leaves no empty Hacks folder.
    REQUIRE(f.setup->Undo(second.record.id).ok);
    CHECK_FALSE(f.library->Find(hack->id));
    CHECK_FALSE(fs::exists(f.roms / "gba" / "Hacks"));
}

TEST_CASE("downloads: a patch without a checksum is matched by name, else the user picks") {
    Fixture f("ips");
    const Bytes emerald = MakeRom(65536, 11), ruby = MakeRom(65536, 12);
    WriteFile(f.roms / "gba" / "Pokemon - Emerald Version (USA).gba", emerald);
    WriteFile(f.roms / "gba" / "Pokemon - Ruby Version (USA).gba", ruby);
    f.Open();
    WriteFile(f.downloads / "rogue.ips", MakeIps(emerald, MakeHack(emerald, 3)));
    const Outcomes matched = f.Run("rogue.ips", "Pokemon Emerald Rogue - Romhacking.net");
    INFO(matched.record.message);
    REQUIRE(matched.record.outcome == Outcome::HackAdded);
    REQUIRE(f.library->Find(matched.record.games.front().id)->hack_of == f.Named("Pokemon - Emerald Version")->id);

    WriteFile(f.downloads / "mystery.ips", MakeIps(ruby, MakeHack(ruby, 4)));
    const Outcomes asked = f.Run("mystery.ips");
    REQUIRE(asked.record.outcome == Outcome::NeedsBase);
    REQUIRE(asked.choices.size() == 2);
    const Outcomes answered = f.Run("mystery.ips", {}, f.Named("Pokemon - Ruby Version")->id);
    REQUIRE(answered.record.outcome == Outcome::HackAdded);
    // The question is gone from the list: the answer replaced it.
    for (const Record& record : f.setup->History()) {
        REQUIRE(record.outcome != Outcome::NeedsBase);
    }
}

TEST_CASE("downloads: a patch for a game the user does not have stays") {
    Fixture f("missing");
    f.Open();
    const Bytes base = MakeRom(4096, 21);
    WriteFile(f.downloads / "hack.bps", MakeBps(base, MakeHack(base, 1)));
    const Outcomes done = f.Run("hack.bps");
    REQUIRE(done.record.outcome == Outcome::MissingBase);
    REQUIRE(fs::exists(f.downloads / "hack.bps"));
}

TEST_CASE("downloads: a ROM patched already becomes a hack of the user's game with its code") {
    Fixture f("prepatched");
    const auto gba = [](std::string_view title, std::string_view code, int seed) {
        Bytes rom = MakeRom(65536, seed);
        std::fill(rom.begin() + 0xA0, rom.begin() + 0xB2, 0);
        std::copy(title.begin(), title.end(), rom.begin() + 0xA0);
        std::copy(code.begin(), code.end(), rom.begin() + 0xAC);
        return rom;
    };
    WriteFile(f.roms / "gba" / "Monster Quest (USA).gba", gba("MONSTERQUEST", "AMQE", 31));
    f.Open();
    const Bytes hack_rom = gba("MQREMIX", "AMQE", 32);
    WriteFile(f.downloads / "mq_remix_v2.0.gba", hack_rom);
    const Outcomes done = f.Run("mq_remix_v2.0.gba", "Monster Quest Remix | Hackdex");
    INFO(done.record.message);
    REQUIRE(done.record.outcome == Outcome::HackAdded);
    CHECK(done.record.message == "Monster Quest Remix has been added to the library, made from Monster Quest");
    REQUIRE(done.record.games.size() == 1);
    const auto hack = f.library->Find(done.record.games.front().id);
    REQUIRE(hack);
    CHECK(hack->hack_of == f.Named("Monster Quest")->id);
    CHECK(ReadFile(f.roms / "gba" / "Hacks" / "Monster Quest Remix.gba") == hack_rom);
    CHECK_FALSE(fs::exists(f.downloads / "mq_remix_v2.0.gba"));

    // Taken back: the download returns to Downloads, the hack and its Hacks folder go.
    REQUIRE(f.setup->Undo(done.record.id).ok);
    CHECK(ReadFile(f.downloads / "mq_remix_v2.0.gba") == hack_rom);
    CHECK_FALSE(f.library->Find(hack->id));
    CHECK_FALSE(fs::exists(f.roms / "gba" / "Hacks"));

    // A game with the same code named as that game is a game, not a hack.
    WriteFile(f.downloads / "Monster Quest (Europe).gba", gba("MONSTERQUEST", "AMQE", 33));
    CHECK(f.Run("Monster Quest (Europe).gba").record.outcome == Outcome::Added);
}

TEST_CASE("downloads: a console's manifest magic decides before the app's own checks") {
    const fs::path dir = FreshDir("magic");
    SystemTable systems;
    systems.Merge(ParseSystems(R"({"id":"duo","name":"Duo","content":{"extensions":["bin"],
        "magic":[{"offset":2,"bytes":"ABCD","also":[{"offset":5,"bytes":"10","mask":"F0"}]}]}})"));
    Bytes rom(64, 0);
    rom[2] = 0xAB, rom[3] = 0xCD, rom[5] = 0x1F;
    WriteFile(dir / "Game.bin", rom);
    CHECK(MagicSystem(dir / "Game.bin", systems, {}) == "duo");
    CHECK(Identify(dir / "Game.bin", systems).system == "duo");
    rom[5] = 0x2F; // the masked part no longer fits
    WriteFile(dir / "Other.bin", rom);
    CHECK(MagicSystem(dir / "Other.bin", systems, {}).empty());

    // The official manifests' magic says what the app's own checks say.
    SystemTable official;
    for (const auto& plugin : fs::directory_iterator(DEN_BUNDLED_PLUGINS)) {
        if (const auto text = ReadTextFile(plugin.path() / "plugin.json")) {
            official.Merge(ParseSystems(*text));
        }
    }
    Bytes genesis = MakeRom(0x4000, 2);
    std::memcpy(genesis.data() + 0x100, "SEGA GENESIS    ", 16);
    Bytes mars = genesis;
    std::memcpy(mars.data() + 0x100, "SEGA 32X        ", 16);
    Bytes gba = MakeRom(0x200, 3);
    const std::uint8_t logo[] = {0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21};
    std::memcpy(gba.data() + 4, logo, sizeof logo);
    gba[0xB2] = 0x96;
    Bytes nds = MakeRom(0x200, 4);
    std::memcpy(nds.data() + 0xC0, logo, sizeof logo);
    Bytes gb = MakeRom(0x200, 5);
    const std::uint8_t gb_logo[] = {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B};
    std::memcpy(gb.data() + 0x104, gb_logo, sizeof gb_logo);
    gb[0x143] = 0x00;
    Bytes gbc = gb;
    gbc[0x143] = 0x80;
    Bytes gg = MakeRom(0x8000, 6);
    std::memcpy(gg.data() + 0x7FF0, "TMR SEGA", 8);
    gg[0x7FFF] = 0x6C;
    Bytes sms = gg;
    sms[0x7FFF] = 0x4C;
    for (const auto& [name, bytes, system] : std::vector<std::tuple<std::string, Bytes, std::string>>{
             {"a.bin", genesis, "genesis"}, {"b.bin", mars, "32x"}, {"c.bin", gba, "gba"}, {"d.bin", nds, "nds"},
             {"e.bin", gb, "gb"}, {"f.bin", gbc, "gbc"}, {"g.bin", gg, "gg"}, {"h.bin", sms, "sms"}}) {
        INFO(system);
        WriteFile(dir / name, bytes);
        CHECK(MagicSystem(dir / name, official, {}) == system);
        CHECK(SniffSystem(dir / name, {}) == system);
    }
}
