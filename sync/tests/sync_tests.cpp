// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sync engine with a provider in memory and a data folder of its own: portable names,
// the plan, conflicts, the journal, running games, PC consoles and the folder store.

#include <atomic>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <mutex>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/jobs.h"
#include "den/base/json.h"
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
    std::function<void(const std::string& path)> on_put; // before the file is stored
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
                            std::int64_t modified, const Progress&) override {
        if (cloud_.on_put) {
            cloud_.on_put(path);
        }
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

    std::map<std::string, library::PlayTotals> own_play, other_play;

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
        config.own_play = [this] { return own_play; };
        config.set_other_play = [this](const std::map<std::string, library::PlayTotals>& others) {
            other_play = others;
        };
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
    const Result<std::string> put = store->Put("saves/gba/Game.srm", scratch.dir / "local.srm", "", 1'700'000'000, {});
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
    CHECK(store->Put("saves/gba/Game.srm", scratch.dir / "local.srm", put.value(), 1, {}).error() == "conflict");
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

TEST_CASE("the journal is written after every file, so a run cut short loses nothing") {
    Cloud cloud;
    Device a(cloud, "A");
    a.entries = {Game("gba:1", "gba", a.scratch.dir / "roms" / "One.gba"),
                 Game("gba:2", "gba", a.scratch.dir / "roms" / "Two.gba")};
    Write(a.Save("gba:1"), "one", 1'800'000'100);
    Write(a.Save("gba:2"), "two", 1'800'000'100);
    const fs::path journal = a.scratch.dir / "sync" / "test.cloud" / "journal.json";
    std::atomic<bool> cancel{false};
    std::string seen;
    cloud.on_put = [&](const std::string& path) {
        if (path == "saves/gba/Two.srm") {
            seen = Read(journal); // the first file is on disk before the second goes
            cancel = true;
        }
    };
    REQUIRE(a.engine->RunOnce({}, &cancel).ok);
    CHECK(seen.find("saves/gba/One.srm") != std::string::npos);
    // The next run starts where that one stopped: no conflict, nothing sent twice.
    cloud.on_put = nullptr;
    cloud.puts.clear();
    REQUIRE(a.Sync().ok);
    CHECK(cloud.puts.empty());
    CHECK(a.engine->Conflicts().empty());
    CHECK(cloud.files.at("saves/gba/Two.srm").data == "two");
}

TEST_CASE("saves go before games, and the run counts as a sync before the games go up") {
    Cloud cloud;
    Device a(cloud, "A", {"saves", "games"});
    const fs::path rom = a.scratch.dir / "roms" / "Big.gba";
    Write(rom, "a big game");
    a.entries = {Game("gba:1", "gba", rom)};
    Write(a.Save("gba:1"), "save", 1'800'000'100);
    a.engine->SetUploadGames(true);
    bool save_first = false;
    std::int64_t last_sync = 0;
    bool games_pass = false;
    cloud.on_put = [&](const std::string& path) {
        if (path == "games/gba/Big.gba") {
            std::lock_guard lock(cloud.mutex);
            save_first = cloud.files.contains("saves/gba/Big.srm");
            last_sync = a.engine->State().last_sync;
            games_pass = a.engine->State().games_pass;
        }
    };
    REQUIRE(a.Sync().ok);
    CHECK(save_first);
    CHECK(last_sync == a.clock);
    CHECK(games_pass);
    CHECK(a.engine->GameState("gba:1").synced > 0);
    CHECK(cloud.files.contains("games/gba/Big.gba"));
}

TEST_CASE("games go up once: never again, never back, and never deleted") {
    Cloud cloud;
    Device a(cloud, "A", {"games"});
    const fs::path one = a.scratch.dir / "roms" / "One.gba";
    const fs::path two = a.scratch.dir / "roms" / "Two.gba";
    Write(one, "game one");
    Write(two, "game two");
    a.entries = {Game("gba:1", "gba", one), Game("gba:2", "gba", two)};
    a.engine->SetUploadGames(true);
    // A game starts while the first one goes up: the second waits for a later run.
    cloud.on_put = [&](const std::string&) { a.engine->Pump("gba:1"); };
    REQUIRE(a.Sync().ok);
    CHECK(cloud.puts == std::vector<std::string>{"games/gba/One.gba"});
    cloud.on_put = nullptr;
    a.engine->Pump("");
    REQUIRE(a.Sync().ok);
    CHECK(cloud.puts == std::vector<std::string>{"games/gba/One.gba", "games/gba/Two.gba"});

    // Already up: not again, with the journal gone and the file touched.
    fs::remove(a.scratch.dir / "sync" / "test.cloud" / "journal.json");
    Write(one, "game one", 1'900'000'000);
    REQUIRE(a.Sync().ok);
    CHECK(cloud.puts.size() == 2);

    // A game from the library is never uploaded back.
    cloud.files["games/gba/Three.gba"] = {"game three", 1, cloud.next_rev++};
    REQUIRE(a.engine->DownloadGame("test.cloud", {"games/gba/Three.gba"}, nullptr).ok);
    const fs::path three = a.engine->GamesFolder() / "gba" / "Three.gba";
    a.entries.push_back(Game("gba:3", "gba", three));
    cloud.files.erase("games/gba/Three.gba");
    REQUIRE(a.Sync().ok);
    CHECK(cloud.puts.size() == 2);

    // Deleted here: the remote copy stays; and nothing local goes because the remote did.
    fs::remove(two);
    a.entries = {Game("gba:1", "gba", one), Game("gba:3", "gba", three)};
    REQUIRE(a.Sync().ok);
    CHECK(cloud.files.contains("games/gba/Two.gba"));
    CHECK(fs::exists(three));
}

TEST_CASE("play time adds up across devices without counting anything twice") {
    Cloud cloud;
    Device a(cloud, "Legion Go S", {"saves", "config"}), b(cloud, "Retroid Pocket", {"saves", "config"});
    a.entries = {Game("gba:a", "gba", a.scratch.dir / "roms" / "Lazarus.gba")};
    b.entries = {Game("gba:b", "gba", b.scratch.dir / "roms" / "Lazarus.gba")};
    a.own_play["gba:a"] = {100, 1'800'000'000, 2};
    b.own_play["gba:b"] = {50, 1'800'000'500, 1};
    REQUIRE(a.Sync().ok);
    REQUIRE(b.Sync().ok);
    REQUIRE(a.Sync().ok);
    REQUIRE(b.other_play.contains("gba:b"));
    CHECK(b.other_play.at("gba:b").seconds == 100);
    CHECK(b.other_play.at("gba:b").count == 2);
    REQUIRE(a.other_play.contains("gba:a"));
    CHECK(a.other_play.at("gba:a").seconds == 50);
    CHECK(a.other_play.at("gba:a").last_played == 1'800'000'500);

    // More play on one device replaces its part on the other, however often they sync.
    a.own_play["gba:a"] = {160, 1'800'001'000, 3};
    REQUIRE(a.Sync().ok);
    REQUIRE(b.Sync().ok);
    REQUIRE(b.Sync().ok);
    CHECK(b.other_play.at("gba:b").seconds == 160);
    cloud.puts.clear();
    REQUIRE(a.Sync().ok);
    REQUIRE(b.Sync().ok);
    CHECK(cloud.puts.empty()); // a device's file goes up only when its own play changed

    // A conflict copy names the device it came from.
    Write(a.Save("gba:a"), "legion save", 1'800'002'000);
    REQUIRE(a.Sync().ok);
    Write(b.Save("gba:b"), "retroid save", 1'800'003'000);
    fs::remove(b.scratch.dir / "sync" / "test.cloud" / "journal.json");
    REQUIRE(b.Sync().ok);
    REQUIRE(b.engine->Conflicts().size() == 1);
    CHECK(b.engine->Conflicts()[0].from == "Legion Go S");
    CHECK(b.other_play.at("gba:b").seconds == 160);
}

TEST_CASE("settings changed on both devices merge key by key, without a conflict copy") {
    CHECK(MergeSettings(R"({"a":1,"b":1,"c":1})", R"({"a":2,"b":1,"c":1})", R"({"a":1,"b":3})") ==
          WriteJson(JsonObject({{"a", JsonNumber(2)}, {"b", JsonNumber(3)}})));
    CHECK(MergeSettings("", R"({"a":2})", R"({"a":3,"n":1})") ==
          WriteJson(JsonObject({{"a", JsonNumber(3)}, {"n", JsonNumber(1)}})));
    CHECK(MergeSettings("{}", "[]", "{}").empty());

    Cloud cloud;
    Device a(cloud, "A", {"config"}), b(cloud, "B", {"config"});
    a.settings->SetNumber("player.shader.crt.bright", 1.450000047683716);
    a.settings->SetString("ui.theme", "dark");
    REQUIRE(a.Sync().ok);
    REQUIRE(b.Sync().ok);
    CHECK(b.settings->GetString("ui.theme") == "dark");
    // The last bits of a fraction moving does not make the file go up again.
    b.settings->SetNumber("player.shader.crt.bright", 1.4500000476837154);
    cloud.puts.clear();
    REQUIRE(b.Sync().ok);
    CHECK(cloud.puts.empty());
    // Each changes another key: both keep both, and nobody gets a conflict.
    a.settings->SetString("ui.theme", "light");
    b.settings->SetBool("player.rewind", true);
    REQUIRE(a.Sync().ok);
    REQUIRE(b.Sync().ok);
    REQUIRE(a.Sync().ok);
    CHECK(b.settings->GetString("ui.theme") == "light");
    CHECK(b.settings->GetBool("player.rewind"));
    CHECK(a.settings->GetBool("player.rewind"));
    CHECK(a.settings->GetString("ui.theme") == "light");
    CHECK(a.engine->Conflicts().empty());
    CHECK(b.engine->Conflicts().empty());
}
