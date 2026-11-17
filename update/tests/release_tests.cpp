// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/update/release.h"
#include "den/update/version.h"

using namespace den::update;

namespace {

// The shape of api.github.com/repos/Denarmo/emuden/releases/latest, cut down.
constexpr const char* LATEST = R"({
  "url": "https://api.github.com/repos/Denarmo/emuden/releases/1",
  "html_url": "https://github.com/Denarmo/emuden/releases/tag/v0.1.0",
  "tag_name": "v0.1.0",
  "name": "EmuDEN 0.1.0",
  "draft": false,
  "prerelease": false,
  "published_at": "2026-11-16T22:41:07Z",
  "assets": [
    {"name": "EmuDEN-android-arm64.apk", "size": 17780348,
     "digest": "sha256:EDF16ABC283ACB5417F683758B20D9B8C03D4C436C6FC0B6C16A448450EDBA94",
     "browser_download_url": "https://github.com/Denarmo/emuden/releases/download/v0.1.0/EmuDEN-android-arm64.apk"},
    {"name": "EmuDEN-windows-x64.zip", "size": 10088791, "digest": null,
     "browser_download_url": "https://github.com/Denarmo/emuden/releases/download/v0.1.0/EmuDEN-windows-x64.zip"},
    {"name": "SHA256SUMS.txt", "size": 269,
     "browser_download_url": "https://github.com/Denarmo/emuden/releases/download/v0.1.0/SHA256SUMS.txt"}
  ],
  "body": "The first release of EmuDEN.\r\n\r\n- **Windows:** EmuDEN-windows-x64.zip. Unzip it and run `emuden.exe`.\r\n- **Android (arm64):** EmuDEN-android-arm64.apk.\r\n\r\nSHA256SUMS.txt has the checksums."
})";

std::string Item(const std::string& tag, bool draft, bool prerelease) {
    return R"({"tag_name": ")" + tag + R"(", "draft": )" + (draft ? "true" : "false") + R"(, "prerelease": )" +
           (prerelease ? "true" : "false") + R"(, "assets": []})";
}

} // namespace

TEST_CASE("versions parse as semver") {
    const auto version = ParseVersion("v1.20.3-beta.2+build.7");
    REQUIRE(version);
    CHECK(version->major == 1);
    CHECK(version->minor == 20);
    CHECK(version->patch == 3);
    CHECK(version->pre == std::vector<std::string>{"beta", "2"});
    CHECK(ParseVersion("0.1.0"));
    CHECK_FALSE(ParseVersion(""));
    CHECK_FALSE(ParseVersion("dev"));
    CHECK_FALSE(ParseVersion("1.2"));
    CHECK_FALSE(ParseVersion("1.2.3.4"));
    CHECK_FALSE(ParseVersion("01.2.3"));
    CHECK_FALSE(ParseVersion("1.2.3-"));
    CHECK_FALSE(ParseVersion("1.2.3-beta..1"));
    CHECK_FALSE(ParseVersion("1.2.x"));
}

TEST_CASE("versions compare by semver precedence") {
    CHECK(IsNewer("0.1.0", "0.0.9"));
    CHECK(IsNewer("v0.10.0", "0.9.9"));
    CHECK(IsNewer("1.0.0", "0.99.99"));
    CHECK(IsNewer("1.0.0", "1.0.0-rc.1"));
    CHECK(IsNewer("1.0.0-rc.1", "1.0.0-beta.11"));
    CHECK(IsNewer("1.0.0-beta.11", "1.0.0-beta.2"));
    CHECK(IsNewer("1.0.0-beta", "1.0.0-alpha.1"));
    CHECK(IsNewer("1.0.0-alpha.beta", "1.0.0-alpha.1"));
    CHECK(IsNewer("1.0.0-alpha.1", "1.0.0-alpha"));
    CHECK_FALSE(IsNewer("0.1.0", "0.1.0"));
    CHECK_FALSE(IsNewer("0.1.0+other", "0.1.0"));
    CHECK_FALSE(IsNewer("0.0.9", "0.1.0"));
    CHECK_FALSE(IsNewer("garbage", "0.1.0"));
    CHECK_FALSE(IsNewer("0.2.0", ""));
}

TEST_CASE("development builds are never offered updates") {
    CHECK(CanUpdateFrom("0.1.0"));
    CHECK(CanUpdateFrom("0.0.9"));
    CHECK_FALSE(CanUpdateFrom(""));
    CHECK_FALSE(CanUpdateFrom("0.0.0"));
    CHECK_FALSE(CanUpdateFrom("dev"));
    CHECK_FALSE(CanUpdateFrom("0.1"));
}

TEST_CASE("the latest release parses with its assets") {
    const auto release = PickRelease(LATEST, false);
    REQUIRE(release);
    CHECK(release->tag == "v0.1.0");
    CHECK(release->version == "0.1.0");
    CHECK(release->name == "EmuDEN 0.1.0");
    CHECK(release->page == "https://github.com/Denarmo/emuden/releases/tag/v0.1.0");
    CHECK(release->published == "2026-11-16T22:41:07Z");
    REQUIRE(release->assets.size() == 3);
    const Asset* apk = release->Find("EmuDEN-android-arm64.apk");
    REQUIRE(apk);
    CHECK(apk->size == 17780348);
    CHECK(apk->sha256 == "edf16abc283acb5417f683758b20d9b8c03d4c436c6fc0b6c16a448450edba94");
    CHECK(release->Find("EmuDEN-windows-x64.zip")->sha256.empty());
    CHECK(release->Find(SUMS_NAME)->url.ends_with("/v0.1.0/SHA256SUMS.txt"));
    CHECK(release->Find("EmuDEN-x86_64.AppImage") == nullptr);
}

TEST_CASE("drafts and pre-releases are skipped unless beta updates are on") {
    const std::string list = "[" + Item("v0.3.0", true, false) + "," + Item("v0.2.1-beta.1", false, true) + "," +
                             Item("v0.2.0", false, false) + "," + Item("v0.2.2-rc.1", false, false) + "," +
                             Item("nightly", false, false) + "," + Item("v0.1.0", false, false) + "]";
    const auto stable = PickRelease(list, false);
    REQUIRE(stable);
    CHECK(stable->version == "0.2.0");
    const auto beta = PickRelease(list, true);
    REQUIRE(beta);
    CHECK(beta->version == "0.2.2-rc.1");
    CHECK_FALSE(PickRelease("[" + Item("v0.3.0", true, false) + "]", true));
    CHECK_FALSE(PickRelease(Item("v0.3.0", false, true), false));
    CHECK_FALSE(PickRelease("not json", true));
    CHECK_FALSE(PickRelease(R"({"message": "Not Found"})", false));
}

TEST_CASE("each system installs its own asset") {
    CHECK(AssetNameFor("windows-x86_64") == "EmuDEN-windows-x64.zip");
    CHECK(AssetNameFor("linux-x86_64") == "EmuDEN-x86_64.AppImage");
    CHECK(AssetNameFor("android-arm64") == "EmuDEN-android-arm64.apk");
    CHECK(AssetNameFor("windows-arm64").empty());
    CHECK(AssetNameFor("linux-arm64").empty());
    CHECK(AssetNameFor("macos-arm64").empty());
}

TEST_CASE("checksum lines parse in sha256sum's forms") {
    const auto sums = ParseSums(
        "e8d961534d2f4332e6966d7220376606379eecf2e1068652e64a5b4622018543  EmuDEN-windows-x64.zip\n"
        "1506707C66181E20A40F2A7FBD758FB9AF0C9696694DA69704B41EFAABF0F264 *EmuDEN-x86_64.AppImage\r\n"
        "# a comment\n"
        "\n"
        "tooshort  EmuDEN-android-arm64.apk\n"
        "edf16abc283acb5417f683758b20d9b8c03d4c436c6fc0b6c16a448450edba94\n"
        "edf16abc283acb5417f683758b20d9b8c03d4c436c6fc0b6c16a448450edba94  Name with spaces.apk");
    REQUIRE(sums.size() == 3);
    CHECK(sums.at("EmuDEN-windows-x64.zip") == "e8d961534d2f4332e6966d7220376606379eecf2e1068652e64a5b4622018543");
    CHECK(sums.at("EmuDEN-x86_64.AppImage") == "1506707c66181e20a40f2a7fbd758fb9af0c9696694da69704b41efaabf0f264");
    CHECK(sums.contains("Name with spaces.apk"));
    CHECK_FALSE(sums.contains("EmuDEN-android-arm64.apk"));
}

TEST_CASE("release notes become plain text") {
    const auto release = PickRelease(LATEST, false);
    REQUIRE(release);
    CHECK(PlainNotes(release->notes) == "The first release of EmuDEN.\n\n"
                                        "- Windows: EmuDEN-windows-x64.zip. Unzip it and run emuden.exe.\n"
                                        "- Android (arm64): EmuDEN-android-arm64.apk.\n\n"
                                        "SHA256SUMS.txt has the checksums.");
    CHECK(PlainNotes("## What's new\n\n\n* Faster [saves](https://example.com/x) for snake_case\n```\ncode\n```") ==
          "What's new\n\n- Faster saves for snake_case\ncode");
    CHECK(PlainNotes("").empty());
}

TEST_CASE("release dates read as words") {
    CHECK(DateText("2026-11-16T22:41:07Z") == "16 November 2026");
    CHECK(DateText("2026-01-02") == "2 January 2026");
    CHECK(DateText("soon") == "soon");
    CHECK(DateText("2026-13-01") == "2026-13-01");
}

TEST_CASE("automatic checks happen once a day") {
    const std::int64_t day = CHECK_INTERVAL;
    const std::int64_t now = 1'800'000'000;
    CHECK(CheckDue(0, now));
    CHECK(CheckDue(now - day, now));
    CHECK(CheckDue(now - day - 1, now));
    CHECK_FALSE(CheckDue(now - day + 1, now));
    CHECK_FALSE(CheckDue(now, now));
    CHECK_FALSE(CheckDue(now - 60, now));
    // A clock set back past the last check does not wait for it to come round again.
    CHECK(CheckDue(now + 3600, now));
    CHECK(CheckDue(now - 10, now, 5));
}

TEST_CASE("the update source is the build's repository") {
    // The test build is configured with the default.
    CHECK(UpdateRepo() == DEFAULT_REPO);
    CHECK(LatestUrl("Denarmo/emuden") == "https://api.github.com/repos/Denarmo/emuden/releases/latest");
    CHECK(ListUrl("Denarmo/emuden") == "https://api.github.com/repos/Denarmo/emuden/releases?per_page=10");
    CHECK(ReleasesPage("Denarmo/emuden") == "https://github.com/Denarmo/emuden/releases");
    CHECK(LatestUrl("someone/emuden-releases") ==
          "https://api.github.com/repos/someone/emuden-releases/releases/latest");
    CHECK(IsRepoName("Denarmo/emuden"));
    CHECK(IsRepoName("some_one/emu.den-releases"));
    CHECK_FALSE(IsRepoName("emuden"));
    CHECK_FALSE(IsRepoName("/emuden"));
    CHECK_FALSE(IsRepoName("Denarmo/"));
    CHECK_FALSE(IsRepoName("a/b/c"));
    CHECK_FALSE(IsRepoName("../emuden"));
    CHECK_FALSE(IsRepoName("a/b?x=1"));
    CHECK_FALSE(IsRepoName("a b/c"));
}
