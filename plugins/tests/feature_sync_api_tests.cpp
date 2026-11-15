// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What sync providers use: the sync manifest fields, den.http.request, den.secrets, den.sync
// and the host calling plugin.provider for its results.

#include <map>
#include <mutex>

#include <catch2/catch_test_macros.hpp>

#include "den/platform/pal.h"
#include "feature/runner.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::feature;
using namespace den::plugins::testing;

namespace {

using ScriptRunner = den::plugins::feature::Runner;

const char* const PROVIDER = R"({"id":"test.sync","name":"Sync","version":"1.0.0",
  "kind":"feature","api":1,"needs":["kind.feature","feature.lua.1"],
  "feature":{"entry":"main.lua","permissions":["sync","http","http.local","http.user_host"],
    "http":{"hosts":["api.example.org"]},"sync":{"categories":["saves","states"]}}})";

struct MemorySecrets final : pal::Secrets {
    std::map<std::string, std::string> values;
    std::optional<std::string> Get(const std::string& key) override {
        const auto it = values.find(key);
        return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    Status Set(const std::string& key, const std::string& value) override {
        values[key] = value;
        return {};
    }
    void Remove(const std::string& key) override {
        values.erase(key);
    }
};

struct FakeLink final : SyncLink {
    fs::path folder, home;
    std::vector<std::vector<std::string>> downloads;
    JsonValue StatusOf(const std::string&) override {
        return JsonObject({{"active", JsonBool(true)}, {"uploaded", JsonNumber(3)}});
    }
    Status SyncNow(const std::string&) override {
        return {};
    }
    std::vector<std::string> EnabledCategories(const std::string&) override {
        return {"saves"};
    }
    fs::path Folder(const std::string&) override {
        return folder;
    }
    Status CheckFolder(const fs::path&) override {
        return {};
    }
    Status UseFolder(const std::string&, const fs::path& chosen) override {
        folder = chosen;
        return {};
    }
    fs::path Home() override {
        return home;
    }
    Status DownloadGame(const std::string&, const std::vector<std::string>& paths) override {
        downloads.push_back(paths);
        return {};
    }
};

struct Seen {
    std::string url, method, body, body_file;
    bool follow = true, any_certificate = false;
    std::vector<std::pair<std::string, std::string>> headers;
};

struct Stage {
    ScratchDir scratch;
    MemorySecrets secrets;
    std::shared_ptr<FakeLink> link = std::make_shared<FakeLink>();
    std::vector<Seen> seen;
    std::mutex mutex;
    std::map<std::string, fs::path> lent;

    std::unique_ptr<ScriptRunner> Run(const std::string& script, const char* manifest = PROVIDER) {
        scratch.Write("package/main.lua", script);
        RunnerConfig config;
        config.manifest = ParseFeatureManifest(manifest).take();
        config.package_dir = scratch.dir / "package";
        config.data_dir = scratch.dir / "data";
        config.app_version = "1.2.3";
        config.platform = "test";
        config.http = [this](const pal::HttpRequest& request) {
            std::lock_guard lock(mutex);
            Seen s{request.url, request.method, request.body, PathToUtf8(request.body_file),
                   request.follow_redirects, request.loopback_any_certificate, request.headers};
            if (!request.body_file.empty()) {
                s.body = ReadTextFile(request.body_file).value_or("").substr(
                    request.body_offset, request.body_length == 0 ? std::string::npos : request.body_length);
            }
            seen.push_back(s);
            pal::HttpResponse response;
            response.status = request.url.ends_with("/missing") ? 404 : 200;
            response.body = request.method + " " + s.body;
            response.headers = {{"etag", "\"v2\""}, {"x-many", "a"}, {"x-many", "b"}};
            return response;
        };
        config.now = [] { return std::int64_t{1794528000}; };
        config.instruction_limit = 5'000'000;
        config.secrets = &secrets;
        config.sync = link;
        config.lent = [this](const std::string& token) -> std::optional<fs::path> {
            const auto it = lent.find(token);
            return it == lent.end() ? std::nullopt : std::optional<fs::path>(it->second);
        };
        auto runner = std::make_unique<ScriptRunner>(std::move(config));
        runner->Settle();
        return runner;
    }
};

bool Logged(const ScriptRunner& runner, const std::string& text) {
    for (const std::string& line : runner.Log()) {
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("a sync provider's manifest names its categories once each") {
    auto parsed = ParseFeatureManifest(PROVIDER);
    REQUIRE(parsed);
    const FeatureManifest* manifest = &parsed.value();
    CHECK(manifest->sync_categories == std::vector<std::string>{"saves", "states"});
    const auto lines = PermissionLines(manifest->access, manifest->sync_categories);
    bool user = false, local = false;
    for (const PermissionLine& line : lines) {
        user = user || line.text == "Connect to the server you enter";
        local = local || line.text == "Connect to apps on this device";
    }
    CHECK(user);
    CHECK(local);

    const std::string base = R"({"id":"test.sync","name":"Sync","version":"1.0.0","kind":"feature",
      "api":1,"needs":["kind.feature","feature.lua.1"],"feature":{"entry":"main.lua",)";
    CHECK_FALSE(ParseFeatureManifest(base + R"("permissions":["sync"]}})"));
    CHECK_FALSE(ParseFeatureManifest(base + R"("permissions":["sync"],"sync":{"categories":[]}}})"));
    CHECK_FALSE(ParseFeatureManifest(base + R"("permissions":["sync"],"sync":{"categories":["roms"]}}})"));
    CHECK_FALSE(
        ParseFeatureManifest(base + R"("permissions":["sync"],"sync":{"categories":["saves","saves"]}}})"));
    CHECK_FALSE(ParseFeatureManifest(base + R"("permissions":[],"sync":{"categories":["saves"]}}})"));
    CHECK(ParseFeatureManifest(base + R"("permissions":["sync","http.user_host"],"sync":{"categories":["bios"]}}})"));
}

TEST_CASE("den.http.request sends methods, bodies and multipart and returns any answer") {
    Stage stage;
    stage.scratch.Write("data/note.txt", "file body");
    const auto runner = stage.Run(R"(
        local plugin = {}
        function plugin.open()
          local a = den.http.request{url = "https://api.example.org/x", method = "patch",
                                     headers = {Authorization = "Bearer t"}, body = "{}"}
          den.log("a=" .. a.status .. " " .. a.body .. " " .. a.headers.etag .. " " .. a.headers["x-many"])
          local b = den.http.request{url = "https://api.example.org/missing"}
          den.log("b=" .. b.status)
          local c = den.http.request{url = "https://api.example.org/up", method = "PUT",
                                     body_file = "note.txt", body_offset = 5, body_length = 4}
          den.log("c=" .. c.body)
          local d = den.http.request{url = "https://api.example.org/form", method = "POST",
            multipart = { {name = "meta", value = "1"},
                          {name = "file", file = "note.txt", filename = "n.txt", content_type = "text/plain"} }}
          den.log("d=" .. tostring(d.status))
          local e = den.http.request{url = "http://127.0.0.1:8384/rest/system/status"}
          den.log("e=" .. tostring(e and e.status))
          local f, why = den.http.request{url = "http://127.0.0.1/rest"}
          den.log("f=" .. tostring(f) .. " " .. tostring(why))
          local g, why2 = den.http.request{url = "https://other.example.org/"}
          den.log("g=" .. tostring(g))
          local h, why3 = den.http.request{url = "http://192.168.1.5:8080/api/heartbeat"}
          den.log("h=" .. tostring(h))
        end
        return plugin)");
    runner->Call({"open"}, {JsonObject()});
    runner->Settle();
    INFO(runner->Error());
    CHECK(Logged(*runner, "a=200 PATCH {} \"v2\" a, b"));
    CHECK(Logged(*runner, "b=404"));
    CHECK(Logged(*runner, "c=PUT body"));
    CHECK(Logged(*runner, "d=200"));
    CHECK(Logged(*runner, "e=200"));
    CHECK(Logged(*runner, "f=nil"));
    CHECK(Logged(*runner, "g=nil"));
    CHECK(Logged(*runner, "h=nil")); // no server confirmed yet
    std::lock_guard lock(stage.mutex);
    REQUIRE(stage.seen.size() == 5);
    bool authorized = false;
    for (const auto& [name, value] : stage.seen[0].headers) {
        authorized = authorized || (name == "Authorization" && value == "Bearer t");
    }
    CHECK(authorized);
    CHECK(stage.seen[0].follow);
    CHECK(stage.seen[2].body == "body");
    CHECK(stage.seen[3].body.find("Content-Disposition: form-data; name=\"file\"; filename=\"n.txt\"") !=
          std::string::npos);
    CHECK(stage.seen[3].body.find("file body") != std::string::npos);
    CHECK_FALSE(stage.seen[4].follow);
    CHECK(stage.seen[4].any_certificate);
}

TEST_CASE("the user's server is reached once the user confirmed it") {
    Stage stage;
    const auto runner = stage.Run(R"(
        local plugin = { actions = {} }
        function plugin.actions.connect()
          den.log("set=" .. tostring(den.sync.set_server("http://192.168.1.5:8080/romm")))
          local r = den.http.request{url = "http://192.168.1.5:8080/api/heartbeat"}
          den.log("r=" .. tostring(r and r.status))
        end
        return plugin)");
    runner->Call({"actions", "connect"}, {JsonValue(), JsonString("")});
    runner->Settle();
    const std::vector<Ask> asks = runner->TakeAsks();
    REQUIRE(asks.size() == 1);
    CHECK(asks[0].kind == AskKind::Confirm);
    CHECK(asks[0].message.find("plain http") != std::string::npos);
    runner->Answer(asks[0].id, true, "");
    runner->Settle();
    CHECK(Logged(*runner, "set=true"));
    CHECK(Logged(*runner, "r=200"));
    CHECK(runner->UserServer() == "http://192.168.1.5:8080");
}

TEST_CASE("secrets go to the credential store under the plugin's name") {
    Stage stage;
    const auto runner = stage.Run(R"(
        local plugin = {}
        function plugin.open()
          den.secrets.set("refresh", {token = "abc"})
          den.log("get=" .. den.secrets.get("refresh").token)
          den.secrets.remove("refresh")
          den.log("gone=" .. tostring(den.secrets.get("refresh")))
          den.secrets.set("key", "k1")
          den.log("bad=" .. tostring(den.secrets.set("no spaces", 1)))
        end
        return plugin)");
    runner->Call({"open"}, {JsonObject()});
    runner->Settle();
    CHECK(Logged(*runner, "get=abc"));
    CHECK(Logged(*runner, "gone=nil"));
    CHECK(Logged(*runner, "bad=nil"));
    CHECK(stage.secrets.values.count("plugin/test.sync/key") == 1);
    CHECK(ReadTextFile(stage.scratch.dir / "data" / "storage.json").value_or("").find("k1") == std::string::npos);
}

TEST_CASE("the host calls plugin.provider and gets its results") {
    Stage stage;
    stage.scratch.Write("host/save.srm", std::string("\0\1\2", 3));
    stage.lent["sync:1-x"] = stage.scratch.dir / "host" / "save.srm";
    const auto runner = stage.Run(R"(
        local plugin = { provider = {} }
        function plugin.provider.list(prefix)
          return { {path = "saves/gba/Game.srm", size = 3, modified = 10, rev = "r1"} }
        end
        function plugin.provider.put(path, local_path, rev, modified)
          local data = den.files.read(local_path)
          den.log("put " .. path .. " " .. #data .. " " .. tostring(rev) .. " " .. modified ..
                  " size " .. den.files.size(local_path))
          return "r2"
        end
        function plugin.provider.get(path, local_path, progress)
          progress(5, 10)
          return nil, "not there"
        end
        function plugin.provider.delete(path) error("boom") end
        function plugin.open()
          den.log("status=" .. den.sync.status().uploaded)
          den.log("cats=" .. table.concat(den.sync.enabled_categories(), ","))
          den.log("dl=" .. tostring(den.sync.download_game({"games/gba/Game.gba"})))
        end
        return plugin)");
    std::vector<Result<std::vector<JsonValue>>> results;
    std::mutex mutex;
    const auto keep = [&](Result<std::vector<JsonValue>> result) {
        std::lock_guard lock(mutex);
        results.push_back(std::move(result));
    };
    std::vector<double> progress;
    runner->Invoke({"provider", "list"}, {JsonString("saves/")}, keep);
    runner->Invoke({"provider", "put"},
                   {JsonString("saves/gba/Game.srm"), JsonString("sync:1-x"), JsonString("r1"), JsonNumber(42)},
                   keep);
    runner->Invoke({"provider", "get"}, {JsonString("saves/gba/Game.srm"), JsonString("sync:1-x")}, keep,
                   [&](double done, double total) { progress = {done, total}; });
    runner->Invoke({"provider", "delete"}, {JsonString("saves/gba/Game.srm")}, keep);
    runner->Invoke({"provider", "status"}, {}, keep);
    runner->Settle();
    REQUIRE(results.size() == 5);
    REQUIRE(results[0]);
    CHECK(results[0].value()[0].items[0].String("rev") == "r1");
    REQUIRE(results[1]);
    CHECK(results[1].value()[0].string == "r2");
    CHECK(Logged(*runner, "put saves/gba/Game.srm 3 r1 42 size 3"));
    CHECK_FALSE(results[2]);
    CHECK(results[2].error() == "not there");
    CHECK(progress == std::vector<double>{5, 10});
    CHECK_FALSE(results[3]);
    CHECK_FALSE(results[4]); // no such function
    CHECK(runner->Invoking() == 0);

    runner->Call({"open"}, {JsonObject()});
    runner->Settle();
    CHECK(Logged(*runner, "status=3"));
    CHECK(Logged(*runner, "cats=saves"));
    CHECK(Logged(*runner, "dl=true"));
    CHECK(stage.link->downloads.size() == 1);
}

TEST_CASE("use_folder takes ~/ under the home and asks the user") {
    Stage stage;
    stage.link->home = stage.scratch.dir / "home";
    const auto runner = stage.Run(R"(
        local plugin = { actions = {} }
        function plugin.actions.pick()
          den.log("folder=" .. tostring(den.sync.use_folder("~/Sync/EmuDEN")))
          den.log("now=" .. tostring(den.sync.folder()))
          den.log("bad=" .. tostring(den.sync.use_folder("~/../x")))
        end
        return plugin)");
    runner->Call({"actions", "pick"}, {JsonValue(), JsonString("")});
    runner->Settle();
    std::vector<Ask> asks = runner->TakeAsks();
    REQUIRE(asks.size() == 1);
    CHECK(asks[0].kind == AskKind::Confirm);
    runner->Answer(asks[0].id, true, "");
    runner->Settle();
    const fs::path expected = stage.scratch.dir / "home" / "Sync" / "EmuDEN";
    CHECK(stage.link->folder == expected.lexically_normal());
    CHECK(fs::is_directory(expected));
    CHECK(Logged(*runner, "bad=nil"));
}
