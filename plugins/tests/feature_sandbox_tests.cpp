// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The script sandbox: what a feature plugin's Lua cannot reach, and that a runaway or greedy
// script is stopped.

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "feature/runner.h"
#include "feature/ui_model.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::feature;
using namespace den::plugins::testing;

namespace {

using ScriptRunner = den::plugins::feature::Runner; // not the console Runner enum

const char* const PLAIN = R"({"id":"test.sample","name":"Sample","version":"1.0.0",
  "kind":"feature","api":1,"needs":["kind.feature","feature.lua.1"],
  "feature":{"entry":"main.lua","permissions":[]}})";

const char* const NETWORKED = R"({"id":"test.sample","name":"Sample","version":"1.0.0",
  "kind":"feature","api":1,"needs":["kind.feature","feature.lua.1"],
  "feature":{"entry":"main.lua","permissions":["http","download"],
    "http":{"hosts":["api.example.org"]},"limits":{"memory_mb":8}}})";

struct Stage {
    ScratchDir scratch;
    FakeWeb web;
    std::shared_ptr<FakeServices> services = std::make_shared<FakeServices>();

    std::unique_ptr<ScriptRunner> Run(const std::string& script, const char* manifest = PLAIN,
                                long long instructions = 5'000'000, std::string language = "en") {
        scratch.Write("package/main.lua", script);
        RunnerConfig config;
        config.manifest = ParseFeatureManifest(manifest).take();
        config.package_dir = scratch.dir / "package";
        config.data_dir = scratch.dir / "data";
        config.downloads_dir = scratch.dir / "Downloads";
        config.app_version = "1.2.3";
        config.platform = "test";
        config.http = web.Getter();
        config.services = services;
        config.now = [] { return std::int64_t{1794528000}; };
        config.instruction_limit = instructions;
        config.language = [language] { return language; };
        auto runner = std::make_unique<ScriptRunner>(std::move(config));
        runner->Settle();
        return runner;
    }
};

bool Logged(const ScriptRunner& runner, const std::string& text) {
    const std::vector<std::string> lines = runner.Log();
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("a script has no os, io, package, debug, require or bytecode") {
    Stage stage;
    // The header of a Lua 5.4 binary chunk: load must refuse it whatever mode is asked for.
    const auto runner = stage.Run(R"(
        local function is_nil(name) den.log(name .. "=" .. tostring(_G[name] == nil)) end
        for _, name in ipairs{"os", "io", "package", "debug", "require", "dofile", "loadfile"} do
          is_nil(name)
        end
        den.log("dump=" .. tostring(string.dump == nil))
        local chunk = "\27Lua\84\0\25\147\13\10\26\10\4\8\8\120\86\0\0\0\0\0\0\0\0\0\0\0\40\119\64\1"
        den.log("binary=" .. tostring(load(chunk) == nil))
        den.log("binary-b=" .. tostring(load(chunk, "x", "b") == nil))
        den.log("reader=" .. tostring(load(function() return chunk end) == nil))
        den.log("text=" .. tostring(load("return 7")()))
        den.log("gc=" .. tostring(pcall(collectgarbage, "stop")))
        return {}
    )");
    CHECK(runner->Error().empty());
    for (const char* name : {"os", "io", "package", "debug", "require", "dofile", "loadfile"}) {
        CHECK(Logged(*runner, std::string(name) + "=true"));
    }
    CHECK(Logged(*runner, "dump=true"));
    CHECK(Logged(*runner, "binary=true"));
    CHECK(Logged(*runner, "binary-b=true"));
    CHECK(Logged(*runner, "reader=true"));
    CHECK(Logged(*runner, "text=7"));
    CHECK(Logged(*runner, "gc=false"));
}

TEST_CASE("a script that is not text does not load") {
    Stage stage;
    const auto runner = stage.Run(std::string("\x1bLua\x54\x00\x19\x93\r\n\x1a\n", 12));
    CHECK(runner->Stopped());
    CHECK_FALSE(runner->Error().empty());
}

TEST_CASE("a runaway script is stopped by the instruction limit") {
    Stage stage;
    const auto runner = stage.Run(R"(
        local plugin = { actions = {} }
        function plugin.open() while true do end end
        function plugin.actions.trap()
          -- Swallowing the error must not buy more time.
          while true do pcall(function() while true do end end) end
        end
        function plugin.actions.ping() den.log("pong") end
        return plugin
    )",
                                  PLAIN, 2'000'000);
    runner->Call({"open"}, {});
    runner->Settle();
    CHECK(runner->Error().find("ran too long") != std::string::npos);
    CHECK_FALSE(runner->Stopped());

    runner->Call({"actions", "trap"}, {});
    runner->Settle();
    CHECK(runner->Error().find("ran too long") != std::string::npos);

    // The next callback has a budget of its own.
    runner->Call({"actions", "ping"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "pong"));
    CHECK(runner->Error().empty());

    // Three failures in a row stop the plugin until it is opened again.
    for (int i = 0; i < 3; ++i) {
        runner->Call({"open"}, {});
    }
    runner->Settle();
    CHECK(runner->Stopped());
}

TEST_CASE("a script that takes too much memory is stopped") {
    Stage stage;
    const auto runner = stage.Run(R"(
        local plugin = { actions = {} }
        function plugin.open()
          local hoard = {}
          for i = 1, 100000000 do hoard[i] = string.rep("x", 4096) .. i end
        end
        function plugin.actions.rep() return string.rep("x", 1 << 30) end
        function plugin.actions.ping() den.log("pong") end
        return plugin
    )",
                                  NETWORKED, 2'000'000'000);
    runner->Call({"open"}, {});
    runner->Settle();
    CHECK(runner->Error().find("not enough memory") != std::string::npos);
    runner->Call({"actions", "rep"}, {});
    runner->Settle();
    CHECK(runner->Error().find("not enough memory") != std::string::npos);
    runner->Call({"actions", "ping"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "pong"));
}

TEST_CASE("http reaches declared hosts only") {
    Stage stage;
    stage.web.files["https://api.example.org/list"] = R"({"items":[1,2,3]})";
    const auto runner = stage.Run(R"(
        local plugin = {}
        local function try(name, url)
          local answer, why = den.http.get{ url = url }
          den.log(name .. "=" .. tostring(answer) .. " " .. tostring(why))
        end
        function plugin.open()
          try("undeclared", "https://evil.example.org/x")
          try("plain", "http://api.example.org/list")
          try("userinfo", "https://api.example.org@evil.example.org/x")
          try("port", "https://api.example.org:8443/list")
          try("suffix", "https://api.example.org.evil.example.org/list")
          local answer = den.http.get{ url = "https://api.example.org/list" }
          local items = den.json.decode(answer.body).items
          den.log("first " .. answer.status .. " " .. #items .. " " .. tostring(answer.cached))
          local again = den.http.get{ url = "https://api.example.org/list" }
          den.log("again " .. tostring(again and again.cached))
          den.log("upper " .. tostring(den.http.get{ url = "https://API.example.org/list",
                                                      cache_seconds = 0 } ~= nil))
        end
        return plugin
    )",
                                  NETWORKED);
    runner->Call({"open"}, {});
    runner->Settle();
    CHECK(runner->Error().empty());
    for (const char* name : {"undeclared", "plain", "userinfo", "port", "suffix"}) {
        CHECK(Logged(*runner, std::string(name) + "=nil"));
    }
    CHECK(Logged(*runner, "did not declare the host evil.example.org"));
    CHECK(Logged(*runner, "first 200 3 false"));
    CHECK(Logged(*runner, "again true"));
    CHECK(Logged(*runner, "upper true")); // a host name is compared without case
    // Nothing refused ever reached the network, and the cached answer was not asked for again.
    REQUIRE(stage.web.asked.size() == 2);
    CHECK(stage.web.asked[0] == "https://api.example.org/list");
    CHECK(stage.web.asked[1] == "https://API.example.org/list");
    REQUIRE_FALSE(stage.web.agents.empty());
    CHECK(stage.web.agents.front() == "EmuDEN/1.2.3 (plugin test.sample/1.0.0)");
}

TEST_CASE("a plugin without the http permission cannot connect") {
    Stage stage;
    stage.web.files["https://api.example.org/list"] = "x";
    const auto runner = stage.Run(R"(
        return { open = function()
          den.log("get=" .. tostring(den.http.get{ url = "https://api.example.org/list" }))
          den.log("download=" .. tostring(den.download{ url = "https://api.example.org/list",
                                                         name = "a.txt" }))
          den.log("open=" .. tostring(den.open_url("https://api.example.org/")))
          den.log("inbox=" .. tostring(den.files.inbox()))
          den.log("find=" .. tostring(den.library.find{ crc32 = 1 }))
        end }
    )");
    runner->Call({"open"}, {});
    runner->Settle();
    CHECK(runner->Error().empty());
    for (const char* name : {"get", "download", "open", "inbox", "find"}) {
        CHECK(Logged(*runner, std::string(name) + "=nil"));
    }
    CHECK(stage.web.Count() == 0);
}

TEST_CASE("downloads land in the plugin's folder and are checked") {
    Stage stage;
    stage.web.files["https://api.example.org/a.bps"] = "BPS1 patch bytes";
    const std::string good = HashOf("BPS1 patch bytes");
    const auto runner = stage.Run(R"(
        return { open = function()
          local path = den.download{ url = "https://api.example.org/a.bps", name = "a.bps",
                                     sha256 = ")" + good + R"(" }
          den.log("good=" .. tostring(path) .. " " .. tostring(den.files.exists("downloads/a.bps")))
          local bad, why = den.download{ url = "https://api.example.org/a.bps", name = "b.bps",
                                         sha256 = string.rep("0", 64) }
          den.log("bad=" .. tostring(bad) .. " " .. tostring(den.files.exists("downloads/b.bps")))
          den.log("escape=" .. tostring(den.download{ url = "https://api.example.org/a.bps",
                                                       name = "../a.bps" }))
        end }
    )",
                                  NETWORKED);
    runner->Call({"open"}, {});
    runner->Settle();
    CHECK(runner->Error().empty());
    CHECK(Logged(*runner, "good=downloads/a.bps true"));
    CHECK(Logged(*runner, "bad=nil false"));
    CHECK(Logged(*runner, "escape=nil"));
    CHECK(fs::exists(stage.scratch.dir / "data" / "downloads" / "a.bps"));
    CHECK_FALSE(fs::exists(stage.scratch.dir / "a.bps"));
}

TEST_CASE("files stay inside the plugin's folder, storage persists") {
    Stage stage;
    stage.scratch.Write("secret.txt", "secret");
    stage.scratch.Write("data/inbox/note.txt", "hello");
    const char* const script = R"(
        return { open = function()
          for _, path in ipairs{"../secret.txt", "inbox/../../secret.txt", "/etc/passwd",
                                "C:/Windows/win.ini", "inbox\\..\\..\\secret.txt"} do
            den.log("read " .. path .. "=" .. tostring(den.files.read(path)))
          end
          den.log("list=" .. tostring(den.files.list("..")))
          den.log("note=" .. tostring(den.files.read("inbox/note.txt")))
          den.log("script=" .. tostring(den.files.read("package/main.lua") ~= nil))
          den.log("unpackage=" .. tostring(den.files.remove("package/main.lua")))
          den.log("before=" .. tostring(den.storage.get("seen")))
          den.storage.set("seen", { count = 2, tags = {"a", "b"}, none = den.json.null })
          den.log("after=" .. den.json.encode(den.storage.get("seen").tags))
        end }
    )";
    {
        const auto runner = stage.Run(script);
        runner->Call({"open"}, {});
        runner->Settle();
        CHECK(runner->Error().empty());
        for (const char* path : {"../secret.txt", "inbox/../../secret.txt", "/etc/passwd",
                                 "C:/Windows/win.ini", "inbox\\..\\..\\secret.txt"}) {
            CHECK(Logged(*runner, std::string("read ") + path + "=nil"));
        }
        CHECK(Logged(*runner, "list=nil"));
        CHECK(Logged(*runner, "note=hello"));
        CHECK(Logged(*runner, "script=true"));
        CHECK(Logged(*runner, "unpackage=nil"));
        CHECK(Logged(*runner, "before=nil"));
        CHECK(Logged(*runner, R"(after=["a","b"])"));
    }
    const auto runner = stage.Run(script);
    runner->Call({"open"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "before=table"));
}

TEST_CASE("waits suspend the script and go on with the answer") {
    Stage stage;
    const auto runner = stage.Run(R"(
        local plugin = { actions = {} }
        function plugin.open()
          den.sleep(20)
          local yes = den.ui.confirm{ title = "Sure?", message = "Really", yes = "Do it" }
          den.log("confirmed=" .. tostring(yes))
        end
        function plugin.actions.other() den.log("other ran") end
        return plugin
    )");
    runner->Call({"open"}, {});
    runner->Call({"actions", "other"}, {});
    runner->Settle();
    // The second call ran while the first one waits for the screen.
    CHECK(Logged(*runner, "other ran"));
    CHECK_FALSE(Logged(*runner, "confirmed"));
    const std::vector<Ask> asks = runner->TakeAsks();
    REQUIRE(asks.size() == 1);
    CHECK(asks[0].kind == AskKind::Confirm);
    CHECK(asks[0].yes == "Do it");
    CHECK_FALSE(runner->Answer(asks[0].id + 1000, true, {}));
    CHECK(runner->Answer(asks[0].id, true, {}));
    runner->Settle();
    CHECK(Logged(*runner, "confirmed=true"));
}

TEST_CASE("the page model keeps to its limits") {
    std::string body;
    for (int i = 0; i < 500; ++i) {
        body += std::string(i ? "," : "") + R"({"type":"text","text":"t"})";
    }
    const PageModel many = PageFromJson(*ParseJson(R"({"title":"T","body":[)" + body + "]}"));
    CHECK(many.body.size() == MAX_ELEMENTS);

    const std::string wide(5000, 'a');
    PageModel page = PageFromJson(*ParseJson(
        R"({"title":")" + wide + R"(","search":{"placeholder":"Find","action":"search"},
            "chips":{"action":"chip","multi":true,"items":[{"id":"gba","label":"GBA","selected":true}]},
            "body":[{"type":"hologram","text":"later"},
                    {"type":"shelf","title":"S","items":[{"title":"A","action":"go","arg":{"n":1}}]},
                    {"type":"progress","id":"p","fraction":0.25},
                    {"type":"empty","title":"None","button":{"label":"Add","action":"add"}}]})"));
    CHECK(page.title.size() == MAX_STRING);
    CHECK(page.has_search);
    CHECK(page.search_action == "search");
    REQUIRE(page.chips.size() == 1);
    CHECK(page.chips[0].selected);
    REQUIRE(page.body.size() == 3); // the unknown type is left out
    CHECK(page.body[0].type == ElementType::Shelf);
    REQUIRE(page.body[0].items.size() == 1);
    CHECK(page.body[0].items[0].type == ElementType::Tile);
    CHECK(ParseJson(page.body[0].items[0].arg_json)->Find("n")->number == 1);
    CHECK(page.body[2].items.at(0).type == ElementType::Button);
    CHECK(PatchElement(page, "p", *ParseJson(R"({"fraction":0.5,"label":"Half"})")));
    CHECK(page.body[1].fraction == 0.5f);
    CHECK(page.body[1].label == "Half");
    CHECK_FALSE(PatchElement(page, "missing", *ParseJson("{}")));
}

TEST_CASE("den.tr reads the plugin's own translations for the app's language") {
    const char* const TRANSLATED = R"({"id":"test.sample","name":"Sample","version":"1.0.0",
      "kind":"feature","api":1,"needs":["kind.feature","feature.lua.1"],
      "translations":{"es":{"Sign in":"Iniciar sesión","{} of {} done":"{} de {} hechos"}},
      "feature":{"entry":"main.lua","permissions":[]}})";
    const char* const SCRIPT = R"(
        den.log("lang=" .. den.language())
        den.log("a=" .. den.tr("Sign in"))
        den.log("b=" .. den.tr("{} of {} done", 2, 5))
        den.log("c=" .. den.tr("Untranslated {}", true))
        return {}
    )";
    Stage spanish;
    const auto runner = spanish.Run(SCRIPT, TRANSLATED, 5'000'000, "es-ES");
    CHECK(runner->Error().empty());
    CHECK(Logged(*runner, "lang=es-ES"));
    CHECK(Logged(*runner, "a=Iniciar sesión"));
    CHECK(Logged(*runner, "b=2 de 5 hechos"));
    CHECK(Logged(*runner, "c=Untranslated true"));

    Stage english;
    const auto plain = english.Run(SCRIPT, TRANSLATED);
    CHECK(Logged(*plain, "lang=en"));
    CHECK(Logged(*plain, "a=Sign in"));
    CHECK(Logged(*plain, "b=2 of 5 done"));
}
