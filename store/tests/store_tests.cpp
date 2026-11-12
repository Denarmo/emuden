// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "den/store/settings.h"

using den::store::Settings;

TEST_CASE("settings survive a reload") {
    const auto file = std::filesystem::temp_directory_path() / "emuden-store-test" / "settings.json";
    std::filesystem::remove_all(file.parent_path());
    {
        Settings settings(file);
        CHECK(settings.GetString("ui.theme", "Light") == "Light");
        settings.SetString("ui.theme", "Dark");
        settings.SetBool("window.fullscreen", true);
        settings.SetNumber("audio.volume", 0.5);
        settings.SetList("library.folders", {"D:/ROMS/GB", "D:/ROMS/GBA"});
    }
    Settings again(file);
    CHECK(again.GetString("ui.theme") == "Dark");
    CHECK(again.GetBool("window.fullscreen"));
    CHECK(again.GetNumber("audio.volume") == 0.5);
    CHECK(again.GetList("library.folders").size() == 2);
    again.Remove("ui.theme");
    CHECK_FALSE(again.Has("ui.theme"));
    std::filesystem::remove_all(file.parent_path());
}
