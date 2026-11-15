// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/library/steam.h"

using namespace den;
using namespace den::library::steam;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "emuden-steam-test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// A path as Steam writes it into a .vdf: backslashes escaped.
std::string VdfPath(const fs::path& path) {
    std::string out;
    for (const char c : PathToUtf8(path)) {
        out += c == '\\' ? std::string("\\\\") : std::string(1, c);
    }
    return out;
}

void Put(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    REQUIRE(WriteFileAtomic(file, text));
}

// shortcuts.vdf with one shortcut.
std::string ShortcutsFile(std::uint32_t appid, const std::string& name, const std::string& options) {
    std::string out;
    out += '\0';
    out += "shortcuts";
    out += '\0';
    out += '\0';
    out += "0";
    out += '\0';
    out += '\x02';
    out += "appid";
    out += '\0';
    out.append(reinterpret_cast<const char*>(&appid), 4);
    for (const auto& [key, value] : {std::pair<std::string, std::string>{"AppName", name}, {"Exe", "\"/x/EmuDEN.AppImage\""},
                                     {"LaunchOptions", options}}) {
        out += '\x01';
        out += key;
        out += '\0';
        out += value;
        out += '\0';
    }
    out += '\0';
    out += "tags";
    out += '\0';
    out += '\x08';
    out += '\x08';
    out += '\x08';
    out += '\x08';
    return out;
}

} // namespace

TEST_CASE("Steam's KeyValues text is read") {
    const auto vdf = ParseVdf("\"AppState\"\n{\n\t\"appid\"\t\t\"1145350\"\n\t\"name\"\t\t\"Hades \\\"II\\\"\"\n"
                              "\t// comment\n\t\"UserConfig\" { \"language\" \"english\" }\n}\n");
    REQUIRE(vdf);
    const Vdf* state = vdf->Find("appstate");
    REQUIRE(state != nullptr);
    CHECK(state->Value("AppID") == "1145350");
    CHECK(state->Value("name") == "Hades \"II\"");
    CHECK(state->Find("UserConfig")->Value("language") == "english");
    CHECK_FALSE(ParseVdf("\"a\" { \"b\" \"c\""));
}

TEST_CASE("an installation lists its games, shortcuts, tools and their settings") {
    const fs::path root = FreshDir("install");
    const fs::path other = FreshDir("other-library");
    Put(root / "steamapps" / "libraryfolders.vdf",
        "\"libraryfolders\" { \"0\" { \"path\" \"" + VdfPath(root) + "\" } \"1\" { \"path\" \"" +
            VdfPath(other) + "\" } }");
    Put(root / "steamapps" / "appmanifest_1145350.acf", "\"AppState\" { \"appid\" \"1145350\" \"name\" \"Hades II\" }");
    Put(root / "steamapps" / "appmanifest_4183110.acf", "\"AppState\" { \"appid\" \"4183110\" \"name\" \"Proton 11.0\" }");
    Put(other / "steamapps" / "appmanifest_389140.acf", "\"AppState\" { \"appid\" \"389140\" \"name\" \"Horizon Chase Turbo\" }");
    Put(root / "compatibilitytools.d" / "GE-Proton9-20" / "compatibilitytool.vdf",
        "\"compatibilitytools\" { \"compat_tools\" { \"GE-Proton9-20\" { \"display_name\" \"GE-Proton9-20\" } } }");
    Put(root / "userdata" / "125201501" / "config" / "localconfig.vdf",
        "\"UserLocalConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { \"apps\" { \"1145350\" { \"LaunchOptions\" "
        "\"gamemoderun %command%\" } } } } } }");
    Put(root / "userdata" / "125201501" / "config" / "shortcuts.vdf",
        ShortcutsFile(4105511571u, "EmuDEN", "EMUDEN_AUTOMATION=5577 %command%"));
    Put(root / "config" / "config.vdf",
        "\"InstallConfigStore\" { \"Software\" { \"Valve\" { \"Steam\" { \"CompatToolMapping\" { \"1145350\" { "
        "\"name\" \"proton_11\" \"config\" \"\" \"priority\" \"250\" } } } } } }");

    const Install install(root);
    const std::vector<App> apps = install.Apps();
    REQUIRE(apps.size() == 4);
    CHECK(apps[0].name == "EmuDEN");
    CHECK(apps[0].shortcut);
    CHECK(apps[0].appid == "4105511571");
    CHECK(apps[1].name == "Hades II");
    CHECK(apps[2].name == "Horizon Chase Turbo");
    CHECK(apps[3].tool);

    const std::vector<Tool> tools = install.Tools();
    REQUIRE(tools.size() == 2);
    CHECK(tools[0].name == "proton_11");
    CHECK(tools[1].name == "GE-Proton9-20");

    const AppConfig hades = install.ConfigFromFiles("1145350");
    CHECK(hades.launch_options == "gamemoderun %command%");
    CHECK(hades.compat_tool == "proton_11");
    CHECK(hades.compat_tool_chosen);
    CHECK(install.ConfigFromFiles("4105511571").launch_options == "EMUDEN_AUTOMATION=5577 %command%");
}

TEST_CASE("official Proton names map to Steam's tool names") {
    CHECK(ProtonToolName("Proton 9.0") == "proton_9");
    CHECK(ProtonToolName("Proton 9.0-4") == "proton_9");
    CHECK(ProtonToolName("Proton 6.3") == "proton_63");
    CHECK(ProtonToolName("Proton Experimental") == "proton_experimental");
    CHECK(ProtonToolName("Proton Hotfix") == "proton_hotfix");
    CHECK(ProtonToolName("Steam Linux Runtime 3.0").empty());
}

TEST_CASE("only launch options without shell syntax are allowed") {
    CHECK(CheckLaunchOptions("").ok);
    CHECK(CheckLaunchOptions("-dx11 -nointro").ok);
    CHECK(CheckLaunchOptions("PROTON_USE_WINED3D=1 DXVK_ASYNC=1 %command%").ok);
    CHECK(CheckLaunchOptions("gamescope -w 1280 -h 800 -f -- %command% -windowed").ok);
    CHECK(CheckLaunchOptions("WINEDLLOVERRIDES=dinput8=n,b mangohud %command%").ok);
    CHECK_FALSE(CheckLaunchOptions("rm -rf ~; %command%").ok);
    CHECK_FALSE(CheckLaunchOptions("$(curl evil) %command%").ok);
    CHECK_FALSE(CheckLaunchOptions("LD_PRELOAD=/tmp/x.so %command%").ok);
    CHECK_FALSE(CheckLaunchOptions("/home/deck/x.sh %command%").ok);
    CHECK_FALSE(CheckLaunchOptions("gamescope -- /home/deck/x.sh %command%").ok);
    CHECK_FALSE(CheckLaunchOptions("%command% %command%").ok);
    CHECK_FALSE(CheckLaunchOptions("bash %command%").ok);
    CHECK_FALSE(CheckLaunchOptions("A=1\n%command%").ok);
}

TEST_CASE("the client sends fixed scripts with the values quoted") {
    std::vector<std::string> scripts;
    Client client([&](const std::string& script) -> den::Result<std::string> {
        scripts.push_back(script);
        if (script.find("RegisterForAppDetails") != std::string::npos) {
            return std::string(R"({"launch":"-dx11","tool":"proton_11","display":"Proton 11.0-2","priority":85})");
        }
        return std::string("ok");
    });
    const auto config = client.Config("1145350");
    REQUIRE(config);
    CHECK(config.value().compat_tool_display == "Proton 11.0-2");
    CHECK_FALSE(config.value().compat_tool_chosen); // Steam's own choice for the game
    CHECK(client.Set("1145350", std::string("DXVK_HUD=fps %command%"), std::string("proton_9")).ok);
    CHECK(scripts.back().find("SetAppLaunchOptions(1145350, \"DXVK_HUD=fps %command%\")") != std::string::npos);
    CHECK(scripts.back().find("SpecifyCompatTool(1145350, \"proton_9\")") != std::string::npos);
    CHECK(client.Set("4105511571", std::string("-x"), std::nullopt).ok);
    CHECK(scripts.back().find("SetShortcutLaunchOptions(4105511571") != std::string::npos);
    CHECK_FALSE(client.Set("1145350); alert(1", std::string("-x"), std::nullopt).ok);
    CHECK_FALSE(client.Set("1", std::nullopt, std::string("x\"); evil(\"")).ok);
    CHECK(JsString("a\xE2\x80\xA8z") == "\"a\\u2028z\"");
}
