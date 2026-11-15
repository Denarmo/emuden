// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sync engine with a provider in memory and a data folder of its own: portable names,
// the plan, conflicts, the journal, running games, PC consoles and the folder store.

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/jobs.h"
#include "den/store/settings.h"
#include "den/sync/engine.h"
#include "den/sync/names.h"
#include "den/sync/plan.h"
#include "den/sync/provider.h"

using namespace den;
using namespace den::sync;
namespace fs = std::filesystem;

namespace {

struct Scratch {
    fs::path dir;
    Scratch() {
        static std::atomic<int> next{0};
        dir = fs::temp_directory_path() / ("den-sync-test-" + std::to_string(std::rand()) + "-" + std::to_string(next++));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void Write(const fs::path& file, const std::string& data, std::int64_t time = 0) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << data;
    if (time > 0) {
        SetFileTime(file, time);
    }
}

std::string Read(const fs::path& file) {
    return ReadTextFile(file).value_or("<none>");
}

// The remote side in memory, shared by the "devices" of a test.
struct Cloud {
    struct File {
        std::string data;
        std::int64_t modified = 0;
        int rev = 0;
    };
    std::map<std::string, File> files;
    std::mutex mutex;
    int next_rev = 1;
    std::vector<std::string> puts;
};

class MemoryProvider final : public Provider {
public:
    explicit MemoryProvider(Cloud& cloud) : cloud_(cloud) {}
    std::string Id() const override {
        return "test.cloud";
    }
    Result<std::vector<RemoteFile>> List(const std::string& prefix) override {
        std::lock_guard lock(cloud_.mutex);
        std::vector<RemoteFile> out;
        for (const auto& [path, file] : cloud_.files) {
            if (path.starts_with(prefix)) {
                out.push_back({path, file.data.size(), file.modified, std::to_string(file.rev)});
            }
        }
        return out;
    }
    Status Get(const std::string& path, const fs::path& local, const Progress&) override {
        std::lock_guard lock(cloud_.mutex);
        const auto found = cloud_.files.find(path);
        if (found == cloud_.files.end()) {
            return Status::Fail("missing");
        }
        Write(local, found->second.data);
        return {};
    }
    Result<std::string> Put(const std::string& path, const fs::path& local, const std::string& rev,
                            std::int64_t modified) override {
        std::lock_guard lock(cloud_.mutex);
        auto& file = cloud_.files[path];
        if (!rev.empty() && file.rev != 0 && std::to_string(file.rev) != rev) {
            return Status::Fail("conflict");
        }
        file.data = Read(local);
        file.modified = modified;
        file.rev = cloud_.next_rev++;
        cloud_.puts.push_back(path);
        return std::to_string(file.rev);
    }
    Status Delete(const std::string& path) override {
        std::lock_guard lock(cloud_.mutex);
        cloud_.files.erase(path);
        return {};
    }

private:
    Cloud& cloud_;
};

library::Entry Game(const std::string& id, const std::string& system, const fs::path& file) {
    library::Entry entry;
    entry.id = id;
    entry.system = system;
    entry.path = PathToUtf8(file);
    entry.name = PathToUtf8(file.stem());
    return entry;
}

// One device: its data folder, settings and library.
struct Device {
    Scratch scratch;
    std::unique_ptr<store::Settings> settings;
    std::vector<library::Entry> entries;
    std::shared_ptr<Engine> engine;
    std::int64_t clock = 1'800'000'000;

    Device(Cloud& cloud, const std::string& name, std::vector<std::string> categories = {"saves", "states"}) {
        settings = std::make_unique<store::Settings>(scratch.dir / "settings.json");
        EngineConfig config;
        config.data_dir = scratch.dir;
        config.settings = settings.get();
        config.device = name;
        config.now = [this] { return clock; };
        config.entries = [this] { return entries; };
        config.providers = {{"test.cloud", "Cloud", std::move(categories)}};
        config.provider_factory = [&cloud](const std::string&) { return std::make_unique<MemoryProvider>(cloud); };
        engine = CreateEngine(config);
        settings->SetString("sync.active", "test.cloud");
    }
    fs::path Save(const std::string& id) const {
        return scratch.dir / "saves" / "gba" / (SafeFileName(id) + ".srm");
    }
    Status Sync() {
        return engine->RunOnce({}, nullptr);
    }
};

} // namespace

TEST_CASE("portable names come from the system and the ROM's stem") {
    const std::vector<library::Entry> entries = {
        Game("gba:01", "gba", "D:/ROMS/gba/Super Mario Bros. 3.gba"),
        Game("gba:02", "gba", "D:/ROMS/gba/Super Mario Bros.gba"),
        Game("gba:03", "gba", "E:/other/super mario bros.gba"), // the same stem: does not sync
        Game("steam:9", "steam", "C:/Steam/game.exe"),
    };
    const GameIndex games(entries, {"steam"});
    CHECK(games.Games().size() == 2);
    CHECK(games.Duplicates().size() == 1);
    CHECK(games.ByFileName("gba", "Super Mario Bros. 3.srm")->id == "gba:01");
    CHECK(games.ByFileName("gba", "Super Mario Bros.slot1.state")->id == "gba:02");
    const auto target = TargetOf("states/gba/Super Mario Bros. 3.slot2.png", games, "/data", "/games");
    REQUIRE(target);
    CHECK(target->file == fs::path("/data/states/gba/gba_01.slot2.png"));
    CHECK_FALSE(TargetOf("saves/steam/game.srm", games, "/data", "/games"));
    CHECK_FALSE(TargetOf("saves/gba/../x.srm", games, "/data", "/games"));
    CHECK(TargetOf("games/gba/New.gba", games, "/data", "/games")->file == fs::path("/games/gba/New.gba"));
    CHECK(IsPcPath("saves/steam/x.srm", {"steam"}));
    CHECK(IsPcPath("config/games/steam/x.json", {"steam"}));
    CHECK_FALSE(IsPcPath("saves/gba/Steam Deck.srm", {"steam"}));
}

TEST_CASE("a save goes up, comes down on the other device, and a conflict keeps the loser") {
    Cloud cloud;
    Device pc(cloud, "PC"), handheld(cloud, "Retroid");
    pc.entries = {Game("gba:aa", "gba", pc.scratch.dir / "roms" / "Emerald.gba")};
    handheld.entries = {Game("gba:bb", "gba", handheld.scratch.dir / "roms" / "Emerald.gba")};

    Write(pc.Save("gba:aa"), "pc save 1", 1'800'000'100);
    REQUIRE(pc.Sync().ok);
    CHECK(cloud.files.at("saves/gba/Emerald.srm").data == "pc save 1");

    REQUIRE(handheld.Sync().ok);
    CHECK(Read(handheld.Save("gba:bb")) == "pc save 1");
    CHECK(handheld.engine->State().downloaded == 1);

    // Nothing changed: nothing moves.
    cloud.puts.clear();
    REQUIRE(pc.Sync().ok);
    REQUIRE(handheld.Sync().ok);
    CHECK(cloud.puts.empty());

    // The handheld plays on; the PC gets it.
    Write(handheld.Save("gba:bb"), "handheld save 2", 1'800'000'200);
    REQUIRE(handheld.Sync().ok);
    REQUIRE(pc.Sync().ok);
    CHECK(Read(pc.Save("gba:aa")) == "handheld save 2");

    // Both change it: the newer one (the PC's) wins, the handheld keeps its own as a copy.
    Write(handheld.Save("gba:bb"), "handheld save 3", 1'800'000'300);
    Write(pc.Save("gba:aa"), "pc save 3", 1'800'000'400);
    REQUIRE(pc.Sync().ok);
    REQUIRE(handheld.Sync().ok);
    CHECK(Read(handheld.Save("gba:bb")) == "pc save 3");
    const std::vector<Conflict> conflicts = handheld.engine->Conflicts();
    REQUIRE(conflicts.size() == 1);
    CHECK(conflicts[0].path == "saves/gba/Emerald.srm");
    CHECK(conflicts[0].from == "Retroid");
    CHECK(Read(conflicts[0].file) == "handheld save 3");
    CHECK(PathToUtf8(conflicts[0].file.filename()).starts_with("Emerald.srm.conflict-Retroid-2027"));
    CHECK(handheld.engine->GameState("gba:bb").conflict);

    // "Use this one instead": the copy comes back and goes up; the replaced one is kept.
    REQUIRE(handheld.engine->UseConflict(0).ok);
    CHECK(Read(handheld.Save("gba:bb")) == "handheld save 3");
    REQUIRE(handheld.Sync().ok);
    CHECK(cloud.files.at("saves/gba/Emerald.srm").data == "handheld save 3");
    CHECK(handheld.engine->Conflicts().size() == 1);
    CHECK(Read(handheld.engine->Conflicts()[0].file) == "pc save 3");

    // The journal is on disk.
    CHECK(fs::is_regular_file(handheld.scratch.dir / "sync" / "test.cloud" / "journal.json"));
    CHECK(handheld.engine->GameState("gba:bb").synced > 0);
}

TEST_CASE("the same file new on both sides is agreed on without a conflict") {
    Cloud cloud;
    Device a(cloud, "A"), b(cloud, "B");
    a.entries = {Game("gba:1", "gba", "x/Game.gba")};
    b.entries = {Game("gba:2", "gba", "y/Game.gba")};
    Write(a.Save("gba:1"), "same");
    Write(b.Save("gba:2"), "same");
    REQUIRE(a.Sync().ok);
    REQUIRE(b.Sync().ok);
    CHECK(b.engine->Conflicts().empty());
    CHECK(cloud.puts.size() == 1);
}

TEST_CASE("nothing is deleted locally because it is missing remotely") {
    Cloud cloud;
    Device a(cloud, "A");
    a.entries = {Game("gba:1", "gba", "x/Game.gba")};
    Write(a.Save("gba:1"), "keep me");
    REQUIRE(a.Sync().ok);
    cloud.files.clear();
    REQUIRE(a.Sync().ok);
    CHECK(Read(a.Save("gba:1")) == "keep me");
    CHECK(cloud.files.at("saves/gba/Game.srm").data == "keep me"); // it went up again

    // A delete the user made in EmuDEN goes out.
    fs::remove(a.Save("gba:1"));
    a.engine->NoteDeleted(a.Save("gba:1"));
    REQUIRE(a.Sync().ok);
    CHECK_FALSE(cloud.files.contains("saves/gba/Game.srm"));
}

TEST_CASE("a running game's files are not replaced until it ends") {
    Cloud cloud;
    Device a(cloud, "A"), b(cloud, "B");
    a.entries = {Game("gba:1", "gba", "x/Game.gba")};
    b.entries = {Game("gba:2", "gba", "y/Game.gba")};
    Write(a.Save("gba:1"), "from a");
    REQUIRE(a.Sync().ok);
    Write(b.Save("gba:2"), "playing", 1);
    b.engine->Pump("gba:2");
    REQUIRE(b.Sync().ok);
    CHECK(Read(b.Save("gba:2")) == "playing");
    CHECK(b.engine->State().waiting == 1);
    CHECK(b.engine->GameState("gba:2").waiting);
    b.engine->Pump("");
    REQUIRE(b.Sync().ok);
    CHECK(Read(b.Save("gba:2")) == "from a"); // the remote one is newer
}

TEST_CASE("PC consoles never sync") {
    Cloud cloud;
    Device a(cloud, "A");
    a.entries = {Game("steam:1", "steam", "C:/Steam/game.exe")};
    Write(a.scratch.dir / "saves" / "steam" / (SafeFileName("steam:1") + ".srm"), "pc");
    cloud.files["saves/steam/game.srm"] = {"remote", 1, 1};
    REQUIRE(a.Sync().ok);
    CHECK(cloud.puts.empty());
    CHECK_FALSE(fs::exists(a.scratch.dir / "saves" / "steam" / "game.srm"));
}

TEST_CASE("settings sync without what belongs to the device") {
    Cloud cloud;
    Device a(cloud, "A", {"config"}), b(cloud, "B", {"config"});
    a.settings->SetString("ui.theme", "Dark");
    a.settings->SetNumber("ui.scale", 1.5);
    a.settings->SetString("console.steam.path", "C:/Steam");
    a.settings->SetList("library.collection_games.best", {"gba:1", "steam:2"});
    REQUIRE(a.Sync().ok);
    const std::string uploaded = cloud.files.at("config/settings.json").data;
    CHECK(uploaded.find("Dark") != std::string::npos);
    CHECK(uploaded.find("ui.scale") == std::string::npos);
    CHECK(uploaded.find("sync.") == std::string::npos);
    CHECK(uploaded.find("steam") == std::string::npos);
    b.settings->SetNumber("ui.scale", 2.0);
    REQUIRE(b.Sync().ok);
    CHECK(b.settings->GetString("ui.theme") == "Dark");
    CHECK(b.settings->GetNumber("ui.scale") == 2.0);
}

TEST_CASE("games are listed by the plan, never downloaded by it, uploaded only when asked") {
    Cloud cloud;
    Device a(cloud, "A", {"games"});
    const fs::path rom = a.scratch.dir / "roms" / "Game.gba";
    Write(rom, "rom bytes");
    a.entries = {Game("gba:1", "gba", rom)};
    cloud.files["games/gba/Other.gba"] = {"other rom", 1, 1};
    REQUIRE(a.Sync().ok);
    CHECK(cloud.puts.empty());
    CHECK_FALSE(fs::exists(a.engine->GamesFolder() / "gba" / "Other.gba"));
    a.engine->SetUploadGames(true);
    REQUIRE(a.Sync().ok);
    CHECK(cloud.files.at("games/gba/Game.gba").data == "rom bytes");
    REQUIRE(a.engine->DownloadGame("test.cloud", {"games/gba/Other.gba"}, nullptr).ok);
    CHECK(Read(a.engine->GamesFolder() / "gba" / "Other.gba") == "other rom");
    CHECK(Read(rom) == "rom bytes"); // the user's own folder is only read
}

TEST_CASE("the folder store keeps plain files and refuses a stale write") {
    Scratch scratch;
    const fs::path root = scratch.dir / "Sync" / "EmuDEN";
    fs::create_directories(root);
    auto store = CreateFolderStore("emuden.sync.syncthing", root);
    Write(scratch.dir / "local.srm", "one");
    const Result<std::string> put = store->Put("saves/gba/Game.srm", scratch.dir / "local.srm", "", 1'700'000'000);
    REQUIRE(put);
    CHECK(Read(root / "saves" / "gba" / "Game.srm") == "one");
    Write(root / "saves" / "gba" / ".syncthing.Game.srm.tmp", "partial");
    auto listed = store->List("saves/");
    REQUIRE(listed);
    REQUIRE(listed->size() == 1);
    CHECK(listed->front().path == "saves/gba/Game.srm");
    CHECK(listed->front().modified == 1'700'000'000);
    CHECK(listed->front().rev == put.value());

    REQUIRE(store->Get("saves/gba/Game.srm", scratch.dir / "back.srm", {}).ok);
    CHECK(Read(scratch.dir / "back.srm") == "one");

    Write(root / "saves" / "gba" / "Game.srm", "changed by another device", 1'700'000'500);
    CHECK(store->Put("saves/gba/Game.srm", scratch.dir / "local.srm", put.value(), 1).error() == "conflict");
    REQUIRE(store->Delete("saves/gba/Game.srm").ok);
    CHECK(store->List("")->empty());
}

TEST_CASE("the journal and the plan agree on what changed") {
    Scratch scratch;
    Journal journal(scratch.dir / "journal.json");
    journal.entries["saves/gba/A.srm"] = {"h1", "r1", 1, 5};
    journal.entries["saves/gba/B.srm"] = {"h1", "r1", 1, 5};
    journal.entries["saves/gba/Gone.srm"] = {"h1", "r1", 1, 5};
    journal.deleted.insert("saves/gba/C.srm");
    journal.entries["saves/gba/C.srm"] = {"h1", "r1", 1, 5};
    REQUIRE(journal.Save());
    const Journal loaded(scratch.dir / "journal.json");
    CHECK(loaded.entries.size() == 4);
    CHECK(loaded.deleted.contains("saves/gba/C.srm"));

    Catalog local;
    local["saves/gba/A.srm"] = {"saves/gba/A.srm", "a", "", 1, 1, "h2", ""};  // changed here
    local["saves/gba/B.srm"] = {"saves/gba/B.srm", "b", "", 1, 1, "h2", ""};  // changed on both
    std::map<std::string, RemoteFile> remote;
    remote["saves/gba/A.srm"] = {"saves/gba/A.srm", 1, 1, "r1"};
    remote["saves/gba/B.srm"] = {"saves/gba/B.srm", 1, 1, "r2"};
    remote["saves/gba/C.srm"] = {"saves/gba/C.srm", 1, 1, "r1"};
    remote["saves/gba/New.srm"] = {"saves/gba/New.srm", 1, 1, "r9"};
    PlanRequest request{&local, &remote, &loaded, {"saves"}, false};
    std::map<std::string, Op> ops;
    for (const Action& action : MakePlan(request)) {
        ops[action.path] = action.op;
    }
    CHECK(ops.at("saves/gba/A.srm") == Op::Upload);
    CHECK(ops.at("saves/gba/B.srm") == Op::Conflict);
    CHECK(ops.at("saves/gba/C.srm") == Op::DeleteRemote);
    CHECK(ops.at("saves/gba/New.srm") == Op::Download);
    CHECK(ops.at("saves/gba/Gone.srm") == Op::Forget);
}
