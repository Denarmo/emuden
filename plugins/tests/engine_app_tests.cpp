// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Engine apps as a console's cores: the manifest's engine-app component and installing one.

#include <catch2/catch_test_macros.hpp>

#include "den/plugins/installer.h"
#include "den/plugins/manifest.h"
#include "den/plugins/sources.h"
#include "den/store/settings.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::testing;

namespace {

const std::string CERT = "AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89:AB:CD:EF:01:23:45:67:89";
const std::string EXE = "this stands in for an engine";

std::string ManifestText(const std::string& engine_component) {
    return R"({"manifest_version":1,"id":"emuden.testcon","name":"Test Console","version":"1.0.0",
      "kind":"console","engine":"libretro","publisher":"Tester","api":1,"needs":["engine.libretro"],
      "systems":[{"id":"testcon","name":"Test Console","cores":["testengine","testcore"]}],
      "components":[)" + engine_component + R"(,
        {"id":"testcore","type":"libretro-core","name":"Test Core",
         "downloads":{"test-plat":{"url":"https://example.org/core.zip","sha256":")" +
           std::string(64, 'a') + R"(","size":10,"archive":"zip","entry":"testcore_libretro.dll"}}}]})";
}

std::string EngineComponent(const std::string& zip_sha256, std::size_t zip_size) {
    return R"({"id":"testengine","type":"engine-app","name":"Test Engine","version":"2.0.0",
      "ecp":{"id":"io.github.example.testengine","level":2,
             "capabilities":["pause","menu","states"],"needs":["bios"]},
      "downloads":{"test-plat":{"url":"https://example.org/engine.zip","sha256":")" + zip_sha256 +
           R"(","size":)" + std::to_string(zip_size) + R"(,"archive":"zip","entry":"TestEngine/"}},
      "executable":{"test-plat":"TestEngine/test-engine.exe"},
      "android":{"package":"io.github.example.testengine","cert_sha256":")" + CERT + R"(",
                 "min_version_code":3,
                 "apk":{"url":"https://example.org/engine.apk","sha256":")" + std::string(64, 'b') +
           R"(","size":100,"archive":"none"},
                 "store":"https://example.org/store/testengine"}})";
}

} // namespace

TEST_CASE("an engine app is one of a console's cores", "[plugins][engine-app]") {
    auto manifest = ParseManifest(ManifestText(EngineComponent(std::string(64, 'c'), 10)));
    REQUIRE(manifest);
    const Component* engine = manifest->FindComponent("testengine");
    REQUIRE(engine != nullptr);
    CHECK(engine->type == TYPE_ENGINE_APP);
    CHECK(engine->engine_app.ecp_id == "io.github.example.testengine");
    CHECK(engine->engine_app.level == 2);
    CHECK(engine->engine_app.capabilities.size() == 3);
    CHECK(engine->engine_app.needs == std::vector<std::string>{"bios"});
    CHECK(engine->engine_app.package == "io.github.example.testengine");
    CHECK(engine->engine_app.cert_sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    CHECK(engine->engine_app.min_version_code == 3);
    REQUIRE(engine->engine_app.apk);
    CHECK(engine->ExecutableFor("test-plat") == "TestEngine/test-engine.exe");

    // Listed with the libretro cores, best first, wherever it can be installed.
    const auto desktop = manifest->CoresFor(manifest->systems.front(), "test-plat");
    REQUIRE(desktop.size() == 2);
    CHECK(desktop[0]->id == "testengine");
    const auto android = manifest->CoresFor(manifest->systems.front(), "android-arm64");
    REQUIRE(android.size() == 1);
    CHECK(android[0]->id == "testengine");
    CHECK(manifest->CoresFor(manifest->systems.front(), "other-plat").empty());
}

TEST_CASE("an engine app that is not pinned is refused", "[plugins][engine-app]") {
    const auto with = [](const std::string& from, const std::string& to) {
        std::string component = EngineComponent(std::string(64, 'c'), 10);
        const std::size_t at = component.find(from);
        REQUIRE(at != std::string::npos);
        component.replace(at, from.size(), to);
        return ParseManifest(ManifestText(component));
    };
    CHECK_FALSE(with(CERT, "not a digest"));
    CHECK_FALSE(with(R"("package":"io.github.example.testengine")", R"("package":"1bad")"));
    CHECK_FALSE(with(R"("level":2)", R"("level":3)"));
    CHECK_FALSE(with(R"("level":2)", R"("level":-1)"));
    // Level 0: an app that is only handed the game.
    auto plain = with(R"("level":2)", R"("level":0)");
    REQUIRE(plain);
    REQUIRE(plain->FindComponent("testengine") != nullptr);
    CHECK(plain->FindComponent("testengine")->engine_app.level == 0);
    CHECK_FALSE(with(R"("TestEngine/test-engine.exe")", R"("../test-engine.exe")"));
    CHECK_FALSE(with(R"("archive":"none")", R"("archive":"zip")"));
    CHECK_FALSE(with(R"("store":"https://)", R"("store":"http://)"));
    std::string no_descriptor = EngineComponent(std::string(64, 'c'), 10);
    no_descriptor.replace(no_descriptor.find(R"("ecp")"), 5, R"("xcp")");
    CHECK_FALSE(ParseManifest(ManifestText(no_descriptor)));
    // Without the android part it is a desktop engine only.
    std::string desktop_only = EngineComponent(std::string(64, 'c'), 10);
    desktop_only = desktop_only.substr(0, desktop_only.find(R"(,
      "android")")) + "}";
    auto manifest = ParseManifest(ManifestText(desktop_only));
    REQUIRE(manifest);
    CHECK(manifest->CoresFor(manifest->systems.front(), "android-arm64").empty());
}

TEST_CASE("an added source offers only engine apps of its own namespace", "[plugins][engine-app]") {
    const auto from = [](const std::string& package, const std::string& plugin = "jdoe.testcon") {
        std::string text = ManifestText(EngineComponent(std::string(64, 'c'), 10));
        for (const std::string& old : {std::string("emuden.testcon"), std::string("io.github.example.testengine")}) {
            for (std::size_t at = text.find(old); at != std::string::npos; at = text.find(old)) {
                text.replace(at, old.size(), old == "emuden.testcon" ? plugin : package);
            }
        }
        return ParseManifest(text);
    };
    auto own = from("io.github.jdoe.testengine");
    REQUIRE(own);
    CHECK(CheckThirdPartyManifest(*own, "jdoe").ok);
    auto plain = from("jdoe.testengine");
    REQUIRE(plain);
    CHECK(CheckThirdPartyManifest(*plain, "jdoe").ok);
    auto other = from("org.ppsspp.ppsspp");
    REQUIRE(other);
    CHECK_FALSE(CheckThirdPartyManifest(*other, "jdoe").ok);
    auto host = from("org.example.testengine", "example.org.testcon");
    REQUIRE(host);
    CHECK(CheckThirdPartyManifest(*host, "example.org").ok);
}

TEST_CASE("a desktop engine app is installed, verified and removed", "[plugins][engine-app]") {
    ScratchDir scratch;
    Jobs jobs(1);
    FakeWeb web;
    const std::string zip = StoredZipOf({{"TestEngine/test-engine.exe", EXE}, {"TestEngine/data/a.bin", "a"}});
    web.files["https://example.org/engine.zip"] = zip;
    scratch.Write("plugins/emuden.testcon/plugin.json", ManifestText(EngineComponent(HashOf(zip), zip.size())));

    store::Settings settings(scratch.dir / "settings.json");
    Config config;
    config.data_dir = scratch.dir;
    config.jobs = &jobs;
    config.settings = &settings;
    HostDeps deps;
    deps.http = web.Getter();
    deps.source = "github:tester/plugins@main";
    deps.platform_key = "test-plat";
    deps.bundled = false;
    auto host = CreateHostWith(config, std::move(deps));

    auto core = host->CoreFor("testcon");
    REQUIRE(core);
    CHECK(core->runner == Runner::Engine);
    CHECK_FALSE(core->installed);
    CHECK(core->engine.level == 2);
    CHECK(core->engine.downloadable);
    CHECK_FALSE(host->Installed("testcon"));

    const JobPtr job = host->Install("testcon");
    jobs.Wait(job);
    REQUIRE(job->State() == JobState::Done);
    core = host->CoreFor("testcon");
    REQUIRE(core);
    CHECK(core->installed);
    CHECK(core->Ready());
    CHECK(core->library == scratch.dir / "engines" / "testengine" / "TestEngine" / "test-engine.exe");
    CHECK(core->engine.executable == core->library);
    CHECK(core->missing_bios.empty());
    CHECK(host->Installed("testcon"));
    CHECK(fs::is_regular_file(scratch.dir / "engines" / "testengine" / "TestEngine" / "data" / "a.bin"));

    // A changed executable is not the one that was installed.
    { std::ofstream(core->library, std::ios::binary) << "something else entirely"; }
    CHECK_FALSE(host->CoreFor("testcon")->installed);

    // The libretro core of the same console can still be chosen.
    host->SelectCore("testcon", "testcore");
    CHECK(host->CoreFor("testcon")->runner == Runner::Core);
    host->SelectCore("testcon", "testengine");

    const JobPtr removed = host->Remove("testcon");
    jobs.Wait(removed);
    CHECK(removed->State() == JobState::Done);
    CHECK_FALSE(fs::exists(scratch.dir / "engines" / "testengine"));
}
