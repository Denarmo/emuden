// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

#include <catch2/catch_test_macros.hpp>

#include "core_options.h"
#include "den/base/files.h"
#include "interface_spec.h"
#include "page_widths.h"
#include "quick_spec.h"
#include "web_rules.h"

using namespace den;
using namespace den::app;
namespace fs = std::filesystem;

namespace {

bool Same(gfx::Color a, gfx::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

} // namespace

TEST_CASE("an interface plugin's look, with defaults for what is missing or out of range") {
    const fs::path dir = PathFromUtf8("/plugins/look");
    auto spec = ParseInterface(R"({
        "id": "emuden.look", "name": "Look",
        "interface": {
            "layout": "shelf3d",
            "colors": {"background": "#102030", "accent": "#ff000080", "text": "red"},
            "backdrop": {"art": "console", "tint": 4, "wallpaper": "art/wall.png"},
            "covers": {"height": 0.1, "spacing": 40},
            "tabs": {"recent": false},
            "launch": {"animation": "spin"},
            "consoles": {
                "gba": {"color": "#00ff00", "label": "A very long console label", "wallpaper": "../x.png",
                        "object": {"shape": "cartridge", "style": "snes", "grip": 20, "label": [0, 0, 0.5, 0.5]}},
                "group:nintendo": {"object": {"shape": "disc", "style": "unknown"}},
                "*": {"color": "#000000"}
            }
        }})",
                                     dir, {"snes", "n64"});
    REQUIRE(spec);
    CHECK(spec->id == "emuden.look");
    CHECK(spec->layout == "shelf3d");
    CHECK(Same(spec->background, {0x10, 0x20, 0x30, 255}));
    CHECK(Same(spec->accent, {255, 0, 0, 0x80}));
    CHECK(Same(spec->text, {255, 255, 255, 255})); // "red" is not a colour it reads
    CHECK(spec->backdrop == "console");
    CHECK(spec->tint == 1.0f);
    CHECK(spec->wallpaper == PathToUtf8(dir / PathFromUtf8("art/wall.png")));
    CHECK(spec->cover_height == 0.3f);
    CHECK(spec->spacing == 40);
    CHECK_FALSE(spec->tab_recent);
    CHECK(spec->tab_all);
    CHECK(spec->launch == "zoom");

    const ConsoleLook gba = spec->LookFor("gba", "nintendo");
    CHECK(Same(gba.color, {0, 255, 0, 255}));
    CHECK(gba.label.size() == 16);
    CHECK(gba.wallpaper.empty()); // outside the package
    CHECK(gba.object.shape == "cartridge");
    CHECK(gba.object.style == "snes");
    CHECK(gba.object.grip == 8);
    CHECK(gba.object.label.w == 0.5f);
    const ConsoleLook gb = spec->LookFor("gb", "nintendo");
    CHECK(gb.object.shape == "disc");
    CHECK(gb.object.style.empty());
    CHECK(Same(spec->LookFor("psx", "sony").color, {0, 0, 0, 255}));

    CHECK_FALSE(ParseInterface(R"({"interface": {"layout": "coverflow"}})", dir, {}));
    CHECK_FALSE(ParseInterface(R"({"id": "x"})", dir, {}));
    CHECK(KnownLayout("carousel"));
}

TEST_CASE("an interface plugin's quick settings layouts, each a choice") {
    auto choices = ParseQuickSettings(R"({
        "id": "emuden.quick-layouts", "name": "Quick Layouts",
        "quick_settings": {
            "icon": "icon.png",
            "layouts": [
                {"id": "big", "name": "Big tiles", "layout": "grid", "columns": 9, "tile_size": 3,
                 "colors": {"background": "#101820", "accent": "#ff8800", "text": "white"},
                 "actions": ["exit", "states", "nonsense", "exit"], "hidden": ["fps", "bogus"]},
                {"layout": "dock", "dock": 1, "cover": false},
                {"layout": "dock", "name": "Same id again"},
                {"layout": "hologram"},
                {"layout": "split", "id": "Bad Id!", "columns": 1}
            ]
        }})");
    REQUIRE(choices);
    REQUIRE(choices->size() == 3); // the second dock reuses its id, the hologram is unknown
    const QuickSpec& big = (*choices)[0];
    CHECK(big.plugin_id == "emuden.quick-layouts");
    CHECK(big.Key() == "emuden.quick-layouts/big");
    CHECK(big.name == "Big tiles");
    CHECK(big.layout == "grid");
    CHECK(big.columns == 6);
    CHECK(big.tile_size == 1.5f);
    REQUIRE(big.background);
    CHECK(Same(*big.background, {0x10, 0x18, 0x20, 255}));
    CHECK(big.accent);
    CHECK_FALSE(big.text);
    CHECK(big.order == std::vector<std::string>{"exit", "states"});
    CHECK(big.hidden == std::vector<std::string>{"fps"});
    const QuickSpec& dock = (*choices)[1];
    CHECK(dock.choice_id == "dock");
    CHECK(dock.name == "Dock");
    CHECK(dock.dock == 3);
    CHECK_FALSE(dock.cover);
    const QuickSpec& split = (*choices)[2];
    CHECK(split.choice_id == "split");
    CHECK(split.columns == 0);

    // One layout as the object itself.
    auto one = ParseQuickSettings(R"({"id": "jdoe.q", "quick_settings": {"layout": "compact"}})");
    REQUIRE(one);
    REQUIRE(one->size() == 1);
    CHECK(one->front().Key() == "jdoe.q/compact");
    CHECK_FALSE(ParseQuickSettings(R"({"id": "jdoe.q", "quick_settings": {"layout": "nope"}})"));
    CHECK_FALSE(ParseQuickSettings(R"({"id": "jdoe.q", "interface": {}})"));
    CHECK(QuickSpec{}.Key().empty());
}

TEST_CASE("quick actions in a layout's order, the plugin's first, those there now") {
    QuickSpec standard;
    const std::vector<std::string> everything = QuickActionIds();
    // EmuDEN's own: the card's controls, then today's sections.
    CHECK(ArrangeQuickActions(standard, everything) ==
          std::vector<std::string>{"pause", "rewind", "fast_forward", "exit", "states", "screen", "bfi",
                                   "controls", "cheats", "apps", "volume"});
    // Without a rewinding core and without anyone adding cheats.
    CHECK(ArrangeQuickActions(standard, {"exit", "pause", "volume", "states"}) ==
          std::vector<std::string>{"pause", "exit", "states", "volume"});

    QuickSpec grid;
    grid.layout = "grid";
    grid.order = {"states", "swap"};
    grid.hidden = {"fps", "pause"};
    const std::vector<std::string> arranged = ArrangeQuickActions(grid, everything);
    CHECK(arranged.front() == "states");
    CHECK(arranged[1] == "swap");
    CHECK(std::find(arranged.begin(), arranged.end(), "fps") == arranged.end());
    CHECK(std::find(arranged.begin(), arranged.end(), "pause") == arranged.end());
    CHECK(std::count(arranged.begin(), arranged.end(), "swap") == 1);
    // Every section stays reachable in every layout.
    for (const char* layout : {"standard", "grid", "compact", "dock", "split", "performance"}) {
        INFO(layout);
        CHECK(KnownQuickLayout(layout));
        QuickSpec spec;
        spec.layout = layout;
        const std::vector<std::string> ids = ArrangeQuickActions(spec, everything);
        for (const char* section : {"pause", "exit", "states", "screen", "bfi", "apps", "volume", "cheats"}) {
            CHECK(std::find(ids.begin(), ids.end(), section) != ids.end());
        }
        const bool controls = std::find(ids.begin(), ids.end(), "controls") != ids.end();
        const bool pages = std::find(ids.begin(), ids.end(), "options") != ids.end() &&
                           std::find(ids.begin(), ids.end(), "buttons") != ids.end();
        CHECK((controls || pages));
    }
    CHECK_FALSE(KnownQuickLayout("carousel"));
    CHECK(KnownQuickAction("cheats"));
    CHECK_FALSE(KnownQuickAction("teleport"));
}

TEST_CASE("noted core options round-trip and name their settings keys") {
    const std::vector<KnownOption> options = {
        {"mgba_skip_bios", "Skip BIOS", "OFF", {"OFF", "ON"}},
        {"mgba_frameskip", "Frameskip", "0", {"0", "1", "2"}},
    };
    const std::vector<KnownOption> again = ParseKnownOptions(WriteKnownOptions(options));
    REQUIRE(again.size() == 2);
    CHECK(again[0].key == "mgba_skip_bios");
    CHECK(again[0].label == "Skip BIOS");
    CHECK(again[0].fallback == "OFF");
    CHECK(again[1].values == std::vector<std::string>{"0", "1", "2"});
    // Options without a key or values are left out; a file that does not parse gives nothing.
    CHECK(ParseKnownOptions(R"([{"key": "a", "values": []}, {"values": ["x"]}])").empty());
    CHECK(ParseKnownOptions("not json").empty());

    CHECK(KnownOptionsFileName("emuden.core.mgba") == "emuden_core_mgba.json");
    CHECK(OptionKey("emuden.core.mgba", "mgba.skip-bios") == "core_option.emuden_core_mgba.mgba_skip_bios");
    CHECK(GlobalOptionKey("mGBA 0.10", "mgba.skip_bios") == "core_options.mgba010.mgba_skip_bios");
}

TEST_CASE("the browser takes patches and archives, never ROM images") {
    using namespace den::app::web;
    CHECK(ExtensionOf("Hack.V2.BPS") == "bps");
    CHECK(ExtensionOf("noextension").empty());
    CHECK(JudgeDownload("hack.bps", {}) == Verdict::Take);
    CHECK(JudgeDownload("readme.txt", {}) == Verdict::NotWanted);
    CHECK(JudgeDownload("game.gba", {}) == Verdict::RomImage);
    CHECK(JudgeDownload("game.gba", {"gba"}) == Verdict::RomImage);
    CHECK(JudgeDownload("game.gba", {ANY}) == Verdict::Take);
    CHECK(JudgeDownload("notes.txt", {"txt"}) == Verdict::Take);
    CHECK(JudgeDownload("hack.bps", {"zip"}) == Verdict::NotWanted);
    CHECK(RefusalText("game.gba", Verdict::RomImage).find("full game file") != std::string::npos);
    CHECK(RefusalText("hack.rar", Verdict::NotWanted).find("rar") != std::string::npos);

    CHECK(CanLeaveTo("https://www.romhacking.net/hacks/1"));
    CHECK_FALSE(CanLeaveTo("http://example.com/"));
    CHECK_FALSE(CanLeaveTo("javascript:alert(1)"));

    CHECK(SplitList(" bps, ZIP ,,7z") == std::vector<std::string>{"bps", "zip", "7z"});
    std::vector<std::string> extensions = {" BPS", "", "gba", "zip"};
    CleanExtensions(extensions);
    CHECK(extensions == std::vector<std::string>{"bps", "zip"});
}

TEST_CASE("downloads get a name that is free") {
    const fs::path dir = fs::temp_directory_path() / ("den_app_logic_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    CHECK(web::FreeFile(dir, "hack.bps") == dir / "hack.bps");
    std::ofstream(dir / "hack.bps") << "1";
    std::ofstream(dir / "hack (2).bps") << "2";
    CHECK(web::FreeFile(dir, "hack.bps") == dir / "hack (3).bps");
    fs::remove_all(dir);
}

TEST_CASE("page widths: the rail and Settings follow the room, not the device") {
    constexpr float MARGIN = 32;
    // 16:9 at the base size: the labelled rail and the sections beside the rows, as before.
    CHECK_FALSE(SlimRail(1280));
    CHECK(RailWidth(1280) == FULL_RAIL_WIDTH);
    CHECK(SettingsSideBySide(RailContentWidth(1280, MARGIN)));
    // 4:3 at the base size keeps the labelled rail; Settings goes to two levels.
    CHECK_FALSE(SlimRail(960));
    CHECK_FALSE(SettingsSideBySide(RailContentWidth(960, MARGIN)));
    // A 1240x1080 screen at 1.45 (570 units) and at 1.6 (517): the slim rail, two levels.
    for (const float page : {570.0f, 517.0f}) {
        CHECK(SlimRail(page));
        CHECK(RailWidth(page) == SLIM_RAIL_WIDTH);
        CHECK_FALSE(SettingsSideBySide(RailContentWidth(page, MARGIN)));
        CHECK(RailContentWidth(page, MARGIN) > 300);
    }
    CHECK_FALSE(SlimRail(0)); // not measured yet
}

TEST_CASE("page widths: an opened Settings section has the rail's room for its two levels") {
    constexpr float MARGIN = 32;
    // Without the rail, 4:3 at the base size has the sections beside the rows.
    CHECK(SettingsSideBySide(LevelContentWidth(1280, MARGIN)));
    CHECK(SettingsSideBySide(LevelContentWidth(960, MARGIN)));
    CHECK(LevelContentWidth(960, MARGIN) > RailContentWidth(960, MARGIN));
    // A near-square handheld keeps the rows alone.
    CHECK_FALSE(SettingsSideBySide(LevelContentWidth(570, MARGIN)));
}
