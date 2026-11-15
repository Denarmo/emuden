// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/plugins/index.h"
#include "den/plugins/manifest.h"
#include "den/plugins/signature.h"

using namespace den;
using namespace den::plugins;

namespace {

std::string Fixture(const char* name) {
    std::ifstream file(std::filesystem::path(DEN_PLUGINS_FIXTURES) / name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

const std::int64_t NOV_13_2026 = *ParseUtcTime("2026-11-13T00:00:00Z");

} // namespace

TEST_CASE("versions and ranges") {
    CHECK(Version::Parse("1.2.3") == std::optional<Version>(Version{1, 2, 3}));
    CHECK_FALSE(Version::Parse("1.2"));
    CHECK_FALSE(Version::Parse("1.2.3-beta"));
    CHECK(Version{1, 10, 0} > Version{1, 9, 9});
    CHECK(VersionInRange({1, 4, 0}, "*"));
    CHECK(VersionInRange({1, 4, 0}, ">=1.0.0 <2.0.0"));
    CHECK_FALSE(VersionInRange({2, 0, 0}, ">=1.0.0 <2.0.0"));
    CHECK(VersionInRange({1, 0, 0}, "=1.0.0"));
    CHECK_FALSE(VersionInRange({1, 0, 0}, "nonsense"));
}

TEST_CASE("paths and dates") {
    CHECK(IsSafeRelativePath("mgba_libretro.dll"));
    CHECK(IsSafeRelativePath("PPSSPP/flash0/font/"));
    CHECK_FALSE(IsSafeRelativePath("../evil.dll"));
    CHECK_FALSE(IsSafeRelativePath("/abs"));
    CHECK_FALSE(IsSafeRelativePath("C:/abs"));
    CHECK_FALSE(IsSafeRelativePath("a\\b"));
    CHECK(ParseUtcTime("1970-01-02T00:00:01Z") == std::optional<std::int64_t>(86401));
    CHECK(ParseUtcTime("2026-11-12T19:34:28Z") == std::optional<std::int64_t>(1794512068));
    CHECK_FALSE(ParseUtcTime("2026-11-12 19:34:28"));
}

TEST_CASE("base64 and Ed25519 round trip") {
    const std::array<std::uint8_t, 5> bytes = {1, 2, 3, 4, 5};
    CHECK(EncodeBase64(bytes) == "AQIDBAU=");
    CHECK(DecodeBase64("AQIDBAU=") == std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    CHECK_FALSE(DecodeBase64("AQID*AU="));

    std::array<std::uint8_t, 32> seed{};
    seed[0] = 7;
    const KeyPair key = KeyPairFromSeed(seed);
    const std::string signature = SignEd25519(bytes, key);
    CHECK(VerifyEd25519(bytes, signature, key.public_b64));
    const std::array<std::uint8_t, 5> other = {1, 2, 3, 4, 6};
    CHECK_FALSE(VerifyEd25519(other, signature, key.public_b64));
    CHECK_FALSE(VerifyEd25519(bytes, signature, OfficialKeys()[0]));
}

TEST_CASE("the published index verifies against the official keys") {
    const std::string bytes = Fixture("index.json");
    const std::string signature = Fixture("index.json.sig");
    REQUIRE(!bytes.empty());
    Index index;
    IndexVerdict verdict = VerifyIndex(bytes, signature, OfficialKeys(), 0, NOV_13_2026, index);
    INFO(verdict.message);
    REQUIRE(verdict.check == IndexCheck::Ok);
    CHECK(index.sequence == 5);
    CHECK(index.source_name == "EmuDEN");
    const IndexPlugin* gb = index.Find("emuden.gb");
    REQUIRE(gb != nullptr);
    CHECK(gb->version == Version{1, 0, 0});
    CHECK(gb->package.url == "packages/emuden.gb-1.0.0.emuplugin");
    CHECK(gb->package.size == 1102);

    std::string tampered = bytes;
    tampered[tampered.find("emuden.gb")] = 'x';
    CHECK(VerifyIndex(tampered, signature, OfficialKeys(), 0, NOV_13_2026, index).check ==
          IndexCheck::BadSignature);
    CHECK(VerifyIndex(bytes, signature, OfficialKeys(), 6, NOV_13_2026, index).check ==
          IndexCheck::Rollback);
    CHECK(VerifyIndex(bytes, signature, OfficialKeys(), 0, NOV_13_2026 + 400 * 86400, index)
              .check == IndexCheck::Expired);
    const std::vector<std::string> spare = {OfficialKeys()[1]};
    CHECK(VerifyIndex(bytes, signature, spare, 0, NOV_13_2026, index).check ==
          IndexCheck::BadSignature);
}

TEST_CASE("source addresses") {
    auto location = ParseSourceUrl(OFFICIAL_SOURCE);
    REQUIRE(location.ok());
    CHECK(location->index_url ==
          "https://raw.githubusercontent.com/Denarmo/emuden-plugins/main/index.json");
    CHECK(ResolveSourceUrl(location->index_url, "packages/a.emuplugin") ==
          "https://raw.githubusercontent.com/Denarmo/emuden-plugins/main/packages/a.emuplugin");
    CHECK(ResolveSourceUrl(location->index_url, "https://example.org/a") == "https://example.org/a");
    CHECK(ResolveSourceUrl(location->index_url, "../a").empty());
    CHECK(ResolveSourceUrl(location->index_url, "http://example.org/a").empty());
    CHECK_FALSE(ParseSourceUrl("https://example.org/index.json").ok());
}

TEST_CASE("every bundled manifest is read") {
    std::size_t count = 0;
    const BundledManifest* bundled = BundledManifests(count);
    REQUIRE(count >= 19);
    bool saw_gb = false, saw_psx = false, saw_switch2 = false;
    for (std::size_t i = 0; i < count; ++i) {
        Result<Manifest> manifest = ParseManifest(
            std::string_view(reinterpret_cast<const char*>(bundled[i].data), bundled[i].size));
        INFO(bundled[i].id << ": " << manifest.error());
        REQUIRE(manifest.ok());
        CHECK(manifest->id == bundled[i].id);
        const Console& console = manifest->systems.front().console;
        if (manifest->id == "emuden.gb") {
            saw_gb = true;
            CHECK(console.id == "gb");
            CHECK(console.extensions == std::vector<std::string>{"gb"});
            CHECK(console.scan.folder_aliases == std::vector<std::string>{"gameboy"});
            CHECK(console.cover_aspect == 1.0f);
            CHECK(console.handheld);
            CHECK(console.thumbnails == "Nintendo - Game Boy");
            REQUIRE(console.scan.header_title.size() == 1);
            CHECK(console.scan.header_title[0].offset == 308);
            CHECK(console.buttons.size() == 4);
            const auto cores = manifest->CoresFor(manifest->systems.front(), "windows-x86_64");
            REQUIRE(cores.size() == 4);
            CHECK(cores[0]->id == "mgba");
            CHECK(cores[0]->DownloadFor("windows-x86_64")->entry == "mgba_libretro.dll");
            CHECK(manifest->CoresFor(manifest->systems.front(), "android-arm64").size() == 4);
        } else if (manifest->id == "emuden.psx") {
            saw_psx = true;
            CHECK(console.scan.index_extensions == std::vector<std::string>{"m3u", "cue"});
            CHECK(console.scan.member_extensions == std::vector<std::string>{"bin"});
            CHECK(console.scan.loose_members);
            CHECK(console.bios.empty()); // only cores that need no BIOS are offered
        } else if (manifest->id == "emuden.ps2") {
            // Play! needs no BIOS and is the default everywhere; PSX2 ECP is an optional
            // Android engine app that needs the player's BIOS.
            const auto android = manifest->CoresFor(manifest->systems.front(), "android-arm64");
            REQUIRE(android.size() == 2);
            CHECK(android[0]->id == "play");
            CHECK(android[1]->type == "engine-app");
            const auto cores = manifest->CoresFor(manifest->systems.front(), "windows-x86_64");
            REQUIRE(cores.size() == 1);
            CHECK(cores[0]->id == "play");
            CHECK(console.bios.empty());
        } else if (manifest->id == "emuden.gba") {
            // GBA DS is chosen by ROM code for the games it has a second screen for.
            const Component* gbads = manifest->FindComponent("gbads");
            REQUIRE(gbads != nullptr);
            CHECK(gbads->games.offset == 0xAC);
            CHECK(std::find(gbads->games.codes.begin(), gbads->games.codes.end(), "A2CP") != gbads->games.codes.end());
            CHECK(manifest->systems.front().cores.front() == "mgba");
        } else if (manifest->id == "emuden.msx") {
            // blueMSX runs on the free C-BIOS, which comes as system files.
            CHECK(console.bios.empty());
            CHECK(manifest->SystemFilesFor("bluemsx", "android-arm64").size() == 2);
        } else if (manifest->id == "emuden.switch2") {
            saw_switch2 = true;
            CHECK(console.runner == Runner::Engine);
            CHECK(console.short_name == "Switch 2");
            CHECK(console.scan.scanner == "switch2");
        }
    }
    CHECK((saw_gb && saw_psx && saw_switch2));
}

TEST_CASE("a manifest that is not pinned is refused") {
    const char* text = R"({"manifest_version":1,"id":"emuden.t","name":"T","version":"1.0.0",
        "kind":"console","engine":"libretro","api":1,"needs":["engine.libretro"],
        "systems":[{"id":"tt","name":"T","cores":["c"]}],
        "components":[{"id":"c","type":"libretro-core","downloads":{"any":
          {"url":"https://example.org/c.zip","sha256":"","size":4,"archive":"zip","entry":"c.dll"}}}]})";
    CHECK_FALSE(ParseManifest(text).ok());
    std::string good(text);
    good.replace(good.find("\"sha256\":\"\""), 11, "\"sha256\":\"" + std::string(64, 'a') + "\"");
    CHECK(ParseManifest(good).ok());
    std::string http = good;
    http.replace(http.find("https://"), 8, "http://");
    CHECK_FALSE(ParseManifest(http).ok());
    std::string future = good;
    future.replace(future.find("engine.libretro"), 15, "engine.warpdrive");
    CHECK_FALSE(ParseManifest(future).ok());
}
