// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.achievements: the permission, sign-in only after the user pressed something, the token
// kept in the plugin's secrets and never the password, settings and a game's progress.

#include <catch2/catch_test_macros.hpp>

#include "den/platform/pal.h"
#include "feature/runner.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::feature;
using namespace den::plugins::testing;

namespace {

using ScriptRunner = den::plugins::feature::Runner;

struct Secrets final : pal::Secrets {
    std::map<std::string, std::string> values;
    std::optional<std::string> Get(const std::string& key) override {
        const auto it = values.find(key);
        return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    Status Set(const std::string& key, const std::string& value) override {
        values[key] = value;
        return {};
    }
    void Remove(const std::string& key) override {
        values.erase(key);
    }
};

struct FakeAchievements final : AchievementServices {
    std::optional<AchievementUser> user;
    AchievementOptions options;
    std::string last_password;
    Result<std::string> SignIn(const std::string& name, const std::string& password) override {
        last_password = password;
        if (password != "right") {
            return Status::Fail("The user name or password is not right.");
        }
        user = AchievementUser{name, name, "", 100, 5, 0};
        return std::string("TOKEN123");
    }
    void SignOut() override {
        user.reset();
    }
    std::optional<AchievementUser> User() override {
        return user;
    }
    bool SigningIn() override {
        return false;
    }
    AchievementOptions Options() override {
        return options;
    }
    void SetOptions(const AchievementOptions& next) override {
        options = next;
    }
    Result<AchievementGame> Game(const std::string& id) override {
        AchievementGame game;
        game.library_id = id;
        game.title = "Tetris";
        game.id = 508;
        game.total = 2;
        game.unlocked = 1;
        game.achievements.push_back({1, 5, "Lines", "Clear 10 lines", "", true, "", 0, 50});
        game.achievements.push_back({2, 10, "Tetris", "Four at once", "", false, "1/4", 25, 10});
        return game;
    }
    std::string Playing() override {
        return {};
    }
};

const char* const SCRIPT = R"(
    local plugin = { actions = {} }
    function plugin.open()
      local ok, why = den.achievements.sign_in{ user = "Tester", password = "right" }
      den.log("open " .. tostring(ok) .. " " .. tostring(why))
    end
    function plugin.actions.sign_in(arg, password)
      local ok, why = den.achievements.sign_in{ user = "Tester", password = password }
      den.log("sign_in " .. tostring(ok) .. " " .. tostring(why))
      local s = den.achievements.status()
      den.log("status " .. tostring(s.signed_in) .. " " .. tostring(s.user and s.user.score))
    end
    function plugin.actions.hardcore()
      den.achievements.set{ hardcore = true, sound = false }
      local s = den.achievements.status()
      den.log("options " .. tostring(s.hardcore) .. " " .. tostring(s.sound) .. " " .. tostring(s.screenshot))
    end
    function plugin.actions.game(id)
      local g = den.achievements.game(id)
      den.log("game " .. g.title .. " " .. g.unlocked .. "/" .. g.total .. " " .. g.achievements[2].progress)
    end
    function plugin.actions.sign_out()
      den.achievements.sign_out()
      den.log("out " .. tostring(den.achievements.status().signed_in))
    end
    return plugin
)";

struct Stage {
    ScratchDir scratch;
    Secrets secrets;
    std::shared_ptr<FakeAchievements> fake = std::make_shared<FakeAchievements>();

    std::unique_ptr<ScriptRunner> Run(const std::string& permissions) {
        scratch.Write("package/main.lua", SCRIPT);
        RunnerConfig config;
        config.manifest = ParseFeatureManifest(
                              R"({"id":"emuden.retroachievements","name":"RetroAchievements","version":"1.0.0",
              "kind":"feature","api":1,"needs":["kind.feature","feature.lua.1","feature.achievements.1"],
              "feature":{"entry":"main.lua","permissions":)" + permissions + "}}")
                              .take();
        config.package_dir = scratch.dir / "package";
        config.data_dir = scratch.dir / "data";
        config.app_version = "1.2.3";
        config.platform = "test";
        config.achievements = fake;
        config.secrets = &secrets;
        auto runner = std::make_unique<ScriptRunner>(std::move(config));
        runner->Settle();
        return runner;
    }
};

bool Logged(const ScriptRunner& runner, const std::string& line) {
    const auto log = runner.Log();
    return std::find(log.begin(), log.end(), line) != log.end();
}

} // namespace

TEST_CASE("den.achievements needs its permission") {
    Stage stage;
    auto runner = stage.Run(R"(["files.import"])");
    runner->Call({"actions", "sign_in"}, {JsonValue(), JsonString("right")});
    runner->Settle();
    CHECK(Logged(*runner, "sign_in nil the plugin has no permission for achievements"));
}

TEST_CASE("sign-in keeps the token, not the password, and only when the user asked") {
    Stage stage;
    auto runner = stage.Run(R"(["achievements"])");
    runner->Call({"open"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "open nil sign-in starts only when the user asks for it"));
    CHECK(stage.fake->last_password.empty());

    runner->Call({"actions", "sign_in"}, {JsonValue(), JsonString("wrong")});
    runner->Settle();
    CHECK(Logged(*runner, "sign_in nil The user name or password is not right."));
    CHECK(stage.secrets.values.empty());

    runner->Call({"actions", "sign_in"}, {JsonValue(), JsonString("right")});
    runner->Settle();
    CHECK(Logged(*runner, "sign_in true nil"));
    CHECK(Logged(*runner, "status true 100"));
    const std::string name = SecretName(ACHIEVEMENTS_PLUGIN, ACHIEVEMENTS_LOGIN_SECRET);
    REQUIRE(stage.secrets.values.contains(name));
    CHECK(stage.secrets.values[name].find("TOKEN123") != std::string::npos);
    CHECK(stage.secrets.values[name].find("right") == std::string::npos);
    CHECK(stage.secrets.values.size() == 1);

    runner->Call({"actions", "sign_out"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "out false"));
    CHECK(stage.secrets.values.empty());
}

TEST_CASE("den.achievements sets the options and shows a game's progress") {
    Stage stage;
    auto runner = stage.Run(R"(["achievements"])");
    runner->Call({"actions", "hardcore"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "options true false false"));
    CHECK(stage.fake->options.hardcore);
    runner->Call({"actions", "game"}, {JsonString("tetris-1")});
    runner->Settle();
    CHECK(Logged(*runner, "game Tetris 1/2 1/4"));
}
