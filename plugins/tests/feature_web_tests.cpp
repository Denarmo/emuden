// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.web: the "browse" permission and what a plugin is handed from the in-app browser. The
// screen's part is played over the fake web view: a test is the user who follows links and
// presses download buttons.

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "den/platform/webview.h"
#include "feature/runner.h"
#include "feature_test_util.h"
#include "seven_zip_writer.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::feature;
using namespace den::plugins::testing;

namespace {

using ScriptRunner = den::plugins::feature::Runner;

std::string ManifestOf(const std::string& feature) {
    return R"({"id":"test.hacks","name":"Hacks","version":"1.0.0","kind":"feature","api":1,
      "needs":["kind.feature","feature.lua.1","feature.web.1"],"feature":)" +
           feature + "}";
}

const std::string BROWSING = ManifestOf(R"({"entry":"main.lua","permissions":["browse"],
    "browse_hosts":["www.example.org","example.org"],"file_types":["bps","xdelta","zip"]})");

const char* const SCRIPT = R"(
    local plugin = { actions = {} }
    function plugin.actions.browse(arg)
      local available, mode = den.web.available()
      den.log("available=" .. tostring(available) .. " mode=" .. mode)
      while true do
        local request = { url = "https://www.example.org/hacks", title = "Hacks" }
        if arg then request.extensions = arg end
        local file, why = den.web.open(request)
        if not file then
          den.log("ended: " .. tostring(why))
          break
        end
        den.log("got " .. file.path .. " | " .. file.name .. " | " .. file.source_host)
        if file.name == "last.bps" then den.web.close() end
      end
    end
    function plugin.actions.try(request)
      local file, why = den.web.open(request)
      den.log("try: " .. tostring(file) .. " " .. tostring(why))
    end
    return plugin
)";

// What the app's web page does for a plugin, over the fake web view.
struct Screen {
    ScriptRunner& runner;
    fs::path inbox;
    std::unique_ptr<pal::FakeWebView> view;
    Ask open;
    std::vector<std::string> toasts, refused_downloads;
    std::map<std::uint64_t, pal::WebDownload> downloads;
    int closes = 0;

    void Serve() {
        runner.Settle();
        for (Ask& ask : runner.TakeAsks()) {
            if (ask.kind == AskKind::Toast) {
                toasts.push_back(ask.message);
            } else if (ask.kind == AskKind::CloseWeb) {
                ++closes;
            } else if (ask.kind == AskKind::OpenWeb) {
                open = ask;
                pal::WebViewConfig config;
                config.allowed_hosts = ask.hosts;
                view = pal::CreateFakeWebView(config);
                pal::WebCallbacks callbacks;
                callbacks.on_download = [this](const pal::WebDownload& download) {
                    const std::string extension = ExtensionOf(PathFromUtf8(download.file_name));
                    if (std::find(open.extensions.begin(), open.extensions.end(), extension) ==
                        open.extensions.end()) {
                        refused_downloads.push_back(download.file_name);
                        return pal::WebDownloadDecision::Refuse();
                    }
                    downloads[download.id] = download;
                    fs::create_directories(inbox);
                    return pal::WebDownloadDecision::Accept(inbox / PathFromUtf8(download.file_name));
                };
                callbacks.on_download_finished = [this](std::uint64_t id, const fs::path& file) {
                    Hand(file, downloads[id].file_name, downloads[id].host);
                };
                view->SetCallbacks(std::move(callbacks));
                view->Navigate(ask.value);
            }
        }
    }
    void Hand(const fs::path& file, const std::string& name, const std::string& host) {
        REQUIRE(runner.Answer(open.id, true, WebFileAnswer({PathToUtf8(file), name, host})));
        Serve();
    }
    void Download(const std::string& name, const std::string& content) {
        view->SimulateDownload("https://files.example.net/get/" + name, name, "application/octet-stream", content);
        Serve();
    }
    void ClosePage() {
        REQUIRE(runner.Answer(open.id, false, {}));
        view.reset();
        Serve();
    }
};

struct Stage {
    ScratchDir scratch;
    std::shared_ptr<FakeServices> services = std::make_shared<FakeServices>();
    bool engine = true;

    bool developer = false;

    std::unique_ptr<ScriptRunner> Run(const std::string& manifest = BROWSING) {
        scratch.Write("package/main.lua", SCRIPT);
        RunnerConfig config;
        config.manifest = ParseFeatureManifest(manifest).take();
        config.package_dir = scratch.dir / "package";
        config.data_dir = scratch.dir / "data";
        config.app_version = "1.2.3";
        config.platform = "test";
        config.developer = developer;
        config.services = services;
        config.web_available = [this] { return engine; };
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

JsonValue Request(const std::string& json) {
    return ParseJson(json).value();
}

} // namespace

TEST_CASE("browse is declared with its hosts and explained") {
    const auto manifest = ParseFeatureManifest(BROWSING);
    REQUIRE(manifest.ok());
    CHECK(manifest.value().access.browse_hosts == std::vector<std::string>{"www.example.org", "example.org"});
    const std::vector<PermissionLine> lines = PermissionLines(manifest.value().access);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].permission == "browse");
    CHECK(lines[0].text == "Shows these websites inside EmuDEN: www.example.org, example.org");

    CHECK(ParseFeatureManifest(ManifestOf(R"({"entry":"main.lua","permissions":["browse"]})")).error() ==
          "browse needs browse_hosts");
    CHECK_FALSE(ParseFeatureManifest(ManifestOf(
                    R"({"entry":"main.lua","permissions":["browse"],"browse_hosts":["*.example.org"]})"))
                    .ok());
    // Hosts without the permission grant nothing.
    const auto plain = ParseFeatureManifest(
        ManifestOf(R"({"entry":"main.lua","permissions":[],"browse_hosts":["www.example.org"]})"));
    REQUIRE(plain.ok());
    CHECK(plain.value().access.browse_hosts.empty());

    // A new site in an update is something to agree to, and the grant keeps the hosts.
    FeatureAccess granted = manifest.value().access;
    FeatureAccess wanted = granted;
    wanted.browse_hosts.push_back("hacks.example.com");
    const FeatureAccess beyond = AccessBeyond(wanted, granted);
    CHECK(beyond.browse_hosts == std::vector<std::string>{"hacks.example.com"});
    CHECK(beyond.Has("browse"));
    CHECK(Empty(AccessBeyond(granted, granted)));
    CHECK(AccessFromJson(AccessToJson(wanted)).browse_hosts == wanted.browse_hosts);
}

TEST_CASE("den.web.open is refused without the permission, for other hosts and for games") {
    Stage stage;
    {
        const auto runner = stage.Run(ManifestOf(R"({"entry":"main.lua","permissions":[]})"));
        runner->Call({"actions", "try"}, {Request(R"({"url":"https://www.example.org/"})")});
        runner->Settle();
        CHECK(Logged(*runner, "try: nil the plugin has no permission to show websites"));
        CHECK(runner->TakeAsks().empty());
    }
    const auto runner = stage.Run();
    const auto refused = [&](const std::string& request, const std::string& why) {
        runner->Call({"actions", "try"}, {Request(request)});
        runner->Settle();
        INFO(request);
        CHECK(Logged(*runner, "try: nil " + why));
        CHECK(runner->TakeAsks().empty());
    };
    refused(R"({"url":"https://evil.example.net/"})", "the plugin did not declare the host evil.example.net");
    refused(R"({"url":"http://www.example.org/"})", "only https addresses are allowed");
    refused(R"({"url":"https://www.example.org/","hosts":["www.example.org","cdn.example.net"]})",
            "the plugin did not declare the host cdn.example.net");
    // The page may be narrowed to some of the declared hosts, and then the address must be on them.
    refused(R"({"url":"https://www.example.org/","hosts":["example.org"]})",
            "the plugin did not declare the host www.example.org");
    refused(R"({"url":"https://www.example.org/","extensions":["gba","nds","iso"]})",
            "only patch files can be taken from a web site, never a game");
    refused(R"({"url":"https://www.example.org/","extensions":["exe","rar"]})",
            "only patch files can be taken from a web site, never a game");
}

TEST_CASE("only a developer plugin shows a test site of this machine over http") {
    const std::string local = ManifestOf(R"({"entry":"main.lua","permissions":["browse"],
        "browse_hosts":["127.0.0.1"],"file_types":["bps"]})");
    const JsonValue request = Request(R"({"url":"http://127.0.0.1:28123/index.html"})");
    Stage stage;
    {
        const auto runner = stage.Run(local);
        runner->Call({"actions", "try"}, {request});
        runner->Settle();
        CHECK(Logged(*runner, "try: nil only https addresses are allowed"));
        CHECK(runner->TakeAsks().empty());
    }
    stage.developer = true;
    const auto runner = stage.Run(local);
    runner->Call({"actions", "try"}, {request});
    runner->Settle();
    const std::vector<Ask> asks = runner->TakeAsks();
    REQUIRE(asks.size() == 1);
    CHECK(asks[0].kind == AskKind::OpenWeb);
    CHECK(asks[0].value == "http://127.0.0.1:28123/index.html");
    CHECK(asks[0].hosts == std::vector<std::string>{"127.0.0.1"});
}

TEST_CASE("every patch the user downloads in the page reaches the script") {
    Stage stage;
    const auto runner = stage.Run();
    Screen screen{*runner, stage.scratch.dir / "app-inbox"};
    // The plugin asks for a ROM type too: it is not on the list the page gets.
    runner->Call({"actions", "browse"}, {Request(R"(["bps","GBA","zip","nds"])")});
    screen.Serve();
    CHECK(Logged(*runner, "available=true mode=in-app"));
    REQUIRE(screen.view);
    CHECK(screen.open.value == "https://www.example.org/hacks");
    CHECK(screen.open.title == "Hacks");
    CHECK(screen.open.hosts == std::vector<std::string>{"www.example.org", "example.org"});
    CHECK(screen.open.extensions == std::vector<std::string>{"bps", "zip"});
    CHECK(screen.view->State().url == "https://www.example.org/hacks");

    // The user browses: only the plugin's sites.
    CHECK(screen.view->SimulateLink("https://example.org/hack/kaizo"));
    CHECK_FALSE(screen.view->SimulateLink("https://elsewhere.example.net/"));

    // A patch, from the site's file host.
    screen.Download("Kaizo v2.bps", "BPS1 a patch");
    CHECK(Logged(*runner, "got inbox/Kaizo v2.bps | Kaizo v2.bps | files.example.net"));
    CHECK(fs::exists(stage.scratch.dir / "data/inbox/Kaizo v2.bps"));
    CHECK_FALSE(fs::exists(screen.inbox / "Kaizo v2.bps")); // moved, not copied

    // A game: the page does not take it.
    screen.Download("Emerald.gba", "a whole game");
    CHECK(screen.refused_downloads == std::vector<std::string>{"Emerald.gba"});
    CHECK_FALSE(fs::exists(stage.scratch.dir / "data/inbox/Emerald.gba"));

    // The script went back to waiting, so the next file arrives too. A zip gives its patch.
    screen.Download("pack.zip", StoredZipOf({{"readme.txt", "hello"}, {"hack/Crystal Clear.bps", "BPS1 more"}}));
    CHECK(Logged(*runner, "got inbox/Crystal Clear.bps | Crystal Clear.bps | files.example.net"));
    CHECK(fs::is_empty(screen.inbox));
    CHECK(screen.toasts.empty());

    // The plugin closes the page once it has what it wanted; the screen says when it is gone.
    screen.Download("last.bps", "BPS1 last");
    CHECK(screen.closes == 1);
    screen.ClosePage();
    CHECK(Logged(*runner, "ended: closed"));
    // Nothing of the visit is left to answer.
    CHECK_FALSE(runner->Answer(screen.open.id, true, WebFileAnswer({"x", "x.bps", "h"})));
}

TEST_CASE("the host takes no game and nothing that is not a patch, whatever it is handed") {
    Stage stage;
    const auto runner = stage.Run();
    Screen screen{*runner, stage.scratch.dir / "app-inbox"};
    runner->Call({"actions", "browse"}, {});
    screen.Serve();
    REQUIRE(screen.view);
    // Without a list in the call, the manifest's file types that are patches or zip.
    CHECK(screen.open.extensions == std::vector<std::string>{"bps", "xdelta", "zip"});

    const auto hand = [&](const std::string& name, const std::string& content) {
        stage.scratch.Write("app-inbox/" + name, content);
        screen.Hand(screen.inbox / PathFromUtf8(name), name, "www.example.org");
    };
    // A screen that let a game through: the host still refuses it and deletes the file.
    hand("Emerald.gba", "a whole game");
    REQUIRE(screen.toasts.size() == 1);
    CHECK(screen.toasts[0].find("this is a full game file") != std::string::npos);
    // A patched game under a patch's name.
    hand("Emerald Kaizo.bps", "a whole game, renamed");
    REQUIRE(screen.toasts.size() == 2);
    CHECK(screen.toasts[1].find("not a patch") != std::string::npos);
    // A type the plugin did not ask for.
    hand("hack.ips", "PATCH");
    REQUIRE(screen.toasts.size() == 3);
    CHECK(screen.toasts[2].find("does not take this kind of file") != std::string::npos);
    // An archive with a game beside the patch, and one without a patch.
    hand("bundle.zip", StoredZipOf({{"hack.bps", "BPS1 x"}, {"Emerald (patched).gba", "game"}}));
    REQUIRE(screen.toasts.size() == 4);
    CHECK(screen.toasts[3].find("holds a full game file") != std::string::npos);
    hand("notes.zip", StoredZipOf({{"readme.txt", "hello"}}));
    REQUIRE(screen.toasts.size() == 5);
    CHECK(screen.toasts[4].find("no patch file") != std::string::npos);

    CHECK_FALSE(Logged(*runner, "got "));
    CHECK(fs::is_empty(screen.inbox));
    CHECK_FALSE(fs::exists(stage.scratch.dir / "data/inbox"));
    screen.ClosePage();
    CHECK(Logged(*runner, "ended: closed"));
}

TEST_CASE("a 7z is opened like a zip: its patch is taken, one holding a game is refused") {
    Stage stage;
    const auto runner = stage.Run(ManifestOf(R"({"entry":"main.lua","permissions":["browse"],
        "browse_hosts":["www.example.org","example.org"],"file_types":["bps","zip","7z","rar"]})"));
    Screen screen{*runner, stage.scratch.dir / "app-inbox"};
    runner->Call({"actions", "browse"}, {});
    screen.Serve();
    REQUIRE(screen.view);
    // rar is never on the page's list: the host could not look inside.
    CHECK(screen.open.extensions == std::vector<std::string>{"bps", "zip", "7z"});

    screen.Download("pack.7z", den::test::StoredSevenZip({{"readme.txt", "hello"},
                                                         {"hack/Crystal Clear.bps", "BPS1 more"}}));
    CHECK(Logged(*runner, "got inbox/Crystal Clear.bps | Crystal Clear.bps | files.example.net"));
    CHECK(fs::is_empty(screen.inbox));
    CHECK(screen.toasts.empty());

    const auto hand = [&](const std::string& name, const std::string& content) {
        stage.scratch.Write("app-inbox/" + name, content);
        screen.Hand(screen.inbox / PathFromUtf8(name), name, "www.example.org");
    };
    hand("bundle.7z", den::test::StoredSevenZip({{"hack.bps", "BPS1 x"}, {"Emerald (patched).gba", "game"}}));
    REQUIRE(screen.toasts.size() == 1);
    CHECK(screen.toasts[0].find("holds a full game file") != std::string::npos);
    hand("notes.7z", den::test::StoredSevenZip({{"readme.txt", "hello"}}));
    REQUIRE(screen.toasts.size() == 2);
    CHECK(screen.toasts[1].find("no patch file") != std::string::npos);
    // A zip that is a 7z inside is judged by what it is.
    hand("sneaky.zip", den::test::StoredSevenZip({{"Emerald.gba", "game"}}));
    REQUIRE(screen.toasts.size() == 3);
    CHECK(screen.toasts[2].find("holds a full game file") != std::string::npos);
    // What 7-Zip wrote: a .bin beside a text, a text alone, and names behind a password.
    const auto vector = [](const char* name) {
        const auto bytes = ReadBinaryFile(fs::path(DEN_ARCHIVE_FIXTURES) / name);
        REQUIRE(bytes);
        return std::string(bytes->begin(), bytes->end());
    };
    hand("texts.7z", vector("lzma-each.7z"));
    REQUIRE(screen.toasts.size() == 4);
    CHECK(screen.toasts[3].find("holds a full game file") != std::string::npos); // its code.bin
    hand("text.7z", vector("ppmd.7z"));
    REQUIRE(screen.toasts.size() == 5);
    CHECK(screen.toasts[4].find("no patch file") != std::string::npos);
    hand("locked.7z", vector("password-names.7z"));
    REQUIRE(screen.toasts.size() == 6);
    CHECK(screen.toasts[5].find("cannot be read") != std::string::npos);

    CHECK(fs::is_empty(screen.inbox));
    screen.ClosePage();
}

TEST_CASE("without a web engine the same call works through the user's browser") {
    Stage stage;
    stage.engine = false;
    const auto runner = stage.Run();
    Screen screen{*runner, stage.scratch.dir / "app-inbox"};
    runner->Call({"actions", "browse"}, {});
    screen.Serve();
    CHECK(Logged(*runner, "available=false mode=browser"));
    // The same ask: the page shows its fallback (the browser and the Downloads folder) and
    // hands over what the user agreed to there.
    REQUIRE(screen.open.kind == AskKind::OpenWeb);
    stage.scratch.Write("app-inbox/Prism.bps", "BPS1 from Downloads");
    screen.Hand(screen.inbox / "Prism.bps", "Prism.bps", "www.example.org");
    CHECK(Logged(*runner, "got inbox/Prism.bps | Prism.bps | www.example.org"));
    screen.ClosePage();
    CHECK(Logged(*runner, "ended: closed"));
}

TEST_CASE("den.open_url opens the browser only for something the user pressed") {
    Stage stage;
    stage.scratch.Write("package/main.lua", "");
    const std::string manifest = ManifestOf(
        R"({"entry":"main.lua","permissions":["open-url"],"open_hosts":["www.example.org"]})");
    RunnerConfig config;
    config.manifest = ParseFeatureManifest(manifest).take();
    config.package_dir = stage.scratch.dir / "package";
    config.data_dir = stage.scratch.dir / "data";
    stage.scratch.Write("package/main.lua", R"(
        local plugin = { actions = {} }
        local function go(from)
          local ok, why = den.open_url("https://www.example.org/discover")
          den.log(from .. ": " .. tostring(ok) .. " " .. tostring(why))
        end
        function plugin.open() go("open") end
        function plugin.resume() go("resume") end
        function plugin.actions.site() den.sleep(1) go("pressed") end
        return plugin
    )");
    ScriptRunner runner(std::move(config));
    runner.Call({"open"}, {});
    runner.Call({"resume"}, {});
    runner.Settle();
    CHECK(Logged(runner, "open: nil a page is opened in the browser only when the user asks for it"));
    CHECK(Logged(runner, "resume: nil a page"));
    CHECK(runner.TakeAsks().empty());
    // After a wait it is still the user's press.
    runner.Call({"actions", "site"}, {});
    runner.Settle();
    CHECK(Logged(runner, "pressed: true"));
    const std::vector<Ask> asks = runner.TakeAsks();
    REQUIRE(asks.size() == 1);
    CHECK(asks[0].kind == AskKind::OpenUrl);
    CHECK(asks[0].value == "https://www.example.org/discover");
}
