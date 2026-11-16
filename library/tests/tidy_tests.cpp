// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "../../patching/tests/encoders.h"
#include "den/base/jobs.h"
#include "den/library/tidy.h"

using namespace den;
using namespace den::library;
using namespace den::library::tidy;
using namespace den::patching::testing;
namespace fs = std::filesystem;

namespace {

Bytes Text(std::string_view text) {
    return {text.begin(), text.end()};
}

struct Fixture {
    fs::path dir = fs::temp_directory_path() / "emuden-tidy-test";
    fs::path roms = dir / "roms", data = dir / "user";
    Jobs jobs{2};
    std::unique_ptr<Library> library;

    Fixture() {
        fs::remove_all(dir);
        WriteFile(roms / "gba" / "space_quest_v1.2.gba", MakeRom(8192, 1));
        WriteFile(roms / "gba" / "space_quest_v1.2.png", Text("cover"));
        WriteFile(roms / "gba" / "messy_synced_game.gba", MakeRom(8192, 2));
        WriteFile(roms / "gbc" / "Wario Land 3 (World) (En,Ja).gbc", MakeRom(8192, 3));
        WriteFile(roms / "gbc" / "scph1001.bin", MakeRom(512, 4)); // a BIOS name: never touched
        // A Mac's resource fork beside a game, and a ".DS_Store".
        WriteFile(roms / "gba" / "._space_quest_v1.2.gba", Bytes{0x00, 0x05, 0x16, 0x07, 0, 2, 0, 0});
        WriteFile(roms / "gba" / ".DS_Store", Text("junk"));
        // "._" without the AppleDouble bytes is someone's file: it stays.
        WriteFile(roms / "gba" / "._notes.txt", Text("mine"));
        fs::create_directories(roms / "old stuff" / "empty");
        fs::create_directories(roms / "psx"); // a console's folder stays even when empty
        // The synced game's saves are online under its file's name.
        WriteFile(data / "sync" / "gdrive" / "journal.json",
                  Text(R"({"version":1,"files":{"saves/gba/messy_synced_game.srm":{"size":8}}})"));
        Config config;
        config.data_dir = data;
        config.jobs = &jobs;
        config.offline = true;
        library = CreateLibrary(config);
        REQUIRE(library->AddFolder(PathToUtf8(roms)).ok);
        Scan();
    }
    void Scan() {
        jobs.Wait(library->Scan());
    }
    std::optional<Entry> AtPath(const fs::path& file) {
        for (const Entry& entry : library->Entries()) {
            if (PathFromUtf8(entry.path) == file) {
                return entry;
            }
        }
        return std::nullopt;
    }
    const Change* Find(const Plan& plan, ChangeKind kind, const fs::path& from) {
        for (const Change& change : plan.changes) {
            if (change.kind == kind && change.from == from) {
                return &change;
            }
        }
        return nullptr;
    }
};

} // namespace

TEST_CASE("tidy: the synced stems come from the sync journals") {
    Fixture f;
    const std::vector<std::string> stems = SyncedStems(f.data);
    CHECK(std::find(stems.begin(), stems.end(), "gba/messy_synced_game") != stems.end());
}

TEST_CASE("tidy: clean names, play time, saves and settings move to the new id; undo takes it back") {
    Fixture f;
    const auto quest = f.AtPath(f.roms / "gba" / "space_quest_v1.2.gba");
    REQUIRE(quest);
    f.library->SetFavourite(quest->id, true);
    f.library->RecordPlay(quest->id, 1794000000, 600);
    WriteFile(f.data / "saves" / "gba" / (quest->id.substr(0, 3) + "_" + quest->id.substr(4) + ".srm"), Text("save"));
    WriteFile(f.data / "states" / "gba" / (quest->id.substr(0, 3) + "_" + quest->id.substr(4) + ".resume.state"),
              Text("state"));

    const Plan plan = MakePlan(*f.library, f.data);
    const Change* rename = f.Find(plan, ChangeKind::Rename, f.roms / "gba" / "space_quest_v1.2.gba");
    REQUIRE(rename);
    CHECK(rename->to == f.roms / "gba" / "Space Quest.gba");
    const Change* shown = f.Find(plan, ChangeKind::ShowName, f.roms / "gba" / "messy_synced_game.gba");
    REQUIRE(shown);
    CHECK(shown->name == "Messy Synced Game");
    CHECK(f.Find(plan, ChangeKind::RemoveJunk, f.roms / "gba" / "._space_quest_v1.2.gba"));
    CHECK(f.Find(plan, ChangeKind::RemoveJunk, f.roms / "gba" / ".DS_Store"));
    CHECK_FALSE(f.Find(plan, ChangeKind::RemoveJunk, f.roms / "gba" / "._notes.txt"));
    CHECK(f.Find(plan, ChangeKind::RemoveFolder, f.roms / "old stuff" / "empty"));
    CHECK(f.Find(plan, ChangeKind::RemoveFolder, f.roms / "old stuff"));
    CHECK_FALSE(f.Find(plan, ChangeKind::RemoveFolder, f.roms / "psx"));
    for (const Change& change : plan.changes) {
        CHECK(change.from.filename() != "Wario Land 3 (World) (En,Ja).gbc"); // tidy already
        CHECK(change.from.filename() != "scph1001.bin");
    }

    std::vector<Moved> moved;
    const Done done = Apply(*f.library, plan, f.data, [&](const Moved& m) { moved.push_back(m); });
    CHECK(done.errors.empty());
    CHECK(done.renamed == 1);
    CHECK(done.named == 1);
    f.Scan();
    const auto renamed = f.AtPath(f.roms / "gba" / "Space Quest.gba");
    REQUIRE(renamed);
    CHECK(renamed->id != quest->id);
    CHECK(renamed->name == "Space Quest");
    CHECK(renamed->favourite);
    CHECK(renamed->play_seconds == 600);
    CHECK(fs::exists(f.roms / "gba" / "Space Quest.png")); // the cover beside it went along
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].old_id == quest->id);
    CHECK(moved[0].new_id == renamed->id);
    const std::string base = renamed->id.substr(0, 3) + "_" + renamed->id.substr(4);
    CHECK(fs::exists(f.data / "saves" / "gba" / (base + ".srm")));
    CHECK(fs::exists(f.data / "states" / "gba" / (base + ".resume.state")));
    // The synced game keeps its file, under its clean name.
    const auto synced = f.AtPath(f.roms / "gba" / "messy_synced_game.gba");
    REQUIRE(synced);
    CHECK(synced->name == "Messy Synced Game");
    CHECK_FALSE(fs::exists(f.roms / "gba" / "._space_quest_v1.2.gba"));
    CHECK(fs::exists(f.roms / "gba" / "._notes.txt"));
    CHECK_FALSE(fs::exists(f.roms / "old stuff"));
    CHECK(fs::exists(f.roms / "psx"));
    CHECK(fs::exists(f.roms / "gbc" / "scph1001.bin"));
    CHECK(CanUndo(f.data));

    // Nothing left to do the second time.
    const Plan again = MakePlan(*f.library, f.data);
    CHECK(again.changes.empty());

    moved.clear();
    REQUIRE(Undo(*f.library, f.data, [&](const Moved& m) { moved.push_back(m); }).ok);
    f.Scan();
    const auto back = f.AtPath(f.roms / "gba" / "space_quest_v1.2.gba");
    REQUIRE(back);
    CHECK(back->id == quest->id);
    CHECK(back->play_seconds == 600);
    CHECK(back->favourite);
    CHECK(fs::exists(f.data / "saves" / "gba" / (quest->id.substr(0, 3) + "_" + quest->id.substr(4) + ".srm")));
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].new_id == quest->id);
    CHECK(f.AtPath(f.roms / "gba" / "messy_synced_game.gba")->name != "Messy Synced Game");
    CHECK_FALSE(CanUndo(f.data));
}

TEST_CASE("tidy: a hack patched before it came is linked to its game where it is; undo unlinks it") {
    const fs::path dir = fs::temp_directory_path() / "emuden-tidy-hack-test";
    fs::remove_all(dir);
    const auto gba = [&](const char* file, std::string_view title, std::string_view code, int seed) {
        Bytes rom = MakeRom(0x8000, seed);
        std::fill(rom.begin() + 0xA0, rom.begin() + 0xB2, 0);
        std::copy(title.begin(), title.end(), rom.begin() + 0xA0);
        std::copy(code.begin(), code.end(), rom.begin() + 0xAC);
        WriteFile(dir / "roms" / "gba" / file, rom);
        return dir / "roms" / "gba" / file;
    };
    gba("Pokemon - FireRed Version (USA, Europe).gba", "POKEMON FIRE", "BPRE", 1);
    const fs::path hack = gba("Pokemon Radical Red.gba", "RADICALRED", "BPRE", 2);
    gba("Pokemon FireRed (Rev 1).gba", "POKEMON FIRE", "BPRE", 3); // the same game: no hack
    Jobs jobs{2};
    Config config;
    config.data_dir = dir / "user";
    config.jobs = &jobs;
    config.offline = true;
    std::unique_ptr<Library> library = CreateLibrary(config);
    REQUIRE(library->AddFolder(PathToUtf8(dir / "roms")).ok);
    jobs.Wait(library->Scan());

    const Plan plan = MakePlan(*library, config.data_dir);
    std::vector<const Change*> links;
    for (const Change& change : plan.changes) {
        if (change.kind == ChangeKind::LinkHack) {
            links.push_back(&change);
        }
    }
    REQUIRE(links.size() == 1);
    CHECK(links[0]->from == hack);
    CHECK(links[0]->name == "Pokemon - FireRed Version");

    const Done done = Apply(*library, plan, config.data_dir, {});
    CHECK(done.linked == 1);
    jobs.Wait(library->Scan());
    std::string hack_of, base_id;
    for (const Entry& entry : library->Entries()) {
        if (PathFromUtf8(entry.path) == hack) {
            hack_of = entry.hack_of;
            CHECK(entry.name == "Pokemon Radical Red"); // the file stays, under its name
        }
        if (entry.name == "Pokemon - FireRed Version") {
            base_id = entry.id;
        }
    }
    CHECK(!base_id.empty());
    CHECK(hack_of == base_id);
    CHECK(fs::exists(hack));
    CHECK(MakePlan(*library, config.data_dir).changes.empty()); // linked once

    REQUIRE(Undo(*library, config.data_dir, {}).ok);
    jobs.Wait(library->Scan());
    for (const Entry& entry : library->Entries()) {
        if (PathFromUtf8(entry.path) == hack) {
            CHECK(entry.hack_of.empty());
        }
    }
}
