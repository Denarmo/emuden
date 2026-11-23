// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/library/content.h"
#include "den/library/names.h"
#include "den/library/scan.h"
#include "switch2_system.h"
#include "seven_zip_writer.h"

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

void Le(std::string& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

// A zip archive with stored (uncompressed) files.
std::string StoredZip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, central;
    for (const auto& [name, data] : files) {
        const std::uint32_t crc = Crc32(data.data(), data.size());
        const std::size_t offset = out.size();
        Le(out, 0x04034b50, 4), Le(out, 20, 2), Le(out, 0, 2), Le(out, 0, 2), Le(out, 0, 4);
        Le(out, crc, 4), Le(out, data.size(), 4), Le(out, data.size(), 4);
        Le(out, name.size(), 2), Le(out, 0, 2);
        out += name + data;
        Le(central, 0x02014b50, 4), Le(central, 20, 2), Le(central, 20, 2), Le(central, 0, 2);
        Le(central, 0, 2), Le(central, 0, 4), Le(central, crc, 4);
        Le(central, data.size(), 4), Le(central, data.size(), 4), Le(central, name.size(), 2);
        Le(central, 0, 2), Le(central, 0, 2), Le(central, 0, 2), Le(central, 0, 2), Le(central, 0, 4);
        Le(central, offset, 4);
        central += name;
    }
    const std::size_t start = out.size();
    out += central;
    Le(out, 0x06054b50, 4), Le(out, 0, 2), Le(out, 0, 2), Le(out, files.size(), 2);
    Le(out, files.size(), 2), Le(out, central.size(), 4), Le(out, start, 4), Le(out, 0, 2);
    return out;
}

// A Switch 2 package with one base game that has a title and an icon.
std::string Package(std::uint64_t program_id, std::string_view title) {
    std::string meta;
    Le(meta, 5, 2), Le(meta, 0, 2), Le(meta, title.size(), 4);
    meta += title;
    const std::string icon = "\xFF\xD8\xFFicon";
    Le(meta, 3, 2), Le(meta, 0, 2), Le(meta, icon.size(), 4);
    meta += icon;

    std::string out("NUP\x01", 4);
    Le(out, 0x80, 2), Le(out, 1, 2), Le(out, 0x80, 8);
    out.resize(0x80, '\0');
    out += std::string("TOC\x01", 4);
    Le(out, 1, 4);
    out.resize(0x80 + 0x30, '\0');
    std::string record;
    Le(record, program_id, 8);
    record.resize(0x20, '\0');
    Le(record, 0x80 + 0x30 + 0x40, 8), Le(record, meta.size(), 4);
    record.resize(0x40, '\0');
    return out + record + meta;
}

const Entry* ByName(const std::vector<Entry>& entries, std::string_view name) {
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [&](const Entry& e) { return e.name == name; });
    return found != entries.end() ? &*found : nullptr;
}

ScanOptions Options(const fs::path& root) {
    ScanOptions options;
    options.folders = {{PathToUtf8(root), {}}};
    options.region_order = DefaultRegionOrder();
    return options;
}

} // namespace

TEST_CASE("system folders and loose files") {
    const fs::path root = FreshDir("folders");
    Put(root / "GBA" / "Pokemon - Emerald Version (USA, Europe).gba");
    Put(root / "GBA" / "notes.txt");
    Put(root / "GBA" / "media" / "Hidden.gba");
    Put(root / "Game Boy" / "Tetris (World) (Rev 1).gb");
    Put(root / "genesis" / "Sonic.md");
    Put(root / "Loose Game (Europe).nds");
    Put(root / "readme.md");
    Put(root / "disc.iso"); // which console?
    Put(root / "holiday" / "Other.gba");

    const std::vector<Entry> entries = ScanFolders(Options(root));
    CHECK(entries.size() == 4);
    const Entry* emerald = ByName(entries, "Pokemon - Emerald Version");
    REQUIRE(emerald != nullptr);
    CHECK(emerald->system == "gba");
    CHECK(emerald->original_name == "Pokemon - Emerald Version (USA, Europe)");
    CHECK(emerald->id == RomId("gba", "Pokemon - Emerald Version (USA, Europe).gba"));
    CHECK(emerald->size_bytes == 3);
    REQUIRE(ByName(entries, "Tetris") != nullptr);
    CHECK(ByName(entries, "Tetris")->system == "gb");
    REQUIRE(ByName(entries, "Sonic") != nullptr);
    CHECK(ByName(entries, "Sonic")->system == "genesis");
    REQUIRE(ByName(entries, "Loose Game") != nullptr);
    CHECK(ByName(entries, "Loose Game")->system == "nds");

    // The ids do not change when the library folder moves.
    const fs::path moved = root.parent_path() / "folders-moved";
    fs::remove_all(moved);
    fs::rename(root, moved);
    const std::vector<Entry> again = ScanFolders(Options(moved));
    REQUIRE(ByName(again, "Tetris") != nullptr);
    CHECK(ByName(again, "Tetris")->id == ByName(entries, "Tetris")->id);
    CHECK(ByName(again, "Tetris")->path != ByName(entries, "Tetris")->path);

    // A folder named like a console, or given one, is that console's folder.
    ScanOptions direct = Options(moved / "GBA");
    CHECK(ScanFolders(direct).size() == 1);
    CHECK(ScanFolders(direct).front().id == emerald->id);
    direct.folders = {{PathToUtf8(moved / "holiday"), "gba"}};
    REQUIRE(ScanFolders(direct).size() == 1);
    CHECK(ScanFolders(direct).front().name == "Other");
}

TEST_CASE("console folders a folder or two down, as EmuDeck keeps them") {
    const fs::path root = FreshDir("nested");
    Put(root / "Emulation" / "roms" / "gb" / "Tetris (World) (Rev 1).gb");
    Put(root / "roms" / "gba" / "Golden Sun.gba");
    Put(root / "Photos" / "holiday" / "gba" / "Hidden.gba"); // Photos holds no console folder
    Put(root / "a" / "b" / "c" / "gba" / "Deep.gba");

    const std::vector<Entry> entries = ScanFolders(Options(root));
    CHECK(entries.size() == 2);
    REQUIRE(ByName(entries, "Tetris") != nullptr);
    CHECK(ByName(entries, "Tetris")->system == "gb");
    REQUIRE(ByName(entries, "Golden Sun") != nullptr);
    CHECK(ByName(entries, "Golden Sun")->system == "gba");
}

TEST_CASE("discs, cue sheets and playlists are one game each") {
    const fs::path root = FreshDir("discs");
    const fs::path psx = root / "psx";
    Put(psx / "Racer (Europe)" / "Racer (Europe).cue", "FILE \"Racer (Europe).bin\" BINARY\n  TRACK 01 MODE2/2352\n");
    Put(psx / "Racer (Europe)" / "Racer (Europe).bin");
    Put(psx / "Lonely (USA).bin");
    Put(psx / "Tracks (USA) (Track 1).bin");
    Put(psx / "Tracks (USA) (Track 2).bin");
    Put(psx / "Final Quest (USA) (Disc 1).cue", "FILE \"Final Quest (USA) (Disc 1).bin\" BINARY\n");
    Put(psx / "Final Quest (USA) (Disc 1).bin");
    Put(psx / "Final Quest (USA) (Disc 2).cue", "FILE \"Final Quest (USA) (Disc 2).bin\" BINARY\n");
    Put(psx / "Final Quest (USA) (Disc 2).bin");
    Put(psx / "Saga (Japan) (Disc 1).chd");
    Put(psx / "Saga (Japan) (Disc 2).chd");
    Put(psx / "Saga (Japan).m3u", "# discs\nSaga (Japan) (Disc 1).chd\nSaga (Japan) (Disc 2).chd\n");

    const std::vector<Entry> entries = ScanFolders(Options(root));
    CHECK(entries.size() == 6);
    REQUIRE(ByName(entries, "Racer") != nullptr);
    CHECK(ByName(entries, "Racer")->path.ends_with(".cue"));
    REQUIRE(ByName(entries, "Lonely") != nullptr);
    CHECK(ByName(entries, "Tracks") != nullptr);
    REQUIRE(ByName(entries, "Final Quest (Disc 1)") != nullptr);
    REQUIRE(ByName(entries, "Final Quest (Disc 2)") != nullptr);
    CHECK(ByName(entries, "Final Quest (Disc 2)")->disc == 2);
    REQUIRE(ByName(entries, "Saga") != nullptr);
    CHECK(ByName(entries, "Saga")->path.ends_with(".m3u"));

    const fs::path disc2 = psx / "Final Quest (USA) (Disc 2).cue";
    CHECK(DiscSetName(disc2) == "Final Quest (USA)");
    const std::vector<fs::path> set = DiscSet(disc2);
    REQUIRE(set.size() == 2);
    CHECK(set[0].filename() == "Final Quest (USA) (Disc 1).cue");
    CHECK(DiscSet(psx / "Lonely (USA).bin").size() == 1);
}

TEST_CASE("zipped ROMs") {
    const fs::path root = FreshDir("zips");
    Put(root / "gb" / "Packed (World).zip", StoredZip({{"readme.txt", "hi"}, {"Packed (World).gb", "GBDATA"}}));
    Put(root / "gb" / "Both (World).zip", StoredZip({{"Both (World).gb", "x"}}));
    Put(root / "gb" / "Both (World).gb");
    Put(root / "gb" / "Empty.zip", StoredZip({{"readme.txt", "hi"}}));
    Put(root / "Loose (USA).zip", StoredZip({{"Loose (USA).gba", "gba"}}));
    Put(root / "broken.zip", "not a zip");

    const std::vector<Entry> entries = ScanFolders(Options(root));
    CHECK(entries.size() == 3);
    const Entry* packed = ByName(entries, "Packed");
    REQUIRE(packed != nullptr);
    CHECK(packed->archive_member == "Packed (World).gb");
    REQUIRE(ByName(entries, "Both") != nullptr);
    CHECK(ByName(entries, "Both")->archive_member.empty());
    REQUIRE(ByName(entries, "Loose") != nullptr);
    CHECK(ByName(entries, "Loose")->system == "gba");

    const fs::path unpack = root / "unpacked";
    const auto rom = PrepareRom(*packed, unpack);
    REQUIRE(rom.ok());
    CHECK(rom.value().filename() == "Packed (World).gb");
    CHECK(ReadTextFile(rom.value()) == std::optional<std::string>("GBDATA"));
    const auto again = PrepareRom(*packed, unpack);
    REQUIRE(again.ok());
    CHECK(again.value() == rom.value());
    const auto plain = PrepareRom(*ByName(entries, "Both"), unpack);
    REQUIRE(plain.ok());
    CHECK(PathToUtf8(plain.value()) == ByName(entries, "Both")->path);
    CHECK_FALSE(IsSafeRelativePath("../evil.gb"));
    CHECK(IsSafeRelativePath("disc/game.cue"));
}

TEST_CASE("ROMs in a 7z") {
    const fs::path root = FreshDir("sevens");
    Put(root / "gb" / "Packed (World).7z",
        den::test::StoredSevenZip({{"readme.txt", "hi"}, {"Packed (World).gb", "GBDATA"}}));
    Put(root / "gb" / "Empty.7z", den::test::StoredSevenZip({{"readme.txt", "hi"}}));
    Put(root / "Loose (USA).7z", den::test::StoredSevenZip({{"Loose (USA).gba", "gba"}}));
    Put(root / "broken.7z", "7z\xBC\xAF\x27\x1C and nothing else");
    // Behind a password: it is listed, and left out because it cannot be unpacked.
    fs::copy_file(fs::path(DEN_ARCHIVE_FIXTURES) / "password.7z", root / "gb" / "Locked (World).7z");

    const std::vector<Entry> entries = ScanFolders(Options(root));
    CHECK(entries.size() == 2);
    const Entry* packed = ByName(entries, "Packed");
    REQUIRE(packed != nullptr);
    CHECK(packed->system == "gb");
    CHECK(packed->archive_member == "Packed (World).gb");
    REQUIRE(ByName(entries, "Loose") != nullptr);
    CHECK(ByName(entries, "Loose")->system == "gba");

    const fs::path unpack = root / "unpacked";
    const auto rom = PrepareRom(*packed, unpack);
    REQUIRE(rom.ok());
    CHECK(rom.value().filename() == "Packed (World).gb");
    CHECK(ReadTextFile(rom.value()) == std::optional<std::string>("GBDATA"));
    CHECK(ReadTextFile(rom.value().parent_path() / "readme.txt") == std::optional<std::string>("hi"));
    const auto again = PrepareRom(*packed, unpack);
    REQUIRE(again.ok());
    CHECK(again.value() == rom.value());
}

TEST_CASE("regional versions are one entry") {
    const fs::path root = FreshDir("regions");
    Put(root / "gbc" / "Mario Golf (USA).gbc");
    Put(root / "gbc" / "Mario Golf (Europe).gbc");
    Put(root / "gbc" / "Mario Golf (Japan).gbc");
    Put(root / "gbc" / "Same (USA).gbc");
    Put(root / "gbc" / "Same (USA) (Rev 1).gbc");

    const std::vector<Entry> entries = ScanFolders(Options(root));
    CHECK(entries.size() == 3);
    const Entry* golf = ByName(entries, "Mario Golf");
    REQUIRE(golf != nullptr);
    CHECK(golf->original_name == "Mario Golf (Europe)");
    CHECK(golf->other_versions.size() == 2);

    ScanOptions every = Options(root);
    every.region_order.clear();
    CHECK(ScanFolders(every).size() == 5);
}

TEST_CASE("local covers and cartridge titles") {
    const fs::path root = FreshDir("covers");
    Put(root / "gba" / "Game.gba", std::string(160, '\0') + "POKEMON EMER" + std::string(20, '\0'));
    Put(root / "gba" / "covers" / "Game.png", "png");
    Put(root / "gba" / "Bare.gba");
    const std::vector<Entry> entries = ScanFolders(Options(root));
    REQUIRE(entries.size() == 2);
    REQUIRE(ByName(entries, "Game") != nullptr);
    CHECK(ByName(entries, "Game")->cover.ends_with("Game.png"));
    CHECK(ByName(entries, "Bare")->cover.empty());
    // A hack keeps the picture beside it, whatever tags its file name carries.
    Put(root / "gba" / "Pokemon - Lazarus (v2.1).gba");
    Put(root / "gba" / "Pokemon Lazarus.jpg", "jpg");
    const std::vector<Entry> again = ScanFolders(Options(root));
    REQUIRE(ByName(again, "Pokemon - Lazarus") != nullptr);
    CHECK(ByName(again, "Pokemon - Lazarus")->cover.ends_with("Pokemon Lazarus.jpg"));
    CHECK(ByName(again, "Bare")->cover.empty());
    const SystemTable table;
    CHECK(ReadRomHeaderTitle(*table.Find("gba"), root / "gba" / "Game.gba") == "POKEMON EMER");
    CHECK(ReadRomHeaderTitle(*table.Find("gba"), root / "gba" / "Bare.gba").empty());
}

TEST_CASE("Switch 2 games") {
    const fs::path root = FreshDir("switch2");
    Put(root / "SWITCH 2" / "Some Dump Name (v1.1.1) [04004860224DEC000].nup",
        Package(0x0400486024DEC000, "The Legend of Zelda\xE2\x84\xA2: Ocarina of Time"));
    Put(root / "SWITCH 2" / "Mario Kart World [0400C3F00006E000].nsp");
    Put(root / "SWITCH 2" / "Unknown Game (v2).xci");
    Put(root / "Loose Package.nup", Package(0x0400000000001000, ""));

    ScanOptions options = Options(root);
    options.icons_dir = root / "icons";
    // Without its plugin nothing of it is found: the folder is no console's.
    CHECK(ScanFolders(options).empty());
    SystemTable systems;
    systems.Set(ParseSystems(SWITCH2_SYSTEM));
    options.systems = &systems;
    const std::vector<Entry> entries = ScanFolders(options);
    REQUIRE(entries.size() == 4);
    const Entry* zelda = ByName(entries, "The Legend of Zelda: Ocarina of Time");
    REQUIRE(zelda != nullptr);
    CHECK(zelda->id == "switch2:0400486024DEC000");
    CHECK(zelda->system == "switch2");
    CHECK(zelda->cover.ends_with("0400486024DEC000.jpg"));
    CHECK(IsRegularFile(PathFromUtf8(zelda->cover)));
    REQUIRE(ByName(entries, "Mario Kart World") != nullptr);
    CHECK(ByName(entries, "Mario Kart World")->id == "switch2:0400C3F00006E000");
    REQUIRE(ByName(entries, "Unknown Game") != nullptr);
    CHECK(ByName(entries, "Unknown Game")->id == RomId("switch2", "Unknown Game (v2).xci"));
    REQUIRE(ByName(entries, "Loose Package") != nullptr);
    CHECK(ByName(entries, "Loose Package")->id == "switch2:0400000000001000");
    CHECK_FALSE(ReadSwitch2Package(root / "SWITCH 2" / "Unknown Game (v2).xci", false));
}

TEST_CASE("Switch 1 games are their own console, beside Switch 2") {
    const fs::path root = FreshDir("switch1");
    Put(root / "SWITCH 2" / "Mario Tennis Fever [0400D4201C6EA000].nup", Package(0x0400D4201C6EA000, ""));
    Put(root / "SWITCH" / "Super Mario Odyssey [0100000000010000].nsp");
    Put(root / "SWITCH" / "Unknown Cart.xci");

    ScanOptions options = Options(root);
    options.icons_dir = root / "icons";
    SystemTable systems;
    std::vector<SystemDef> both = ParseSystems(SWITCH2_SYSTEM);
    for (SystemDef& def : ParseSystems(SWITCH_SYSTEM)) {
        both.push_back(std::move(def));
    }
    // The Switch 2 console of the plugin has only NUP packages now.
    both.front().extensions = {"nup"};
    systems.Set(std::move(both));
    options.systems = &systems;
    const std::vector<Entry> entries = ScanFolders(options);
    REQUIRE(entries.size() == 3);
    const Entry* odyssey = ByName(entries, "Super Mario Odyssey");
    REQUIRE(odyssey != nullptr);
    CHECK(odyssey->system == "switch");
    CHECK(odyssey->id == "switch:0100000000010000");
    const Entry* unknown = ByName(entries, "Unknown Cart");
    REQUIRE(unknown != nullptr);
    CHECK(unknown->system == "switch");
    CHECK(unknown->id == RomId("switch", "Unknown Cart.xci"));
    const Entry* tennis = ByName(entries, "Mario Tennis Fever");
    REQUIRE(tennis != nullptr);
    CHECK(tennis->system == "switch2");
    CHECK(tennis->id == "switch2:0400D4201C6EA000");
}
