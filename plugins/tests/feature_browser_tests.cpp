// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The browser plugin's host API: den.web.open{any = true} (browse.any) and den.downloads
// (downloads.setup), official plugins only.

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "feature/runner.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::feature;
using namespace den::plugins::testing;

namespace {

using ScriptRunner = den::plugins::feature::Runner;

const std::string MANIFEST = R"({"id":"emuden.browser","name":"Browser","version":"1.0.0","kind":"feature",
  "api":1,"needs":["kind.feature","feature.lua.1","feature.web.1","feature.browser.1"],
  "feature":{"entry":"main.lua","permissions":["browse.any","downloads.setup"]}})";

const char* const SCRIPT = R"(
    local plugin = { actions = {} }
    function plugin.actions.browse()
      local file, why = den.web.open{ any = true, title = "Browser" }
      if not file then den.log("ended: " .. tostring(why)) return end
      den.log("got " .. file.download .. " | " .. file.page_title .. " | " .. file.source_host)
      local done, err = den.downloads.setup{ file = file.download, page_title = file.page_title,
                                             source_host = file.source_host }
      den.log("setup: " .. tostring(done and done.outcome) .. " " .. tostring(done and done.message or err))
      local history = den.downloads.history()
      den.log("history: " .. #history .. " " .. history[1].id)
      den.log("undo: " .. tostring(den.downloads.undo(history[1].id)))
      local files = den.downloads.files()
      den.log("files: " .. #files .. " " .. files[1].name)
    end
    function plugin.actions.sneaky()
      den.log("sneaky: " .. tostring(select(2, den.downloads.setup{ file = "../secret.txt" })))
    end
    function plugin.actions.bookmarks()
      local marks = den.web.bookmarks()
      den.log("bookmarks: " .. #marks .. " " .. (marks[1] and marks[1].url or ""))
    end
    return plugin
)";

struct Services final : FeatureServices {
    fs::path folder;
    std::vector<DownloadOrder> orders;
    std::vector<std::string> undone;

    Result<PatchFacts> InspectPatch(const fs::path&) override {
        return Status::Fail("no");
    }
    std::optional<GameFacts> FindBase(const BaseWanted&, const std::atomic<bool>&) override {
        return std::nullopt;
    }
    std::vector<GameFacts> Games(const std::vector<std::string>&) override {
        return {};
    }
    std::optional<GameFacts> Game(const std::string&) override {
        return std::nullopt;
    }
    std::vector<HackFacts> Hacks(const std::string&) override {
        return {};
    }
    Result<HackMade> InstallHack(const HackOrder&, const std::function<bool(float)>&) override {
        return Status::Fail("no");
    }
    Status UpdateHack(const std::string&, const std::string&, const std::optional<std::string>&,
                      const std::optional<std::string>&, const fs::path&) override {
        return Status::Fail("no");
    }
    Status RemoveHack(const std::string&, const std::string&) override {
        return Status::Fail("no");
    }
    fs::path DownloadsFolder() override {
        return folder;
    }
    Result<DownloadFacts> SetUpDownload(const DownloadOrder& order, const std::atomic<bool>&) override {
        orders.push_back(order);
        DownloadFacts facts;
        facts.id = "r1";
        facts.outcome = "added";
        facts.message = "Added Space Game to Game Boy Advance";
        facts.download = PathToUtf8(order.file.filename());
        facts.games.push_back({"gba:1", "Space Game", "gba", "", "", 0});
        return facts;
    }
    std::vector<DownloadFacts> DownloadHistory() override {
        DownloadFacts facts;
        facts.id = "r1";
        return {facts};
    }
    Status UndoDownload(const std::string& id) override {
        undone.push_back(id);
        return {};
    }
};

struct Fixture {
    ScratchDir scratch;
    std::shared_ptr<Services> services = std::make_shared<Services>();

    std::unique_ptr<ScriptRunner> Run(const std::string& manifest = MANIFEST) {
        scratch.Write("package/main.lua", SCRIPT);
        services->folder = scratch.dir / "app" / "downloads";
        fs::create_directories(services->folder);
        RunnerConfig config;
        config.manifest = ParseFeatureManifest(manifest).take();
        config.package_dir = scratch.dir / "package";
        config.data_dir = scratch.dir / "data";
        config.bookmarks_file = scratch.dir / "app" / "browser" / "bookmarks.json";
        config.platform = "test";
        config.services = services;
        config.web_available = [] { return true; };
        config.now = [] { return std::int64_t{1794528000}; };
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

TEST_CASE("browse.any and downloads.setup are explained and kept for official plugins") {
    const auto manifest = ParseFeatureManifest(MANIFEST);
    REQUIRE(manifest.ok());
    const std::vector<PermissionLine> lines = PermissionLines(manifest.value().access);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].text ==
          "Show any website inside EmuDEN and keep what you download there in EmuDEN's Downloads folder");
    CHECK(lines[1].text == "Add the games and patches you download to your library");
    CHECK(CheckTrust(manifest.value(), FeaturePlugin::Trust::Official, {}).ok);
    FeatureManifest other = manifest.value();
    other.id = "someone.browser";
    CHECK_FALSE(CheckTrust(other, FeaturePlugin::Trust::ThirdParty, "someone").ok);
}

TEST_CASE("den.web.open{any = true}: any site, the file stays in Downloads and is set up") {
    Fixture f;
    auto runner = f.Run();
    runner->Call({"actions", "browse"}, {});
    runner->Settle();
    std::vector<Ask> asks = runner->TakeAsks();
    REQUIRE(asks.size() == 1);
    const Ask open = asks.front();
    REQUIRE(open.kind == AskKind::OpenWeb);
    CHECK(open.value == "https://duckduckgo.com/");
    CHECK(open.hosts == std::vector<std::string>{"*"});
    CHECK(open.extensions == std::vector<std::string>{"*"});
    CHECK(open.message == "browser");

    // The screen saved a game straight into the Downloads folder.
    const fs::path file = f.services->folder / "Space Game.gba";
    std::ofstream(file, std::ios::binary) << "ROM";
    REQUIRE(runner->Answer(open.id, true,
                           WebFileAnswer({PathToUtf8(file), "Space Game.gba", "homebrew.example.org", "Space Game by Someone"})));
    runner->Settle();
    INFO(runner->Log().back());
    CHECK(Logged(*runner, "got Space Game.gba | Space Game by Someone | homebrew.example.org"));
    CHECK(Logged(*runner, "setup: added Added Space Game to Game Boy Advance"));
    CHECK(Logged(*runner, "history: 1 r1"));
    CHECK(Logged(*runner, "undo: true"));
    CHECK(Logged(*runner, "files: 1 Space Game.gba"));
    REQUIRE(f.services->orders.size() == 1);
    CHECK(f.services->orders[0].page_title == "Space Game by Someone");
    CHECK(f.services->orders[0].plugin == "emuden.browser");
    CHECK(f.services->undone == std::vector<std::string>{"r1"});
    CHECK(fs::exists(file)); // the host never moves it for the script
}

TEST_CASE("den.downloads takes only files of the folder by name") {
    Fixture f;
    auto runner = f.Run();
    runner->Call({"actions", "sneaky"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "sneaky: that is not the name of a file in Downloads"));
    CHECK(f.services->orders.empty());
}

TEST_CASE("den.web.bookmarks lists the browser's bookmarks") {
    Fixture f;
    f.scratch.Write("app/browser/bookmarks.json", R"([{"title":"Homebrew","url":"https://homebrew.example.org/"}])");
    auto runner = f.Run();
    runner->Call({"actions", "bookmarks"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "bookmarks: 1 https://homebrew.example.org/"));
}

TEST_CASE("without the permissions there is no browser and no den.downloads") {
    Fixture f;
    auto runner = f.Run(R"({"id":"emuden.other","name":"Other","version":"1.0.0","kind":"feature","api":1,
      "needs":["kind.feature","feature.lua.1"],"feature":{"entry":"main.lua","permissions":[]}})");
    runner->Call({"actions", "browse"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "ended: the plugin has no permission to show any website"));
    CHECK(runner->TakeAsks().empty());
}
