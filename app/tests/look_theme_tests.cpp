// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Several Home looks in one interface plugin, and plugin themes with their contrast.

#include <filesystem>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/json.h"
#include "interface_spec.h"
#include "theme_spec.h"

using namespace den;
using namespace den::app;
namespace fs = std::filesystem;

TEST_CASE("an interface plugin may hold several Home looks") {
    const fs::path dir = PathFromUtf8("/plugins/looks");
    auto looks = ParseInterfaces(R"({"id": "jdoe.looks", "name": "Looks", "interfaces": [
        {"id": "grid", "name": "Grid", "layout": "grid", "grid": {"columns": 5}},
        {"name": "List", "layout": "list"},
        {"id": "grid", "name": "Again", "layout": "hero"},
        {"id": "later", "layout": "coverflow"},
        "not a look"
    ]})",
                                 dir, {});
    REQUIRE(looks.ok());
    REQUIRE(looks->size() == 2);
    CHECK((*looks)[0].Key() == "jdoe.looks/grid");
    CHECK((*looks)[0].name == "Grid");
    CHECK((*looks)[0].grid.columns == 5);
    CHECK((*looks)[1].look_id == "list"); // no id: the layout's name
    CHECK((*looks)[1].Key() == "jdoe.looks/list");

    // The one "interface" object is one look, picked by the plugin's id alone.
    auto one = ParseInterfaces(R"({"id": "jdoe.one", "name": "One", "interface": {"layout": "wheel"}})", dir, {});
    REQUIRE(one.ok());
    REQUIRE(one->size() == 1);
    CHECK((*one)[0].Key() == "jdoe.one");
    CHECK((*one)[0].name == "One");

    CHECK_FALSE(ParseInterfaces(R"({"id": "jdoe.none", "interfaces": [{"layout": "coverflow"}]})", dir, {}));
    CHECK_FALSE(ParseInterfaces(R"({"id": "jdoe.none"})", dir, {}));
}

TEST_CASE("plugin themes: defaults, keys and unreadable ones left out") {
    auto themes = ParseThemes(R"({"id": "jdoe.themes", "themes": [
        {"id": "night", "name": "Night", "colors": {"page": "#000000", "fg": "#e0e0e0", "accent": "#ff8800"}},
        {"id": "faint", "name": "Faint", "colors": {"page": "#ffffff", "fg": "#cccccc"}},
        {"id": "dimmed", "name": "Dimmed", "colors": {"page": "#000000", "fg": "#ffffff", "text_dim": "#ffffff20"}},
        {"id": "night", "name": "Twice", "colors": {"page": "#000000", "fg": "#ffffff"}},
        {"id": "Bad Id", "name": "Bad", "colors": {"page": "#000000", "fg": "#ffffff"}},
        {"id": "nameless", "colors": {"page": "#000000", "fg": "#ffffff"}}
    ]})");
    REQUIRE(themes.ok());
    REQUIRE(themes->size() == 1);
    const ui::Theme& night = (*themes)[0];
    CHECK(night.key == "jdoe.themes/night");
    CHECK(night.name == "Night");
    CHECK(night.accent.r == 0xff);
    CHECK(night.accent.g == 0x88);
    CHECK(night.text_dim.a < 255);
    CHECK(night.text_body == ui::LightTheme().text_body);
    CHECK_FALSE(ParseThemes(R"({"id": "jdoe.themes", "themes": []})"));
}

TEST_CASE("contrast ratios") {
    CHECK(ui::ContrastRatio(gfx::Color::Rgb(0x000000), gfx::Color::Rgb(0xffffff)) > 20.9);
    CHECK(ui::ContrastRatio(gfx::Color::Rgb(0xffffff), gfx::Color::Rgb(0xffffff)) < 1.01);
    // Half-transparent black on white is mid grey.
    const double grey = ui::ContrastRatio(gfx::Color{0, 0, 0, 128}, gfx::Color::Rgb(0xffffff));
    CHECK(grey > 3.5);
    CHECK(grey < 4.5);
    CHECK(ReadableTheme(ui::LightTheme()));
    CHECK(ReadableTheme(ui::DarkTheme()));
}

#ifdef DEN_EMUDEN_PLUGINS_DIR
namespace {

std::optional<JsonValue> Manifest(const char* id) {
    const auto text = ReadTextFile(PathFromUtf8(DEN_EMUDEN_PLUGINS_DIR) / "plugins" / id / "plugin.json");
    return text ? ParseJson(*text) : std::nullopt;
}

} // namespace

TEST_CASE("every theme of emuden.themes is read and has readable text") {
    const auto text = ReadTextFile(PathFromUtf8(DEN_EMUDEN_PLUGINS_DIR) / "plugins/emuden.themes/plugin.json");
    REQUIRE(text);
    const std::optional<JsonValue> manifest = Manifest("emuden.themes");
    REQUIRE(manifest);
    const JsonValue* listed = manifest->Find("themes");
    REQUIRE(listed != nullptr);
    auto themes = ParseThemes(*text);
    REQUIRE(themes.ok());
    CHECK(themes->size() == listed->items.size()); // none left out
    CHECK(themes->size() >= 8);
    for (const ui::Theme& theme : *themes) {
        INFO(theme.key);
        CHECK(ui::ContrastRatio(theme.fg, theme.page) >= MIN_TEXT_CONTRAST);
        CHECK(ui::ContrastRatio(theme.text_dim, theme.page) >= MIN_TEXT_CONTRAST);
        CHECK(ui::ContrastRatio(theme.fg, theme.dialog) >= MIN_TEXT_CONTRAST); // dialogs and toasts
        CHECK(ui::ContrastRatio(theme.accent, theme.page) >= 3.0);            // the selection border
    }
}

TEST_CASE("emuden.home-layouts has the grid, list, wheel and hero looks") {
    const fs::path dir = PathFromUtf8(DEN_EMUDEN_PLUGINS_DIR) / "plugins/emuden.home-layouts";
    const auto text = ReadTextFile(dir / "plugin.json");
    REQUIRE(text);
    auto looks = ParseInterfaces(*text, dir, {});
    REQUIRE(looks.ok());
    REQUIRE(looks->size() == 4);
    const char* layouts[] = {"grid", "list", "wheel", "hero"};
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK((*looks)[i].layout == layouts[i]);
        CHECK((*looks)[i].Key() == std::string("emuden.home-layouts/") + layouts[i]);
    }
}

TEST_CASE("emuden.shelf resolves console moulds and shapes from plugin.json") {
    const fs::path dir = PathFromUtf8(DEN_EMUDEN_PLUGINS_DIR) / "plugins/emuden.shelf";
    const auto text = ReadTextFile(dir / "plugin.json");
    REQUIRE(text);
    auto spec = ParseInterface(*text, dir, MouldNames());
    INFO(spec.error());
    REQUIRE(spec.ok());
    CHECK(spec->layout == "shelf3d");
    CHECK(spec->LookFor("gbc", "nintendo").object.style == "gbc");
    const ObjectLook sw2 = spec->LookFor("switch2", "nintendo").object;
    CHECK(sw2.shape == "card");
    CHECK(sw2.style == "switch");
    CHECK(spec->LookFor("nes", "nintendo").object.style == "nes");
    CHECK(spec->LookFor("snes", "nintendo").object.style == "snes");
    CHECK(spec->LookFor("n64", "nintendo").object.style == "n64");
    CHECK(spec->LookFor("sms", "sega").object.style == "mastersystem");
    const ObjectLook ps2 = spec->LookFor("ps2", "sony").object;
    CHECK(ps2.shape == "case");
    CHECK(ps2.disc);
    const ObjectLook psp = spec->LookFor("psp", "sony").object;
    CHECK(psp.shape == "case");
    CHECK(psp.disc);
    CHECK(psp.umd);
}
#endif
