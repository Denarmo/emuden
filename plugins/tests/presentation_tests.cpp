// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What a console manifest says about how its games look and play (screens, pad layout,
// media, netplay, tuning, the default core), and that the official ones say what the app's
// own tables said.

#include <algorithm>
#include <filesystem>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/jobs.h"
#include "den/plugins/installer.h"
#include "den/plugins/manifest.h"
#include "den/store/settings.h"

using namespace den;
using namespace den::plugins;
namespace fs = std::filesystem;

namespace {

const char* const MANIFEST = R"({"manifest_version":1,"id":"jdoe.duo","name":"Duo","version":"1.0.0",
  "kind":"console","engine":"libretro","api":1,"needs":["engine.libretro"],
  "systems":[{"id":"duo","name":"Duo","group":"other","default_core":"second",
    "content":{"extensions":["duo"],"sync":false},
    "presentation":{"layout":"nintendo","digital_only":true,
      "media":{"shape":"card","style":"hucard","aspect":0.64},
      "screens":[{"frame":[320,120],"screens":[{"rect":[0,0,0.5,1]},{"rect":[0.5,0,0.5,1],"touch":true}]},
                 {"frame":[10,10],"screens":[{"rect":[0,0,2,1]}]}]},
    "cores":["first","second"]}],
  "components":[
    {"id":"first","type":"libretro-core","forced_core_options":{"f":"1"},"option_hints":{"h":"2","f":"0"},
     "downloads":{"any":{"url":"https://example.org/a.zip","sha256":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
       "size":4,"archive":"zip","entry":"a.dll"}}},
    {"id":"second","type":"libretro-core","options_name":"Second Core",
     "presentation":{"screens":[{"frame":[256,384],"screens":[{"rect":[0,0,1,0.5]},{"rect":[0,0.5,1,0.5]}]}]},
     "netplay":{"mode":"lockstep","subsystem":257,"description":"Linked.",
       "host":{"save_memory":256,"options":{"side":"one"}},"guest":{"save_memory":768,"options":{"side":"two"}}},
     "tuning":{"opt":[{"min_tier":"mid","value":"on"},{"value":"off"}]},
     "downloads":{"any":{"url":"https://example.org/b.zip","sha256":"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
       "size":4,"archive":"zip","entry":"b.dll"}}}]})";

std::vector<Manifest> Bundled() {
    std::vector<Manifest> out;
    std::size_t count = 0;
    const BundledManifest* bundled = BundledManifests(count);
    for (std::size_t i = 0; i < count; ++i) {
        Result<Manifest> manifest =
            ParseManifest(std::string_view(reinterpret_cast<const char*>(bundled[i].data), bundled[i].size));
        REQUIRE(manifest.ok());
        out.push_back(manifest.take());
    }
    return out;
}

const Component& CoreOf(const std::vector<Manifest>& all, const std::string& plugin, const std::string& core) {
    for (const Manifest& manifest : all) {
        if (manifest.id == plugin) {
            const Component* component = manifest.FindComponent(core);
            REQUIRE(component != nullptr);
            return *component;
        }
    }
    FAIL("no plugin " << plugin);
    throw;
}

} // namespace

TEST_CASE("a console manifest's presentation, netplay and tuning") {
    Result<Manifest> manifest = ParseManifest(MANIFEST);
    INFO(manifest.error());
    REQUIRE(manifest.ok());
    const SystemDef& system = manifest->systems.front();
    const Console& console = system.console;
    CHECK(system.default_core == "second");
    CHECK_FALSE(console.sync);
    CHECK(console.pad_layout == "nintendo");
    CHECK(console.digital_only == std::optional<bool>(true));
    CHECK(console.media.shape == "card");
    CHECK(console.media.style == "hucard");
    CHECK(console.media.aspect == 0.64f);
    REQUIRE(console.screens.size() == 1); // the shape whose screen leaves the picture is left out
    CHECK(console.screens[0].frame_width == 320);
    REQUIRE(console.screens[0].screens.size() == 2);
    CHECK(console.screens[0].screens[1].x == 0.5f);
    CHECK(console.screens[0].screens[1].touch);

    const Component* first = manifest->FindComponent("first");
    const Component* second = manifest->FindComponent("second");
    REQUIRE((first && second));
    CHECK(first->option_hints == std::vector<std::pair<std::string, std::string>>{{"h", "2"}, {"f", "0"}});
    CHECK(first->netplay.mode.empty());
    CHECK(first->screens.empty());
    CHECK(second->options_name == "Second Core");
    REQUIRE(second->screens.size() == 1);
    CHECK(second->screens[0].frame_height == 384);
    CHECK(second->netplay.mode == "lockstep");
    CHECK(second->netplay.subsystem == 257);
    CHECK(second->netplay.host.save_memory == 256);
    CHECK(second->netplay.guest.options == std::vector<std::pair<std::string, std::string>>{{"side", "two"}});
    CHECK(second->tuning.Find("opt") != nullptr);
}

TEST_CASE("what a manifest leaves out") {
    const std::string bare = R"({"manifest_version":1,"id":"jdoe.pc","name":"PC","version":"1.0.0",
      "kind":"console","engine":"libretro","api":1,"needs":["engine.libretro"],
      "systems":[{"id":"apc","name":"A PC","group":"pc"},
                 {"id":"other","name":"Other","group":"other","presentation":{"layout":"sideways",
                  "media":{"shape":"blob"},"digital_only":"yes"}}]})";
    Result<Manifest> manifest = ParseManifest(bare);
    REQUIRE(manifest.ok());
    // A PC's games are not synced unless its manifest says; any other console's are.
    CHECK_FALSE(manifest->systems[0].console.sync);
    const Console& other = manifest->systems[1].console;
    CHECK(other.sync);
    CHECK(other.pad_layout.empty());
    CHECK_FALSE(other.digital_only.has_value());
    CHECK(other.media.shape.empty());
    CHECK(other.screens.empty());
    CHECK(manifest->systems[1].default_core.empty());
}

TEST_CASE("the host chooses the default core and hands out the core's presentation") {
    const fs::path dir = fs::temp_directory_path() / "den-plugins-presentation";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    {
        Jobs jobs(1);
        store::Settings settings(dir / "settings.json");
        Config config;
        config.data_dir = dir;
        config.jobs = &jobs;
        config.settings = &settings;
        // The test's own manifest, installed by hand, and no bundled ones.
        fs::create_directories(dir / "plugins" / "jdoe.duo");
        REQUIRE(WriteFileAtomic(dir / "plugins" / "jdoe.duo" / "plugin.json", MANIFEST));
        HostDeps deps;
        deps.http = [](const pal::HttpRequest&) { return pal::HttpResponse{}; };
        deps.platform_key = "any-plat";
        deps.bundled = false;
        auto host = CreateHostWith(config, std::move(deps));
        auto core = host->CoreFor("duo");
        REQUIRE(core);
        CHECK(core->id == "second");
        CHECK(core->options_name == "Second Core");
        CHECK(core->netplay.subsystem == 257);
        REQUIRE(core->screens.size() == 1);
        CHECK(core->screens[0].frame_width == 256); // the core's own, not the console's
        host->SelectCore("duo", "first");
        core = host->CoreFor("duo");
        REQUIRE(core);
        CHECK(core->id == "first");
        // The hints come first and the forced options after them, so a forced one wins.
        CHECK(core->option_hints.front() == std::pair<std::string, std::string>{"h", "2"});
        CHECK(core->option_hints.back() == std::pair<std::string, std::string>{"f", "1"});
        CHECK(core->options_name == "first");
        REQUIRE(core->screens.size() == 1);
        CHECK(core->screens[0].frame_width == 320); // the console's
    }
    fs::remove_all(dir, ec);
}

TEST_CASE("the official manifests say what the app's tables said") {
    const std::vector<Manifest> all = Bundled();
    // The consoles whose pads have Nintendo's layout, and those without a stick.
    const std::set<std::string> nintendo = {"gb",  "gbc", "gba",      "nds", "nes",        "snes",
                                            "n64", "gamecube", "wii", "virtualboy", "pokemini"};
    const std::set<std::string> digital = {"gb", "gbc", "gba", "nds", "nes", "snes", "genesis", "sms",
                                           "gg", "pico8", "virtualboy", "psx"};
    std::set<std::string> media;
    for (const Manifest& manifest : all) {
        for (const SystemDef& system : manifest.systems) {
            const Console& console = system.console;
            INFO(console.id);
            CHECK(console.pad_layout == (nintendo.contains(console.id) ? "nintendo" : "other"));
            CHECK(console.digital_only == std::optional<bool>(digital.contains(console.id)));
            CHECK(console.sync == (console.id != "steam"));
            if (!console.media.shape.empty()) {
                media.insert(console.id);
            }
            if (!system.cores.empty()) {
                CHECK(std::find(system.cores.begin(), system.cores.end(), system.default_core) != system.cores.end());
            }
            if (console.id == "nds") {
                REQUIRE(console.screens.size() == 1);
                CHECK(console.screens[0].frame_width == 256);
                CHECK(console.screens[0].frame_height == 384);
                CHECK(console.screens[0].screens.at(1).touch);
                CHECK(system.default_core == "melonds");
            } else if (console.id == "snes") {
                CHECK(system.default_core == "snes9x");
            }
        }
    }
    CHECK(media.size() == all.size());

    const Component& gbads = CoreOf(all, "emuden.gba", "gbads");
    REQUIRE(gbads.screens.size() == 1);
    CHECK(gbads.screens[0].frame_width == 240);
    CHECK_FALSE(gbads.screens[0].screens.at(1).touch);
    CHECK(CoreOf(all, "emuden.nds", "melonds").option_hints.front().first == "melonds_screen_layout");
    CHECK(CoreOf(all, "emuden.nds", "desmume").option_hints.front().first == "desmume_screens_layout");
    for (const char* plugin : {"emuden.gb", "emuden.gbc"}) {
        const NetplaySpec& tgb = CoreOf(all, plugin, "tgbdual").netplay;
        CHECK(tgb.mode == "lockstep");
        CHECK(tgb.subsystem == 0x101);
        CHECK(tgb.host.save_memory == 0x100);
        CHECK(tgb.guest.save_memory == 0x300);
        CHECK(tgb.guest.options.size() == 3);
    }
    CHECK(CoreOf(all, "emuden.gba", "gpsp").netplay.mode == "link");
    CHECK(CoreOf(all, "emuden.nds", "melondsds").netplay.mode == "link");
    CHECK(CoreOf(all, "emuden.gamecube", "dolphin").options_name == "dolphin-emu");
}
