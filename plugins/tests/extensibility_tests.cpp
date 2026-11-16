// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What outside developers rely on: the capability list both repositories share, permissions
// tied to their capabilities, official-only permissions, and the developer folder.

#include <algorithm>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "den/base/json.h"
#include "den/plugins/feature_manifest.h"
#include "den/plugins/installer.h"
#include "den/plugins/manifest.h"
#include "den/plugins/sources.h"
#include "den/store/settings.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using den::plugins::testing::HashOf;
using den::plugins::testing::ScratchDir;
using den::plugins::testing::StoredZipOf;

namespace {

std::string Feature(const std::string& id, const std::string& needs, const std::string& feature) {
    return R"({"manifest_version":1,"id":")" + id + R"(","name":"T","version":"1.0.0","kind":"feature",
        "publisher":"T","api":1,"needs":[)" + needs + R"(],"feature":)" + feature + "}";
}

std::string ConsoleJson(const std::string& id, const std::string& extra, const std::string& download) {
    return R"({"manifest_version":1,"id":")" + id + R"(","name":"Dev Console","version":"0.1.0",
        "kind":"console","engine":"libretro","publisher":"Dev","api":1,"needs":["engine.libretro"],)" + extra +
           R"("systems":[{"id":"devcon","name":"Dev Console","group":"other","order":5000,
            "content":{"extensions":["dev"]},"presentation":{"cover_aspect":1},"cores":["devcore"]}],
         "components":[{"id":"devcore","type":"libretro-core","name":"Dev Core",
            "downloads":{"test-plat":)" + download + "}}]}";
}

} // namespace

TEST_CASE("contracts/capabilities.json is what the app accepts") {
    const auto text = ReadTextFile(PathFromUtf8(DEN_CONTRACTS_DIR) / "capabilities.json");
    REQUIRE(text);
    const auto root = ParseJson(*text);
    REQUIRE(root);
    std::set<std::string> listed;
    for (const JsonValue& item : root->Find("capabilities")->items) {
        listed.insert(item.String("name"));
    }
    const auto& host = HostCapabilities();
    CHECK(listed == std::set<std::string>(host.begin(), host.end()));

    std::set<std::string> permissions;
    for (const JsonValue& item : root->Find("permissions")->items) {
        const std::string name = item.String("name");
        permissions.insert(name);
        const JsonValue* needs = item.Find("needs");
        const char* capability = PermissionCapability(name);
        INFO(name);
        CHECK((needs->kind == JsonValue::Kind::String ? needs->string : std::string()) ==
              (capability ? std::string(capability) : std::string()));
        CHECK(item.Find("official_only")->boolean == OfficialOnlyPermission(name));
    }
    const auto known = KnownPermissions();
    CHECK(permissions == std::set<std::string>(known.begin(), known.end()));
}

TEST_CASE("a permission needs the capability that guards it") {
    const std::string base = R"("kind.feature","feature.lua.1")";
    const std::string browse = R"({"entry":"main.lua","permissions":["browse"],"browse_hosts":["example.org"]})";
    CHECK_FALSE(ParseFeatureManifest(Feature("t.web", base, browse)).ok());
    CHECK(ParseFeatureManifest(Feature("t.web", base + R"(,"feature.web.1")", browse)).ok());
    CHECK_FALSE(ParseFeatureManifest(Feature("t.dev", base, R"({"entry":"main.lua","permissions":["device.info"]})")).ok());
    CHECK_FALSE(ParseFeatureManifest(
                    Feature("t.ach", base, R"({"entry":"main.lua","permissions":["achievements"]})")).ok());
    // A script without the Lua API it is written against.
    CHECK_FALSE(ParseFeatureManifest(Feature("t.lua", R"("kind.feature")", R"({"entry":"main.lua","permissions":[]})")).ok());
}

TEST_CASE("only EmuDEN's own plugins sign in to RetroAchievements") {
    auto manifest = ParseFeatureManifest(
        Feature("jdoe.cheevos", R"("kind.feature","feature.lua.1","feature.achievements.1")",
                R"({"entry":"main.lua","permissions":["achievements"]})"));
    REQUIRE(manifest.ok());
    CHECK_FALSE(CheckTrust(*manifest, FeaturePlugin::Trust::ThirdParty, "jdoe").ok);
    CHECK_FALSE(CheckTrust(*manifest, FeaturePlugin::Trust::Developer, {}).ok);
    auto own = ParseFeatureManifest(
        Feature("emuden.retroachievements", R"("kind.feature","feature.lua.1","feature.achievements.1")",
                R"({"entry":"main.lua","permissions":["achievements"]})"));
    REQUIRE(own.ok());
    CHECK(CheckTrust(*own, FeaturePlugin::Trust::Official, {}).ok);
}

TEST_CASE("a sync provider says how it lists") {
    const std::string needs = R"("kind.feature","feature.lua.1")";
    const auto with = [&](const std::string& sync) {
        return ParseFeatureManifest(Feature("t.sync", needs,
                                            R"({"entry":"main.lua","permissions":["sync"],"sync":)" + sync + "}"));
    };
    CHECK(with(R"({"categories":["saves"]})").value().sync_listing == SYNC_LISTING_CATEGORY);
    CHECK(with(R"({"categories":["saves"],"listing":"per_folder"})").value().sync_listing == SYNC_LISTING_PER_FOLDER);
    CHECK_FALSE(with(R"({"categories":["saves"],"listing":"sometimes"})").ok());
}

TEST_CASE("console manifests: platforms, depends, settings and capabilities") {
    const std::string pinned = R"({"url":"https://example.org/c.zip","sha256":")" + std::string(64, 'a') +
                               R"(","size":1,"archive":"zip","entry":"devcore_libretro.dll"})";
    auto parsed = ParseManifest(ConsoleJson("jdoe.dev", R"("platforms":["other-plat"],
        "depends":[{"id":"jdoe.base","version":">=1.0.0"}],"capabilities":{"netplay":false},)",
                                              pinned));
    REQUIRE(parsed.ok());
    CHECK(parsed->CoresFor(parsed->systems[0], "test-plat").empty());
    auto here = ParseManifest(ConsoleJson("jdoe.dev", R"("platforms":["other-plat","test-plat"],)", pinned));
    CHECK(here->CoresFor(here->systems[0], "test-plat").size() == 1);
    REQUIRE(parsed->depends.size() == 1);
    CHECK(parsed->depends[0].range == ">=1.0.0");
    CHECK_FALSE(parsed->systems[0].console.netplay);
    CHECK_FALSE(ParseManifest(ConsoleJson("jdoe.dev", R"("depends":[{"id":"jdoe.base","when":"x"}],)", pinned)).ok());

    auto settings = ParseManifest(ConsoleJson("jdoe.dev", R"("settings":[{"key":"fast","label":"Fast","type":"toggle","default":true,"section":"Speed",
                     "when_core":"devcore","apply":{"core_option":"dev_fast","map":{"true":"on","false":"off"}}},
                    {"key":"other","label":"Other","type":"toggle","default":false,"when_core":"elsecore",
                     "apply":{"core_option":"dev_other"}}],)",
                                                pinned));
    REQUIRE(settings.ok());
    REQUIRE(settings->settings.size() == 2);
    const ManifestSetting& fast = settings->settings[0];
    CHECK(fast.spec.section == "Speed");
    CHECK(fast.Applies("devcon", "test-plat", "devcore"));
    CHECK_FALSE(settings->settings[1].Applies("devcon", "test-plat", "devcore"));
    CHECK(fast.OptionValue("true") == "on");
}

TEST_CASE("the developer folder: local files, its own rules, only in developer mode") {
    const std::string local = R"({"file":"C:/nowhere/devcore.zip","archive":"zip","entry":"devcore_libretro.dll"})";
    // A local file is for the developer folder only.
    CHECK_FALSE(ParseManifest(ConsoleJson("jdoe.dev", "", local)).ok());
    REQUIRE(ParseManifest(ConsoleJson("jdoe.dev", "", local), true).ok());
    // The engines and scanners of EmuDEN's own plugins stay EmuDEN's there too.
    std::string engine = ConsoleJson("jdoe.dev", "", local);
    engine.replace(engine.find(R"("engine":"libretro")"), 19, R"("engine":"switch2")");
    auto switch2 = ParseManifest(engine, true);
    CHECK((!switch2.ok() || !CheckDeveloperManifest(*switch2).ok));

    ScratchDir scratch;
    const std::string library = "not really a core";
    const std::string zip = StoredZipOf({{"devcore_libretro.dll", library}});
    scratch.Write("built/devcore.zip", zip);
    std::string file = PathToUtf8(scratch.dir / "built" / "devcore.zip");
    std::replace(file.begin(), file.end(), '\\', '/');
    std::string manifest = ConsoleJson(
        "jdoe.dev", "", R"({"file":")" + file + R"(","sha256":")" + HashOf(zip) +
                            R"(","archive":"zip","entry":"devcore_libretro.dll"})");
    scratch.Write("data/plugins-dev/jdoe.dev/plugin.json", manifest);

    Jobs jobs(1);
    store::Settings settings(scratch.dir / "settings.json");
    Config config;
    config.data_dir = scratch.dir / "data";
    config.jobs = &jobs;
    config.settings = &settings;
    HostDeps deps;
    deps.http = [](const pal::HttpRequest&) { return pal::HttpResponse{}; };
    deps.source = "github:tester/plugins@main";
    deps.platform_key = "test-plat";
    deps.bundled = false;
    auto host = CreateHostWith(config, std::move(deps));
    CHECK_FALSE(host->FindConsole("devcon"));

    settings.SetBool(DEVELOPER_KEY, true);
    host->ReloadDeveloper();
    const auto console = host->FindConsole("devcon");
    REQUIRE(console);
    CHECK(console->developer);
    CHECK(host->DeveloperDir() == scratch.dir / "data" / "plugins-dev");

    JobPtr install = host->Install("devcon");
    jobs.Wait(install);
    INFO(install->Error());
    REQUIRE(install->State() == JobState::Done);
    const auto core = host->CoreFor("devcon");
    REQUIRE(core);
    CHECK(core->installed);
    CHECK(host->VerifyCore(core->library).ok);
    CHECK_FALSE(host->FindConsole("devcon")->update_available);

    settings.SetBool(DEVELOPER_KEY, false);
    host->ReloadDeveloper();
    CHECK_FALSE(host->FindConsole("devcon"));
}
