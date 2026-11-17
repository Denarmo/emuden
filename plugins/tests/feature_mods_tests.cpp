// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What den.mods does on disk, in temporary folders: finding files of a game, unpacking,
// applying with a record and undoing it, and Vulkan layer manifests.

#include <catch2/catch_test_macros.hpp>

#include "feature/mods.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins::feature;
using namespace den::plugins::testing;

namespace {

std::string Slurp(const fs::path& file) {
    return ReadTextFile(file).value_or("<missing>");
}

// False where this account may not make links (Windows without developer mode).
bool Link(const fs::path& to, const fs::path& link, bool folder) {
    std::error_code ec;
    if (folder) {
        fs::create_directory_symlink(to, link, ec);
    } else {
        fs::create_symlink(to, link, ec);
    }
    return !ec;
}

std::vector<std::string> Paths(const std::vector<mods::Found>& found) {
    std::vector<std::string> out;
    for (const mods::Found& file : found) {
        out.push_back(file.path);
    }
    return out;
}

// A game folder, a plugin's cache and the records, all in one scratch folder.
struct Game {
    ScratchDir scratch;
    fs::path dir = scratch.dir / "common" / "Hades II";
    fs::path cache = scratch.dir / "plugin" / "cache";
    mods::Store store{scratch.dir / "mods" / "test.mods"};

    Game() {
        scratch.Write("common/Hades II/Hades2.exe", "GAME");
        scratch.Write("common/Hades II/bin/amd_fidelityfx_upscaler_dx12.dll", "ORIGINAL-FSR");
        scratch.Write("common/Hades II/bin/dxgi.dll", "ORIGINAL-DXGI");
        scratch.Write("plugin/cache/fsr.dll", "HELIX-FSR");
        scratch.Write("plugin/cache/extra.dll", "HELIX-EXTRA");
        scratch.Write("plugin/cache/readme.txt", "READ ME");
    }
    std::vector<mods::Order> Orders() const {
        return {{cache / "fsr.dll", "bin/amd_fidelityfx_upscaler_dx12.dll"},
                {cache / "extra.dll", "bin/helix/extra.dll"},
                {cache / "readme.txt", "helix-readme.txt"}};
    }
};

const char* const MANIFEST = R"({
  "file_format_version" : "1.0.0",
  "layer" : {
    "name" : "VK_LAYER_LS_frame_generation",
    "type" : "GLOBAL",
    "library_path" : "../../../lib/liblsfg-vk.so",
    "api_version" : "1.4.313",
    "implementation_version" : "2",
    "description" : "Frame generation, \"library_path\" : \"decoy\"",
    "disable_environment" : { "DISABLE_LSFGVK" : "1" }
  }
}
)";

std::string Replaced(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

} // namespace

TEST_CASE("mods find lists a game's files by name without following links") {
    ScratchDir scratch;
    const fs::path game = scratch.dir / "game";
    scratch.Write("game/Game.exe", "exe");
    scratch.Write("game/bin/AMD_FidelityFX_Upscaler_DX12.dll", "fsr");
    scratch.Write("game/bin/x64/nvngx_dlss.dll", "dlss");
    scratch.Write("game/EasyAntiCheat/EasyAntiCheat_x64.dll", "eac");
    scratch.Write("game/1/2/3/4/5/6/7/8/deep.dll", "deep");
    scratch.Write("game/1/2/3/4/5/6/7/8/9/deeper.dll", "deeper");
    scratch.Write("outside/secret.dll", "secret");

    CHECK(Paths(mods::Find(game, {"amd_fidelityfx_upscaler_dx12.dll"})) ==
          std::vector<std::string>{"bin/AMD_FidelityFX_Upscaler_DX12.dll"});
    const auto dlls = mods::Find(game, {"*.dll"});
    // Eight folders down is as deep as it looks.
    CHECK(Paths(dlls) == std::vector<std::string>{"1/2/3/4/5/6/7/8/deep.dll", "EasyAntiCheat/EasyAntiCheat_x64.dll",
                                                  "bin/AMD_FidelityFX_Upscaler_DX12.dll", "bin/x64/nvngx_dlss.dll"});
    REQUIRE(dlls.size() == 4);
    CHECK(dlls[1].size == 3);
    CHECK(dlls[1].sha256 == HashOf("eac"));
    CHECK(Paths(mods::Find(game, {"easyanticheat*", "GAME.EXE"})) ==
          std::vector<std::string>{"Game.exe", "EasyAntiCheat/EasyAntiCheat_x64.dll"});
    CHECK(mods::Find(game, {"nothing.dll"}).empty());
    CHECK(mods::Find(scratch.dir / "missing", {"*"}).empty());

    CHECK(mods::CheckNames({"a.dll", "b*.dll"}).ok);
    CHECK_FALSE(mods::CheckNames({}).ok);
    CHECK_FALSE(mods::CheckNames({"a*b*.dll"}).ok);
    CHECK_FALSE(mods::CheckNames({"bin/a.dll"}).ok);
    CHECK_FALSE(mods::CheckNames({".."}).ok);
    CHECK_FALSE(mods::CheckNames(std::vector<std::string>(17, "a.dll")).ok);

    for (int i = 0; i < 70; ++i) {
        scratch.Write("game/many/file" + std::to_string(i) + ".pak", "p");
    }
    CHECK(mods::Find(game, {"*.pak"}).size() == mods::MAX_FOUND);

    if (Link(scratch.dir / "outside", game / "linked", true) && Link(scratch.dir / "outside" / "secret.dll", game / "link.dll", false)) {
        CHECK(Paths(mods::Find(game, {"secret.dll", "link.dll"})).empty());
    }
}

TEST_CASE("a mod's file must stay inside the game's folder") {
    Game game;
    CHECK(mods::Target(game.dir, "bin/dxgi.dll").ok());
    CHECK(mods::Target(game.dir, "new/folder/file.dll").ok());
    for (const char* bad : {"../Hades II/x.dll", "bin/../../x.dll", "/etc/passwd", "C:/Windows/x.dll", "bin\\x.dll",
                            "bin//x.dll", "bin/", "", "bin/./x.dll", "bin/x.dll.", "bin /x.dll", "a:b"}) {
        INFO(bad);
        CHECK_FALSE(mods::Target(game.dir, bad).ok());
    }
    CHECK_FALSE(mods::Target(game.dir, "bin").ok());             // a folder is there
    CHECK_FALSE(mods::Target(game.dir, "Hades2.exe/x.dll").ok()); // a file is on the way
    CHECK_FALSE(mods::Target(game.scratch.dir / "missing", "x.dll").ok());

    game.scratch.Write("outside/victim.dll", "VICTIM");
    if (Link(game.scratch.dir / "outside", game.dir / "linked", true) &&
        Link(game.scratch.dir / "outside" / "victim.dll", game.dir / "link.dll", false)) {
        CHECK_FALSE(mods::Target(game.dir, "linked/victim.dll").ok());
        CHECK_FALSE(mods::Target(game.dir, "linked/new.dll").ok());
        CHECK_FALSE(mods::Target(game.dir, "link.dll").ok());
        const Status applied = game.store.Apply("1145350", "Helix", game.dir, {{game.cache / "fsr.dll", "linked/victim.dll"}}, 1);
        CHECK_FALSE(applied.ok);
        CHECK(Slurp(game.scratch.dir / "outside" / "victim.dll") == "VICTIM");
        // Nor is a link taken as the file to copy.
        REQUIRE(Link(game.cache / "fsr.dll", game.cache / "alias.dll", false));
        CHECK_FALSE(game.store.Plan("1145350", game.dir, {{game.cache / "alias.dll", "bin/new.dll"}}).ok());
    }
}

TEST_CASE("mods unpack writes the named entries of a zip and a tar.xz") {
    ScratchDir scratch;
    const fs::path out = scratch.dir / "cache" / "mods" / "release";
    scratch.Write("release.zip", StoredZipOf({{"helix/hlx_fsr4.dll", "FSR4"}, {"helix/readme.txt", "hello"}}));
    const std::uint64_t room = 1u << 20;
    REQUIRE(mods::Unpack(scratch.dir / "release.zip", {"helix/hlx_fsr4.dll"}, out, room).ok);
    CHECK(Slurp(out / "helix" / "hlx_fsr4.dll") == "FSR4");
    CHECK_FALSE(fs::exists(out / "helix" / "readme.txt"));
    CHECK_FALSE(mods::Unpack(scratch.dir / "release.zip", {"helix/missing.dll"}, out, room).ok);
    CHECK_FALSE(mods::Unpack(scratch.dir / "release.zip", {"../helix/hlx_fsr4.dll"}, out, room).ok);
    CHECK_FALSE(mods::Unpack(scratch.dir / "release.zip", {"/helix/hlx_fsr4.dll"}, out, room).ok);
    CHECK_FALSE(mods::Unpack(scratch.dir / "release.zip", {"helix\\hlx_fsr4.dll"}, out, room).ok);
    CHECK_FALSE(mods::Unpack(scratch.dir / "release.zip", {}, out, room).ok);
    CHECK_FALSE(mods::Unpack(scratch.dir / "release.zip", std::vector<std::string>(33, "helix/readme.txt"), out, room).ok);
    CHECK(mods::Unpack(scratch.dir / "release.zip", {"helix/hlx_fsr4.dll"}, out, 3).error == "the plugin's folder is full");
    CHECK_FALSE(mods::Unpack(scratch.dir / "none.zip", {"a"}, out, room).ok);
    scratch.Write("text.zip", "this is no archive");
    CHECK_FALSE(mods::Unpack(scratch.dir / "text.zip", {"a"}, out, room).ok);

    std::string big;
    for (std::size_t i = 0; i < 200'000; ++i) {
        big.push_back(static_cast<char>((i * 31 + (i >> 8)) & 0xff));
    }
    const std::string deep = "release/deep-folder-name/deep-folder-name/deep-folder-name/deep-folder-name/"
                             "deep-folder-name/deep-folder-name/deep-folder-name/a-file-with-a-long-name.txt";
    for (const char* name : {"pax.tar.xz", "gnu.tar.xz"}) {
        INFO(name);
        const fs::path archive = PathFromUtf8(DEN_PLUGINS_FIXTURES) / "mods" / name;
        const fs::path to = scratch.dir / "cache" / "mods" / mods::ArchiveStem(name);
        const Status done =
            mods::Unpack(archive, {"release/lib/liblayer.so", "release/share/layer.json", "release/big.bin", deep}, to, room);
        INFO(done.error);
        REQUIRE(done.ok);
        CHECK(Slurp(to / "release" / "lib" / "liblayer.so") == "LIBRARY");
        CHECK(Slurp(to / "release" / "share" / "layer.json") == "{\"file_format_version\": \"1.0.0\"}\n");
        CHECK(Slurp(to / "release" / "big.bin") == big);
        CHECK(Slurp(to / PathFromUtf8(deep)) == "long");
        CHECK_FALSE(fs::exists(to / "release" / "link.so"));
        CHECK_FALSE(mods::Unpack(archive, {"release/link.so"}, to, room).ok); // a link
        CHECK_FALSE(fs::exists(to / "release" / "link.so"));
        CHECK_FALSE(mods::Unpack(archive, {"release"}, to, room).ok);         // a folder
        CHECK_FALSE(mods::Unpack(archive, {"release/missing"}, to, room).ok);
        CHECK(mods::Unpack(archive, {"release/big.bin"}, to, 1000).error == "the plugin's folder is full");
    }
    CHECK(mods::ArchiveStem("lsfg-vk-2.0.tar.xz") == "lsfg-vk-2.0");
    CHECK(mods::ArchiveStem("Helix.SR.ZIP") == "Helix.SR");

    // A .xz cut off in the middle.
    const auto whole = ReadTextFile(PathFromUtf8(DEN_PLUGINS_FIXTURES) / "mods" / "pax.tar.xz");
    REQUIRE(whole);
    scratch.Write("cut.tar.xz", whole->substr(0, whole->size() / 2));
    CHECK_FALSE(mods::Unpack(scratch.dir / "cut.tar.xz", {"release/big.bin"}, out, room).ok);
    CHECK_FALSE(fs::exists(out / "release" / "big.bin"));
}

TEST_CASE("an applied mod is removed and the game's files are back byte for byte") {
    Game game;
    const auto plan = game.store.Plan("1145350", game.dir, game.Orders());
    REQUIRE(plan.ok());
    REQUIRE(plan.value().size() == 3);
    CHECK(plan.value()[0].replaces);
    CHECK_FALSE(plan.value()[1].replaces);
    CHECK_FALSE(game.store.State("1145350"));

    REQUIRE(game.store.Apply("1145350", "Helix SR", game.dir, game.Orders(), 1794528000).ok);
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "HELIX-FSR");
    CHECK(Slurp(game.dir / "bin" / "helix" / "extra.dll") == "HELIX-EXTRA");
    CHECK(Slurp(game.dir / "helix-readme.txt") == "READ ME");
    CHECK(Slurp(game.dir / "bin" / "dxgi.dll") == "ORIGINAL-DXGI");
    const fs::path home = game.scratch.dir / "mods" / "test.mods" / "1145350";
    CHECK(Slurp(home / "originals" / "0") == "ORIGINAL-FSR");
    const auto record = ParseJson(Slurp(home / "record.json"));
    REQUIRE(record);
    CHECK(record->Find("files")->items[0].String("written") == HashOf("HELIX-FSR"));
    CHECK(record->Find("files")->items[0].String("original") == HashOf("ORIGINAL-FSR"));
    CHECK(record->Find("files")->items[1].Find("original")->kind == JsonValue::Kind::Null);

    const auto state = game.store.State("1145350");
    REQUIRE(state);
    CHECK(state->name == "Helix SR");
    CHECK(state->time == 1794528000);
    CHECK(state->complete);
    REQUIRE(state->files.size() == 3);
    CHECK(state->files[0].intact);
    CHECK(state->files[2].intact);
    CHECK(game.store.Games() == std::vector<std::string>{"1145350"});
    CHECK(game.store.Name("1145350") == std::optional<std::string>("Helix SR"));

    const auto left = game.store.Remove("1145350");
    REQUIRE(left.ok());
    CHECK(left.value().empty());
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "ORIGINAL-FSR");
    CHECK_FALSE(fs::exists(game.dir / "bin" / "helix"));
    CHECK_FALSE(fs::exists(game.dir / "helix-readme.txt"));
    CHECK(Slurp(game.dir / "bin" / "dxgi.dll") == "ORIGINAL-DXGI");
    CHECK_FALSE(game.store.State("1145350"));
    CHECK_FALSE(fs::exists(home));
    CHECK(game.store.Remove("1145350").ok()); // nothing to undo is no failure
}

TEST_CASE("a file changed after the apply is left alone and reported") {
    Game game;
    REQUIRE(game.store.Apply("1145350", "Helix SR", game.dir, game.Orders(), 1).ok);
    // A game update replaced the library and the user changed the added text.
    game.scratch.Write("common/Hades II/bin/amd_fidelityfx_upscaler_dx12.dll", "UPDATED-FSR");
    game.scratch.Write("common/Hades II/helix-readme.txt", "EDITED");
    const auto state = game.store.State("1145350");
    REQUIRE(state);
    CHECK_FALSE(state->files[0].intact);
    CHECK(state->files[1].intact);
    CHECK_FALSE(state->files[2].intact);

    const auto left = game.store.Remove("1145350");
    REQUIRE(left.ok());
    CHECK(left.value() == std::vector<std::string>{"bin/amd_fidelityfx_upscaler_dx12.dll", "helix-readme.txt"});
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "UPDATED-FSR");
    CHECK(Slurp(game.dir / "helix-readme.txt") == "EDITED");
    CHECK_FALSE(fs::exists(game.dir / "bin" / "helix" / "extra.dll"));
    CHECK_FALSE(game.store.State("1145350"));
}

TEST_CASE("a second apply undoes the first, and an apply cut short can be removed") {
    Game game;
    REQUIRE(game.store.Apply("1145350", "Helix SR 1", game.dir, game.Orders(), 1).ok);
    // The next version replaces another library and adds nothing.
    const auto plan = game.store.Plan("1145350", game.dir, {{game.cache / "extra.dll", "bin/helix/extra.dll"}});
    REQUIRE(plan.ok());
    CHECK_FALSE(plan.value()[0].replaces); // the first apply's own file
    REQUIRE(game.store.Apply("1145350", "Helix SR 2", game.dir, {{game.cache / "extra.dll", "bin/dxgi.dll"}}, 2).ok);
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "ORIGINAL-FSR");
    CHECK_FALSE(fs::exists(game.dir / "bin" / "helix"));
    CHECK(Slurp(game.dir / "bin" / "dxgi.dll") == "HELIX-EXTRA");
    CHECK(game.store.State("1145350")->name == "Helix SR 2");
    REQUIRE(game.store.Remove("1145350").ok());
    CHECK(Slurp(game.dir / "bin" / "dxgi.dll") == "ORIGINAL-DXGI");

    // Stopped after the first of three copies: the record is there, the rest is not.
    int copies = 0;
    const Status cut = game.store.Apply("1145350", "Helix SR", game.dir, game.Orders(), 3, [&] { return copies++ == 1; });
    CHECK_FALSE(cut.ok);
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "HELIX-FSR");
    CHECK_FALSE(fs::exists(game.dir / "bin" / "helix" / "extra.dll"));
    const auto state = game.store.State("1145350");
    REQUIRE(state);
    CHECK_FALSE(state->complete);
    CHECK(state->files[0].intact);
    CHECK_FALSE(state->files[1].intact);
    // Half a copy of the next file, as a crash would leave it.
    game.scratch.Write("common/Hades II/helix-readme.txt", "REA");
    const auto left = game.store.Remove("1145350");
    REQUIRE(left.ok());
    CHECK(left.value().empty());
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "ORIGINAL-FSR");
    CHECK_FALSE(fs::exists(game.dir / "helix-readme.txt"));
    CHECK_FALSE(fs::exists(game.dir / "bin" / "helix"));
    CHECK_FALSE(game.store.State("1145350"));
}

TEST_CASE("a replaced file is also kept under another name beside it") {
    Game game;
    const std::vector<mods::Order> orders = {
        {game.cache / "fsr.dll", "bin/amd_fidelityfx_upscaler_dx12.dll", "amd_fidelityfx_upscaler_dx12.amd.dll"}};
    const fs::path kept = game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.amd.dll";
    const auto plan = game.store.Plan("1145350", game.dir, orders);
    REQUIRE(plan.ok());
    CHECK(plan.value()[0].keep == "bin/amd_fidelityfx_upscaler_dx12.amd.dll");
    REQUIRE(game.store.Apply("1145350", "Helix SR", game.dir, orders, 1).ok);
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "HELIX-FSR");
    CHECK(Slurp(kept) == "ORIGINAL-FSR");
    CHECK(Slurp(game.scratch.dir / "mods" / "test.mods" / "1145350" / "originals" / "0") == "ORIGINAL-FSR");
    // Applying again finds the copy the app made, and makes it anew.
    CHECK(game.store.Plan("1145350", game.dir, orders).ok());
    REQUIRE(game.store.Apply("1145350", "Helix SR", game.dir, orders, 2).ok);
    CHECK(Slurp(kept) == "ORIGINAL-FSR");
    const auto left = game.store.Remove("1145350");
    REQUIRE(left.ok());
    CHECK(left.value().empty());
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "ORIGINAL-FSR");
    CHECK_FALSE(fs::exists(kept));

    // Refused: nothing to keep, a name that is no plain name, a file of the game in its place.
    CHECK_FALSE(game.store.Plan("1145350", game.dir, {{game.cache / "fsr.dll", "bin/new.dll", "old.dll"}}).ok());
    for (const char* name : {"../kept.dll", "sub/kept.dll", "..", "kept.dll.", "dxgi.dll",
                             "amd_fidelityfx_upscaler_dx12.dll"}) {
        INFO(name);
        CHECK_FALSE(game.store.Plan("1145350", game.dir, {{game.cache / "fsr.dll", "bin/amd_fidelityfx_upscaler_dx12.dll", name}}).ok());
    }
    CHECK_FALSE(game.store.Plan("1145350", game.dir,
                                {{game.cache / "fsr.dll", "bin/amd_fidelityfx_upscaler_dx12.dll", "kept.dll"},
                                 {game.cache / "extra.dll", "bin/kept.dll"}})
                    .ok());

    // A kept copy the user replaced is left alone.
    REQUIRE(game.store.Apply("1145350", "Helix SR", game.dir, orders, 3).ok);
    game.scratch.Write("common/Hades II/bin/amd_fidelityfx_upscaler_dx12.amd.dll", "SOMETHING ELSE");
    const auto after = game.store.Remove("1145350");
    REQUIRE(after.ok());
    CHECK(after.value() == std::vector<std::string>{"bin/amd_fidelityfx_upscaler_dx12.amd.dll"});
    CHECK(Slurp(kept) == "SOMETHING ELSE");
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "ORIGINAL-FSR");
}

TEST_CASE("an apply is refused when it is too much or names a file twice") {
    Game game;
    std::vector<mods::Order> many(65, mods::Order{game.cache / "fsr.dll", "x.dll"});
    for (std::size_t i = 0; i < many.size(); ++i) {
        many[i].to = "many/" + std::to_string(i) + ".dll";
    }
    CHECK_FALSE(game.store.Plan("1145350", game.dir, many).ok());
    many.pop_back();
    CHECK(game.store.Plan("1145350", game.dir, many).ok());
    CHECK_FALSE(game.store.Plan("1145350", game.dir, {}).ok());
    CHECK_FALSE(game.store.Plan("1145350", game.dir, {{game.cache / "fsr.dll", "a.dll"}, {game.cache / "extra.dll", "A.DLL"}}).ok());
    CHECK_FALSE(game.store.Plan("1145350", game.dir, {{game.cache / "missing.dll", "a.dll"}}).ok());
    CHECK_FALSE(game.store.Plan("../1145350", game.dir, {{game.cache / "fsr.dll", "a.dll"}}).ok());
    CHECK_FALSE(game.store.Apply("1145350", "Helix", game.dir, {{game.cache / "fsr.dll", "../a.dll"}}, 1).ok);
    CHECK_FALSE(fs::exists(game.scratch.dir / "common" / "a.dll"));
    CHECK_FALSE(game.store.State("1145350"));

    // Over 1 GiB in all; the files are only measured here.
    std::error_code ec;
    fs::resize_file(game.cache / "extra.dll", (600ull << 20), ec);
    fs::resize_file(game.cache / "readme.txt", (600ull << 20), ec);
    if (!ec) {
        const auto plan = game.store.Plan("1145350", game.dir, {{game.cache / "extra.dll", "a.bin"}, {game.cache / "readme.txt", "b.bin"}});
        CHECK(plan.error() == "the files are more than 1 GiB");
    }
    fs::resize_file(game.cache / "extra.dll", 1, ec);
    fs::resize_file(game.cache / "readme.txt", 1, ec);
}

TEST_CASE("a layer manifest needs disable_environment and only its library_path changes") {
    const auto rewritten = mods::RewriteLayerManifest(MANIFEST, "/data/mods/layers/lsfg/liblsfg-vk.so");
    REQUIRE(rewritten.ok());
    CHECK(rewritten.value() ==
          Replaced(MANIFEST, "\"../../../lib/liblsfg-vk.so\"", "\"/data/mods/layers/lsfg/liblsfg-vk.so\""));
    const auto windows = mods::RewriteLayerManifest(MANIFEST, "C:\\data\\lib \"x\".dll");
    REQUIRE(windows.ok());
    const auto parsed = ParseJson(windows.value());
    REQUIRE(parsed);
    CHECK(parsed->Find("layer")->String("library_path") == "C:\\data\\lib \"x\".dll");

    CHECK_FALSE(mods::RewriteLayerManifest(Replaced(MANIFEST, "\"disable_environment\" : { \"DISABLE_LSFGVK\" : \"1\" }",
                                                    "\"enable_environment\" : { \"ENABLE_LSFGVK\" : \"1\" }"),
                                           "/x.so")
                    .ok());
    CHECK_FALSE(mods::RewriteLayerManifest(Replaced(MANIFEST, "{ \"DISABLE_LSFGVK\" : \"1\" }", "{}"), "/x.so").ok());
    CHECK_FALSE(mods::RewriteLayerManifest(Replaced(MANIFEST, "\"layer\"", "\"driver\""), "/x.so").ok());
    CHECK_FALSE(mods::RewriteLayerManifest(Replaced(MANIFEST, "\"GLOBAL\"", "\"DRIVER\""), "/x.so").ok());
    CHECK_FALSE(mods::RewriteLayerManifest("{\"ICD\": {\"library_path\": \"x.so\"}}", "/x.so").ok());
    CHECK_FALSE(mods::RewriteLayerManifest("not json", "/x.so").ok());

    const std::string two = R"({"file_format_version":"1.2.0","layers":[
      {"name":"VK_LAYER_a","type":"GLOBAL","library_path":"a.so","disable_environment":{"NO_A":"1"}},
      {"name":"VK_LAYER_b","type":"INSTANCE","library_path":"b.so","disable_environment":{"NO_B":"1"}}]})";
    const auto both = mods::RewriteLayerManifest(two, "/lib.so");
    REQUIRE(both.ok());
    CHECK(both.value() == Replaced(Replaced(two, "\"a.so\"", "\"/lib.so\""), "\"b.so\"", "\"/lib.so\""));
    CHECK_FALSE(mods::RewriteLayerManifest(Replaced(two, ",\"disable_environment\":{\"NO_B\":\"1\"}", ""), "/lib.so").ok());
}

TEST_CASE("a layer is set up in the layer folder and removed again") {
    ScratchDir scratch;
    scratch.Write("cache/lsfg_vk.json", MANIFEST);
    scratch.Write("cache/liblsfg-vk.so", "LAYER-LIBRARY");
    scratch.Write("cache/plain.json", "{\"file_format_version\":\"1.0.0\",\"layer\":{\"name\":\"x\",\"type\":\"GLOBAL\",\"library_path\":\"x.so\"}}");
    const fs::path layers_dir = scratch.dir / "home" / "vulkan" / "implicit_layer.d";
    mods::Store store(scratch.dir / "mods" / "emuden.tune.lsfg");
    const std::vector<mods::LayerOrder> order = {{scratch.dir / "cache" / "lsfg_vk.json", scratch.dir / "cache" / "liblsfg-vk.so"}};

    CHECK_FALSE(store.ApplyLayer("lsfg", {{scratch.dir / "cache" / "plain.json", scratch.dir / "cache" / "liblsfg-vk.so"}},
                                 layers_dir, "emuden.tune.lsfg", 5)
                    .ok);
    CHECK_FALSE(store.ApplyLayer("../lsfg", order, layers_dir, "emuden.tune.lsfg", 5).ok);
    CHECK_FALSE(store.ApplyLayer("lsfg", {}, layers_dir, "emuden.tune.lsfg", 5).ok);
    CHECK_FALSE(store.ApplyLayer("lsfg", {{scratch.dir / "cache" / "liblsfg-vk.so", scratch.dir / "cache" / "liblsfg-vk.so"}},
                                 layers_dir, "emuden.tune.lsfg", 5)
                    .ok);
    CHECK_FALSE(fs::exists(layers_dir));
    CHECK_FALSE(store.Layer("lsfg"));

    const auto plan = store.PlanLayer("lsfg", order, "emuden.tune.lsfg");
    REQUIRE(plan.ok());
    CHECK(plan.value() == std::vector<std::string>{"emuden-emuden.tune.lsfg-lsfg_vk.json"});
    const Status applied = store.ApplyLayer("lsfg", order, layers_dir, "emuden.tune.lsfg", 5);
    INFO(applied.error);
    REQUIRE(applied.ok);
    const fs::path library = fs::absolute(scratch.dir / "mods" / "emuden.tune.lsfg" / "layers" / "lsfg" / "liblsfg-vk.so");
    CHECK(Slurp(library) == "LAYER-LIBRARY");
    const fs::path manifest = layers_dir / "emuden-emuden.tune.lsfg-lsfg_vk.json";
    const auto written = ParseJson(Slurp(manifest));
    REQUIRE(written);
    CHECK(written->Find("layer")->String("library_path") == PathToUtf8(library));
    CHECK(written->Find("layer")->Find("disable_environment")->String("DISABLE_LSFGVK") == "1");

    auto state = store.Layer("lsfg");
    REQUIRE(state);
    CHECK(state->time == 5);
    CHECK(state->intact);
    CHECK(state->manifests == std::vector<std::string>{"emuden-emuden.tune.lsfg-lsfg_vk.json"});
    CHECK(store.Layers() == std::vector<std::string>{"lsfg"});
    CHECK(store.Count() == 1);

    scratch.Write("home/vulkan/implicit_layer.d/emuden-emuden.tune.lsfg-lsfg_vk.json", "{}");
    CHECK_FALSE(store.Layer("lsfg")->intact);
    scratch.Write("home/vulkan/implicit_layer.d/other.json", "{}");

    REQUIRE(store.RemoveLayer("lsfg").ok);
    CHECK_FALSE(fs::exists(manifest));
    CHECK_FALSE(fs::exists(library.parent_path()));
    CHECK(fs::exists(layers_dir / "other.json"));
    CHECK_FALSE(store.Layer("lsfg"));
    CHECK(store.RemoveLayer("lsfg").ok);
}

TEST_CASE("everything a plugin set up is undone at once") {
    Game game;
    game.scratch.Write("plugin/cache/layer.json", MANIFEST);
    const fs::path layers_dir = game.scratch.dir / "implicit_layer.d";
    REQUIRE(game.store.Apply("1145350", "Helix SR", game.dir, game.Orders(), 1).ok);
    REQUIRE(game.store.ApplyLayer("lsfg", {{game.cache / "layer.json", game.cache / "extra.dll"}}, layers_dir, "test.mods", 1).ok);
    CHECK(game.store.Count() == 2);
    game.scratch.Write("common/Hades II/helix-readme.txt", "EDITED");
    const Status undone = game.store.RemoveAll();
    CHECK(undone.ok);
    REQUIRE(undone.notes.size() == 1);
    CHECK(undone.notes[0].find("helix-readme.txt") != std::string::npos);
    CHECK(game.store.Count() == 0);
    CHECK(Slurp(game.dir / "bin" / "amd_fidelityfx_upscaler_dx12.dll") == "ORIGINAL-FSR");
    CHECK(ListDirectory(layers_dir).empty());
}
