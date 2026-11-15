// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.device.info, den.game and den.steam: what a plugin sees and that nothing changes
// before the user pressed something and accepted the app's own question.

#include <catch2/catch_test_macros.hpp>

#include "feature/runner.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::feature;
using namespace den::plugins::testing;

namespace {

using ScriptRunner = den::plugins::feature::Runner;

struct FakeTune final : TuneServices {
    std::map<std::string, std::string> own; // the game's own options
    std::vector<std::string> steam_sets;

    JsonValue DeviceFacts() override {
        return JsonObject({{"name", JsonString("Legion Go S")}, {"steamos", JsonBool(true)}});
    }
    std::optional<TuneGame> Game(const std::string& id) override {
        if (id != "gt4" && id != "snes1") {
            return std::nullopt;
        }
        TuneGame game{id, id == "gt4" ? "Gran Turismo 4" : "Mario", id == "gt4" ? "ps2" : "snes", "play", "Play!", true, {}};
        const auto value = own.find("play_res_multi");
        game.options.push_back({"play_res_multi", "Resolution", value != own.end() ? value->second : "1x",
                                {"1x", "2x", "4x"}, value != own.end()});
        return game;
    }
    Result<JsonValue> SetGameOptions(const std::string&, const JsonValue& values) override {
        JsonValue before = JsonObject();
        for (const auto& [key, value] : values.members) {
            const auto had = own.find(key);
            before.members.emplace_back(key, had != own.end() ? JsonString(had->second) : JsonValue());
            if (value.kind == JsonValue::Kind::Null) {
                own.erase(key);
            } else {
                own[key] = value.string;
            }
        }
        return before;
    }
    SteamState Steam() override {
        return {true, true, ""};
    }
    std::vector<SteamApp> SteamApps() override {
        return {{"1145350", "Hades II", false}};
    }
    std::vector<SteamTool> SteamTools() override {
        return {{"proton_11", "Proton 11.0"}};
    }
    Result<SteamConfig> SteamAppConfig(const std::string&) override {
        return SteamConfig{"", "proton_11", "Proton 11.0-2"};
    }
    Status SetSteamApp(const std::string& appid, const std::optional<std::string>& launch,
                       const std::optional<std::string>& tool) override {
        steam_sets.push_back(appid + "|" + launch.value_or("-") + "|" + tool.value_or("-"));
        return {};
    }
};

const char* const SCRIPT = R"(
    local plugin = { actions = {} }
    function plugin.open()
      local info, why = den.device.info()
      den.log("info " .. tostring(info and info.name) .. " " .. tostring(why))
      local set, why2 = den.game.set("gt4", { play_res_multi = "2x" })
      den.log("open set " .. tostring(set) .. " " .. tostring(why2))
    end
    function plugin.actions.look(id)
      local game, why = den.game.settings(id)
      if not game then return den.log("look " .. why) end
      den.log("look " .. game.core .. " " .. game.options[1].key .. "=" .. game.options[1].value)
    end
    function plugin.actions.apply(value)
      local before, why = den.game.set("gt4", { play_res_multi = value })
      if not before then return den.log("apply " .. why) end
      den.log("apply before=" .. tostring(before.play_res_multi == den.json.null and "null" or before.play_res_multi))
    end
    function plugin.actions.steam(launch)
      local status = den.steam.status()
      den.log("steam " .. status.apps[1].name .. " " .. status.tools[1].name)
      local config = den.steam.config("1145350")
      den.log("tool " .. config.compat_tool)
      local ok, why = den.steam.set{ appid = "1145350", name = "Hades II", launch_options = launch, compat_tool = "proton_9" }
      den.log("set " .. tostring(ok) .. " " .. tostring(why))
    end
    return plugin
)";

struct Stage {
    ScratchDir scratch;
    std::shared_ptr<FakeTune> tune = std::make_shared<FakeTune>();

    std::unique_ptr<ScriptRunner> Run(const std::string& permissions, const std::string& systems = R"(["ps2"])") {
        scratch.Write("package/main.lua", SCRIPT);
        RunnerConfig config;
        config.manifest = ParseFeatureManifest(
                              R"({"id":"test.tune","name":"Tune","version":"1.0.0","kind":"feature","api":1,
              "needs":["kind.feature","feature.lua.1","feature.tune.1"],"feature":{"entry":"main.lua",
              "permissions":)" + permissions + R"(,"systems":)" + systems + "}}")
                              .take();
        config.package_dir = scratch.dir / "package";
        config.data_dir = scratch.dir / "data";
        config.app_version = "1.2.3";
        config.platform = "test";
        config.tune = tune;
        config.now = [] { return std::int64_t{1794528000}; };
        auto runner = std::make_unique<ScriptRunner>(std::move(config));
        runner->Settle();
        return runner;
    }
};

bool Logged(const ScriptRunner& runner, const std::string& line) {
    const auto log = runner.Log();
    return std::find(log.begin(), log.end(), line) != log.end();
}

// Answers every question of the app with `yes`; returns the questions.
std::vector<Ask> AnswerAll(ScriptRunner& runner, bool yes) {
    std::vector<Ask> asked;
    for (int i = 0; i < 4; ++i) {
        runner.Settle();
        for (Ask& ask : runner.TakeAsks()) {
            if (ask.kind == AskKind::Confirm) {
                runner.Answer(ask.id, yes, {});
                asked.push_back(ask);
            }
        }
    }
    runner.Settle();
    return asked;
}

} // namespace

TEST_CASE("den.device.info and den.game need their permissions and systems") {
    Stage stage;
    auto none = stage.Run(R"(["library.read"])");
    none->Call({"open"}, {});
    none->Settle();
    CHECK(Logged(*none, "info nil the plugin has no permission to see the device"));

    auto runner = stage.Run(R"(["device.info","game.settings"])");
    runner->Call({"open"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "info Legion Go S nil"));
    // Opening the plugin is not the user asking for a change.
    CHECK(Logged(*runner, "open set nil settings are changed only when the user asks for it"));
    runner->Call({"actions", "look"}, {JsonString("gt4")});
    runner->Settle();
    CHECK(Logged(*runner, "look play play_res_multi=1x"));
    runner->Call({"actions", "look"}, {JsonString("snes1")});
    runner->Settle();
    CHECK(Logged(*runner, "look the plugin did not declare the console snes"));
    CHECK_FALSE(ParseFeatureManifest(R"({"id":"t.t","name":"T","version":"1.0.0","kind":"feature","api":1,
        "needs":["kind.feature","feature.lua.1"],"feature":{"entry":"main.lua","permissions":["game.settings"]}})")
                    .ok());
}

TEST_CASE("den.game.set changes a game only after the app's question is accepted") {
    Stage stage;
    auto runner = stage.Run(R"(["game.settings"])");
    runner->Call({"actions", "apply"}, {JsonString("2x")});
    const std::vector<Ask> refused = AnswerAll(*runner, false);
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].title == "Change the settings of Gran Turismo 4?");
    CHECK(refused[0].message.find("Resolution: 1x -> 2x") != std::string::npos);
    CHECK(Logged(*runner, "apply cancelled"));
    CHECK(stage.tune->own.empty());

    runner->Call({"actions", "apply"}, {JsonString("2x")});
    AnswerAll(*runner, true);
    CHECK(Logged(*runner, "apply before=null"));
    CHECK(stage.tune->own["play_res_multi"] == "2x");
}

TEST_CASE("den.steam lists apps and sets only safe launch options after the question") {
    Stage stage;
    auto runner = stage.Run(R"(["steam"])", "[]");
    runner->Call({"actions", "steam"}, {JsonString("PROTON_USE_WINED3D=1 gamemoderun %command% -dx11")});
    const std::vector<Ask> asked = AnswerAll(*runner, true);
    CHECK(Logged(*runner, "steam Hades II proton_11"));
    CHECK(Logged(*runner, "tool proton_11"));
    REQUIRE(asked.size() == 1);
    CHECK(asked[0].title == "Change Hades II in Steam?");
    CHECK(Logged(*runner, "set true nil"));
    REQUIRE(stage.tune->steam_sets.size() == 1);
    CHECK(stage.tune->steam_sets[0] == "1145350|PROTON_USE_WINED3D=1 gamemoderun %command% -dx11|proton_9");

    runner->Call({"actions", "steam"}, {JsonString("rm -rf ~; %command%")});
    CHECK(AnswerAll(*runner, true).empty()); // refused before anything is asked
    CHECK(Logged(*runner, "set nil launch options may not contain \"~\""));
    CHECK(stage.tune->steam_sets.size() == 1);
}
