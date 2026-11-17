// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.mods from a script: what it returns, that it needs game.files, and that no file of a
// game changes before the user pressed something and accepted the app's own question.

#include <catch2/catch_test_macros.hpp>

#include "feature/runner.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::feature;
using namespace den::plugins::testing;

namespace {

using ScriptRunner = den::plugins::feature::Runner;

struct FakeMods final : ModServices {
    fs::path game_dir, layer_dir;

    Result<ModGame> SteamGame(const std::string& appid) override {
        if (appid != "1145350") {
            return Status::Fail("that Steam game is not installed on this device");
        }
        return ModGame{appid, "Hades II", game_dir};
    }
    fs::path LayerManifestDir() override {
        return layer_dir;
    }
};

const char* const SCRIPT = R"(
    local plugin = { actions = {} }
    local FILES = { { from = "cache/mods/helix/helix/hlx_fsr4.dll", to = "bin/amd_fidelityfx_upscaler_dx12.dll",
                      keep_as = "amd_fidelityfx_upscaler_dx12.amd.dll" },
                    { from = "downloads/notes.txt", to = "helix/notes.txt" } }
    function plugin.open()
      local ok, why = den.mods.apply{ appid = "1145350", name = "Helix SR", files = FILES }
      den.log("open apply " .. tostring(ok) .. " " .. tostring(why))
      local ok2, why2 = den.mods.remove("1145350")
      den.log("open remove " .. tostring(ok2) .. " " .. tostring(why2))
    end
    function plugin.actions.find(appid)
      local files, why = den.mods.find{ appid = appid, names = { "AMD_FIDELITYFX_*.dll", "nothing.dll" } }
      if not files then return den.log("find " .. why) end
      den.log("find " .. #files .. " " .. files[1].path .. " " .. files[1].size .. " " .. files[1].sha256)
    end
    function plugin.actions.unpack(entry)
      local paths, why = den.mods.unpack{ file = "downloads/helix.zip", entries = { entry } }
      den.log("unpack " .. tostring(paths and paths[entry]) .. " " .. tostring(why))
    end
    function plugin.actions.apply(to)
      if to then FILES[2].to = to end
      local ok, why = den.mods.apply{ appid = "1145350", name = "Helix SR", files = FILES }
      den.log("apply " .. tostring(ok) .. " " .. tostring(why))
    end
    function plugin.actions.from(path)
      local ok, why = den.mods.apply{ appid = "1145350", name = "Helix SR", files = { { from = path, to = "x.dll" } } }
      den.log("from " .. tostring(ok) .. " " .. tostring(why))
    end
    function plugin.actions.state()
      local state, why = den.mods.state("1145350")
      if not state then return den.log("state nil " .. tostring(why)) end
      den.log("state " .. state.name .. " " .. state.time .. " " .. #state.files .. " " .. state.files[1].to ..
              " " .. tostring(state.files[1].intact) .. " " .. tostring(state.files[2].intact))
    end
    function plugin.actions.remove()
      local ok, left = den.mods.remove("1145350")
      den.log("remove " .. tostring(ok) .. " " .. (type(left) == "table" and #left or tostring(left)))
    end
    function plugin.actions.layer(manifest)
      local ok, why = den.mods.layer{ name = "lsfg", layers = { { manifest = manifest, library = "cache/liblsfg-vk.so" } } }
      den.log("layer " .. tostring(ok) .. " " .. tostring(why))
    end
    function plugin.actions.layer_state()
      local state = den.mods.layer_state("lsfg")
      den.log("layer_state " .. (state and (state.time .. " " .. state.manifests[1] .. " " .. tostring(state.intact)) or "nil"))
    end
    function plugin.actions.remove_layer()
      local ok, why = den.mods.remove_layer("lsfg")
      den.log("remove_layer " .. tostring(ok) .. " " .. tostring(why))
    end
    return plugin
)";

const char* const LAYER = R"({"file_format_version":"1.0.0","layer":{"name":"VK_LAYER_LS_frame_generation",
  "type":"GLOBAL","library_path":"liblsfg-vk.so","disable_environment":{"DISABLE_LSFGVK":"1"}}})";

struct Stage {
    ScratchDir scratch;
    std::shared_ptr<FakeMods> mods = std::make_shared<FakeMods>();
    fs::path game = scratch.dir / "common" / "Hades II";

    Stage() {
        mods->game_dir = game;
        mods->layer_dir = scratch.dir / "implicit_layer.d";
        scratch.Write("common/Hades II/bin/amd_fidelityfx_upscaler_dx12.dll", "ORIGINAL-FSR");
        scratch.Write("data/downloads/helix.zip", StoredZipOf({{"helix/hlx_fsr4.dll", "HELIX-FSR"}}));
        scratch.Write("data/downloads/notes.txt", "NOTES");
        scratch.Write("data/cache/lsfg_vk.json", LAYER);
        scratch.Write("data/cache/bad.json", "{\"file_format_version\":\"1.0.0\",\"layer\":{\"name\":\"x\","
                                             "\"type\":\"GLOBAL\",\"library_path\":\"x.so\"}}");
        scratch.Write("data/cache/liblsfg-vk.so", "LAYER-LIBRARY");
        scratch.Write("data/secret.txt", "not for games");
    }

    std::unique_ptr<ScriptRunner> Run(const std::string& permissions, const std::string& platform = "linux") {
        scratch.Write("package/main.lua", SCRIPT);
        RunnerConfig config;
        config.manifest = ParseFeatureManifest(
                              R"({"id":"test.mods","name":"Mods","version":"1.0.0","kind":"feature","api":1,
              "needs":["kind.feature","feature.lua.1","feature.mods.1"],"feature":{"entry":"main.lua",
              "permissions":)" + permissions + "}}")
                              .take();
        config.package_dir = scratch.dir / "package";
        config.data_dir = scratch.dir / "data";
        config.mods_dir = scratch.dir / "mods" / "test.mods";
        config.app_version = "1.2.3";
        config.platform = platform;
        config.mods = mods;
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

std::string LastLog(const ScriptRunner& runner) {
    const auto log = runner.Log();
    return log.empty() ? std::string() : log.back();
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

std::string Slurp(const fs::path& file) {
    return ReadTextFile(file).value_or("<missing>");
}

} // namespace

TEST_CASE("den.mods needs game.files, its capability and an official plugin") {
    const std::string feature = R"("feature":{"entry":"main.lua","permissions":["game.files"]}})";
    CHECK_FALSE(ParseFeatureManifest(R"({"id":"emuden.m","name":"T","version":"1.0.0","kind":"feature","api":1,
        "needs":["kind.feature","feature.lua.1"],)" + feature).ok());
    const auto manifest = ParseFeatureManifest(R"({"id":"emuden.m","name":"T","version":"1.0.0","kind":"feature","api":1,
        "needs":["kind.feature","feature.lua.1","feature.mods.1"],)" + feature);
    REQUIRE(manifest.ok());
    CHECK(CheckTrust(manifest.value(), FeaturePlugin::Trust::Official, {}).ok);
    CHECK_FALSE(CheckTrust(manifest.value(), FeaturePlugin::Trust::ThirdParty, "emuden").ok);
    CHECK_FALSE(CheckTrust(manifest.value(), FeaturePlugin::Trust::Developer, {}).ok);
    REQUIRE(PermissionLines(manifest.value().access).size() == 1);

    Stage stage;
    auto none = stage.Run("[]");
    none->Call({"actions", "find"}, {JsonString("1145350")});
    none->Call({"actions", "state"}, {});
    none->Settle();
    CHECK(Logged(*none, "find the plugin has no permission to change game files"));
    CHECK(Logged(*none, "state nil the plugin has no permission to change game files"));
}

TEST_CASE("den.mods.find and unpack give what the plugin applies") {
    Stage stage;
    auto runner = stage.Run(R"(["game.files"])");
    runner->Call({"actions", "find"}, {JsonString("1145350")});
    runner->Settle();
    CHECK(Logged(*runner, "find 1 bin/amd_fidelityfx_upscaler_dx12.dll 12 " + HashOf("ORIGINAL-FSR")));
    runner->Call({"actions", "find"}, {JsonString("620")});
    runner->Settle();
    CHECK(Logged(*runner, "find that Steam game is not installed on this device"));

    runner->Call({"actions", "unpack"}, {JsonString("helix/hlx_fsr4.dll")});
    runner->Settle();
    CHECK(Logged(*runner, "unpack cache/mods/helix/helix/hlx_fsr4.dll nil"));
    CHECK(Slurp(stage.scratch.dir / "data" / "cache" / "mods" / "helix" / "helix" / "hlx_fsr4.dll") == "HELIX-FSR");
    runner->Call({"actions", "unpack"}, {JsonString("../hlx_fsr4.dll")});
    runner->Settle();
    CHECK(Logged(*runner, "unpack nil ../hlx_fsr4.dll is not a path inside the archive"));
    runner->Call({"actions", "unpack"}, {JsonString("/etc/passwd")});
    runner->Settle();
    CHECK(Logged(*runner, "unpack nil /etc/passwd is not a path inside the archive"));
}

TEST_CASE("den.mods.apply changes a game only after the app's question is accepted") {
    Stage stage;
    auto runner = stage.Run(R"(["game.files"])");
    const fs::path library = stage.game / "bin" / "amd_fidelityfx_upscaler_dx12.dll";
    runner->Call({"actions", "unpack"}, {JsonString("helix/hlx_fsr4.dll")});
    // Opening the plugin is not the user asking for a change.
    runner->Call({"open"}, {});
    CHECK(AnswerAll(*runner, true).empty());
    CHECK(Logged(*runner, "open apply nil games are changed only when the user asks for it"));
    CHECK(Logged(*runner, "open remove nil games are changed only when the user asks for it"));

    runner->Call({"actions", "apply"}, {});
    const std::vector<Ask> refused = AnswerAll(*runner, false);
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].title == "Set up Helix SR for Hades II?");
    CHECK(refused[0].message.find("Replaces: bin/amd_fidelityfx_upscaler_dx12.dll (the game's file is kept as "
                                  "bin/amd_fidelityfx_upscaler_dx12.amd.dll)\nNew: helix/notes.txt\n") == 0);
    CHECK(refused[0].message.find("Asked by Mods.") != std::string::npos);
    CHECK(Logged(*runner, "apply nil cancelled"));
    CHECK(Slurp(library) == "ORIGINAL-FSR");
    runner->Call({"actions", "state"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "state nil nil"));

    runner->Call({"actions", "apply"}, {});
    CHECK(AnswerAll(*runner, true).size() == 1);
    CHECK(Logged(*runner, "apply true nil"));
    CHECK(Slurp(library) == "HELIX-FSR");
    CHECK(Slurp(stage.game / "bin" / "amd_fidelityfx_upscaler_dx12.amd.dll") == "ORIGINAL-FSR");
    CHECK(Slurp(stage.game / "helix" / "notes.txt") == "NOTES");
    runner->Call({"actions", "state"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "state Helix SR 1794528000 2 bin/amd_fidelityfx_upscaler_dx12.dll true true"));

    // Refused before anything is asked: a path out of the game, a file that is not the plugin's to give.
    for (const char* to : {"../outside.txt", "/tmp/outside.txt", "bin\\..\\..\\outside.txt"}) {
        runner->Call({"actions", "apply"}, {JsonString(to)});
        CHECK(AnswerAll(*runner, true).empty());
        CHECK(LastLog(*runner) == std::string("apply nil ") + to + " is not a path inside the game's folder");
    }
    runner->Call({"actions", "from"}, {JsonString("secret.txt")});
    CHECK(AnswerAll(*runner, true).empty());
    CHECK(LastLog(*runner) == "from nil secret.txt is not under cache/ or downloads/");
    runner->Call({"actions", "from"}, {JsonString("cache/../secret.txt")});
    CHECK(AnswerAll(*runner, true).empty());
    CHECK(LastLog(*runner) == "from nil the path leaves the plugin's folder");
    CHECK(Slurp(library) == "HELIX-FSR");

    stage.scratch.Write("common/Hades II/helix/notes.txt", "EDITED");
    runner->Call({"actions", "remove"}, {});
    const std::vector<Ask> removal = AnswerAll(*runner, true);
    REQUIRE(removal.size() == 1);
    CHECK(removal[0].title == "Remove Helix SR from Hades II?");
    CHECK(Logged(*runner, "remove true 1"));
    CHECK(Slurp(library) == "ORIGINAL-FSR");
    CHECK_FALSE(fs::exists(stage.game / "bin" / "amd_fidelityfx_upscaler_dx12.amd.dll"));
    CHECK(Slurp(stage.game / "helix" / "notes.txt") == "EDITED");
    // Nothing is applied any more: no question.
    runner->Call({"actions", "remove"}, {});
    CHECK(AnswerAll(*runner, true).empty());
    CHECK(Logged(*runner, "remove true 0"));
}

TEST_CASE("den.mods.layer sets a Vulkan layer up on Linux only, after the question") {
    Stage stage;
    const fs::path manifest = stage.mods->layer_dir / "emuden-test.mods-lsfg_vk.json";
    {
        auto windows = stage.Run(R"(["game.files"])", "windows");
        windows->Call({"actions", "layer"}, {JsonString("cache/lsfg_vk.json")});
        CHECK(AnswerAll(*windows, true).empty());
        CHECK(Logged(*windows, "layer nil Vulkan layers are set up on Linux only"));
        CHECK_FALSE(fs::exists(stage.mods->layer_dir));
    }
    auto runner = stage.Run(R"(["game.files"])");
    runner->Call({"actions", "layer"}, {JsonString("cache/bad.json")});
    CHECK(AnswerAll(*runner, true).empty());
    CHECK(LastLog(*runner).find("has no disable_environment") != std::string::npos);
    runner->Call({"actions", "layer"}, {JsonString("downloads/notes.txt")});
    CHECK(AnswerAll(*runner, true).empty());
    CHECK(LastLog(*runner) == "layer nil downloads/notes.txt is not under cache/");

    runner->Call({"actions", "layer"}, {JsonString("cache/lsfg_vk.json")});
    const std::vector<Ask> refused = AnswerAll(*runner, false);
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].title == "Set up the Vulkan layer lsfg?");
    CHECK(refused[0].message.find("emuden-test.mods-lsfg_vk.json") != std::string::npos);
    CHECK(Logged(*runner, "layer nil cancelled"));
    CHECK_FALSE(fs::exists(manifest));
    runner->Call({"actions", "layer_state"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "layer_state nil"));

    runner->Call({"actions", "layer"}, {JsonString("cache/lsfg_vk.json")});
    CHECK(AnswerAll(*runner, true).size() == 1);
    CHECK(Logged(*runner, "layer true nil"));
    const auto written = ParseJson(Slurp(manifest));
    REQUIRE(written);
    const fs::path library = PathFromUtf8(written->Find("layer")->String("library_path"));
    CHECK(library.is_absolute());
    CHECK(Slurp(library) == "LAYER-LIBRARY");
    runner->Call({"actions", "layer_state"}, {});
    runner->Settle();
    CHECK(Logged(*runner, "layer_state 1794528000 emuden-test.mods-lsfg_vk.json true"));

    runner->Call({"actions", "remove_layer"}, {});
    CHECK(AnswerAll(*runner, true).size() == 1);
    CHECK(Logged(*runner, "remove_layer true nil"));
    CHECK_FALSE(fs::exists(manifest));
    CHECK_FALSE(fs::exists(library));
}
