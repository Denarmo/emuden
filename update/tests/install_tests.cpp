// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <map>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/sha256.h"
#include "den/update/portable.h"
#include "den/update/updater.h"
#include "zip_writer.h"

using namespace den;
using namespace den::update;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("emuden-update-test-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void Write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    REQUIRE(WriteFileAtomic(file, text));
}

std::string Read(const fs::path& file) {
    return ReadTextFile(file).value_or("<missing>");
}

std::string Hex(const std::string& data) {
    Sha256 hash;
    hash.Update(data.data(), data.size());
    return hash.FinishHex();
}

// An old portable folder with its data, and a staged new version.
struct Install {
    fs::path root, install, staged, previous;
    explicit Install(const std::string& name) : root(FreshDir(name)) {
        install = root / "EmuDEN";
        staged = root / "staged";
        previous = root / "previous";
        Write(install / "emuden.exe", "old exe");
        Write(install / "assets" / "fonts" / "a.ttf", "old font");
        Write(install / "assets" / "old-only.txt", "old only");
        Write(install / "licences" / "EmuDEN.txt", "old licence");
        Write(install / "user" / "settings.json", "my settings");
        Write(install / "notes.txt", "kept: not in the new version");
        Write(staged / "emuden.exe", "new exe");
        Write(staged / "assets" / "fonts" / "a.ttf", "new font");
        Write(staged / "licences" / "EmuDEN.txt", "new licence");
        Write(staged / "user" / "settings.json", "must not replace mine");
    }
};

// A release served from memory: name to body.
struct FakeServer {
    std::map<std::string, std::string> files;
    int requests = 0;
    pal::HttpResponse operator()(const pal::HttpRequest& request) {
        ++requests;
        pal::HttpResponse response;
        const auto it = files.find(request.url);
        if (it == files.end()) {
            response.status = 404;
            return response;
        }
        response.status = 200;
        if (request.to_file.empty()) {
            response.body = it->second;
            return response;
        }
        if (request.progress && !request.progress(it->second.size(), it->second.size())) {
            response.status = 0;
            response.error = "cancelled";
            return response;
        }
        WriteFileAtomic(request.to_file, it->second);
        return response;
    }
};

Release ReleaseWith(const std::string& data, const std::string& digest = {}) {
    Release release;
    release.version = "0.2.0";
    release.assets.push_back({"EmuDEN-x86_64.AppImage", "https://example.test/EmuDEN-x86_64.AppImage",
                              data.size(), digest});
    release.assets.push_back({SUMS_NAME, "https://example.test/SHA256SUMS.txt", 100, {}});
    return release;
}

} // namespace

TEST_CASE("the swap moves the new version in and keeps the user folder") {
    Install dir("swap");
    REQUIRE(ApplySwap(dir.install, dir.staged, dir.previous).ok);
    CHECK(Read(dir.install / "emuden.exe") == "new exe");
    CHECK(Read(dir.install / "assets" / "fonts" / "a.ttf") == "new font");
    CHECK(Read(dir.install / "licences" / "EmuDEN.txt") == "new licence");
    CHECK(Read(dir.install / "user" / "settings.json") == "my settings");
    CHECK(Read(dir.install / "notes.txt") == "kept: not in the new version");
    // The folders it replaced whole go aside, with what only the old one had.
    CHECK_FALSE(fs::exists(dir.install / "assets" / "old-only.txt"));
    CHECK(Read(dir.previous / "emuden.exe") == "old exe");
    CHECK(Read(dir.previous / "assets" / "old-only.txt") == "old only");
    CHECK_FALSE(fs::exists(dir.previous / "user"));
    CHECK(Read(dir.staged / "user" / "settings.json") == "must not replace mine");
}

TEST_CASE("a failed swap puts the old version back") {
    Install dir("rollback");
    int moves = 0;
    // The new licences folder cannot be moved in, after the exe and assets went through.
    const Mover failing = [&](const fs::path& from, const fs::path& to) {
        ++moves;
        if (from == dir.staged / "licences") {
            return false;
        }
        std::error_code ec;
        fs::rename(from, to, ec);
        return !ec;
    };
    const Status status = ApplySwap(dir.install, dir.staged, dir.previous, failing);
    REQUIRE_FALSE(status.ok);
    CHECK(status.error.find("licences") != std::string::npos);
    CHECK(Read(dir.install / "emuden.exe") == "old exe");
    CHECK(Read(dir.install / "assets" / "fonts" / "a.ttf") == "old font");
    CHECK(Read(dir.install / "assets" / "old-only.txt") == "old only");
    CHECK(Read(dir.install / "licences" / "EmuDEN.txt") == "old licence");
    CHECK(Read(dir.install / "user" / "settings.json") == "my settings");
    // The new files are back in staging for another try.
    CHECK(Read(dir.staged / "emuden.exe") == "new exe");
    CHECK(Read(dir.staged / "licences" / "EmuDEN.txt") == "new licence");
    CHECK(moves > 4);
}

TEST_CASE("an empty staging folder changes nothing") {
    Install dir("empty");
    fs::remove_all(dir.staged);
    fs::create_directories(dir.staged / "user");
    CHECK_FALSE(ApplySwap(dir.install, dir.staged, dir.previous).ok);
    CHECK(Read(dir.install / "emuden.exe") == "old exe");
}

TEST_CASE("the release zip is staged with PowerShell's backslashes") {
    const fs::path dir = FreshDir("stage");
    const fs::path zip = dir / "EmuDEN-windows-x64.zip";
    Write(zip, test::StoredZipFiles({{"emuden.exe", "new exe"},
                                     {"assets\\fonts\\a.ttf", "font"},
                                     {"assets\\plugins\\", ""},
                                     {"user\\settings.json", "not mine"}}));
    const fs::path staged = dir / "staged";
    Write(staged / "stale.txt", "from an earlier try");
    REQUIRE(StageZip(zip, staged).ok);
    CHECK(Read(staged / "emuden.exe") == "new exe");
    CHECK(Read(staged / "assets" / "fonts" / "a.ttf") == "font");
    CHECK_FALSE(fs::exists(staged / "user"));
    CHECK_FALSE(fs::exists(staged / "stale.txt"));
}

TEST_CASE("a zip with the program in one folder is staged from inside it") {
    const fs::path dir = FreshDir("stage-folder");
    const fs::path zip = dir / "a.zip";
    Write(zip, test::StoredZipFiles({{"EmuDEN/emuden.exe", "exe"}, {"EmuDEN/assets/x.txt", "x"}}));
    REQUIRE(StageZip(zip, dir / "staged").ok);
    CHECK(Read(dir / "staged" / "emuden.exe") == "exe");
    CHECK(Read(dir / "staged" / "assets" / "x.txt") == "x");
}

TEST_CASE("zips without the program or with unsafe names are refused") {
    const fs::path dir = FreshDir("stage-bad");
    Write(dir / "none.zip", test::StoredZipFiles({{"readme.txt", "hi"}}));
    CHECK_FALSE(StageZip(dir / "none.zip", dir / "staged").ok);
    Write(dir / "escape.zip", test::StoredZipFiles({{"emuden.exe", "exe"}, {"../evil.txt", "x"}}));
    CHECK_FALSE(StageZip(dir / "escape.zip", dir / "staged").ok);
    CHECK_FALSE(fs::exists(dir / "evil.txt"));
    Write(dir / "drive.zip", test::StoredZipFiles({{"emuden.exe", "exe"}, {"C:\\evil.txt", "x"}}));
    CHECK_FALSE(StageZip(dir / "drive.zip", dir / "staged").ok);
    Write(dir / "notzip.zip", "plain text");
    CHECK_FALSE(StageZip(dir / "notzip.zip", dir / "staged").ok);
}

TEST_CASE("the helper's result is read once") {
    const fs::path dir = FreshDir("result");
    const PortableLayout layout = LayoutFor(dir / "EmuDEN" / "");
    CHECK(layout.install == dir / "EmuDEN");
    CHECK(layout.staged == dir / "EmuDEN" / ".update" / "staged");
    Write(layout.result, "failed\n0.2.0\nemuden.exe is in use and could not be replaced.\n");
    const auto result = TakeHelperResult(layout);
    REQUIRE(result);
    CHECK_FALSE(result->ok);
    CHECK(result->version == "0.2.0");
    CHECK(result->error == "emuden.exe is in use and could not be replaced.");
    CHECK_FALSE(TakeHelperResult(layout));
}

TEST_CASE("downloads are checked against SHA256SUMS.txt") {
    const fs::path dir = FreshDir("download");
    const std::string data = "the new AppImage";
    FakeServer server;
    server.files["https://example.test/EmuDEN-x86_64.AppImage"] = data;
    server.files["https://example.test/SHA256SUMS.txt"] = Hex(data) + "  EmuDEN-x86_64.AppImage\n";
    Source source;
    source.http = [&](const pal::HttpRequest& request) { return server(request); };
    const fs::path file = dir / "new.AppImage";

    REQUIRE(DownloadAsset(source, ReleaseWith(data, Hex(data)), "EmuDEN-x86_64.AppImage", file).ok);
    CHECK(Read(file) == data);
    // Already there and right: not downloaded again (only the checksums are).
    const int before = server.requests;
    REQUIRE(DownloadAsset(source, ReleaseWith(data), "EmuDEN-x86_64.AppImage", file).ok);
    CHECK(server.requests == before + 1);

    SECTION("a file that does not match is not kept") {
        server.files["https://example.test/SHA256SUMS.txt"] = Hex("other") + "  EmuDEN-x86_64.AppImage\n";
        CHECK_FALSE(DownloadAsset(source, ReleaseWith(data), "EmuDEN-x86_64.AppImage", file).ok);
        CHECK_FALSE(fs::exists(file));
    }
    SECTION("GitHub's digest must agree with the sums") {
        CHECK_FALSE(DownloadAsset(source, ReleaseWith(data, Hex("other")), "EmuDEN-x86_64.AppImage", file).ok);
    }
    SECTION("a file without a line in the sums is refused") {
        server.files["https://example.test/SHA256SUMS.txt"] = Hex(data) + "  something-else\n";
        CHECK_FALSE(DownloadAsset(source, ReleaseWith(data), "EmuDEN-x86_64.AppImage", dir / "b").ok);
        CHECK_FALSE(fs::exists(dir / "b"));
    }
    SECTION("a larger file than the release says is stopped") {
        Release release = ReleaseWith(data);
        release.assets[0].size = 4;
        CHECK_FALSE(DownloadAsset(source, release, "EmuDEN-x86_64.AppImage", dir / "c").ok);
        CHECK_FALSE(fs::exists(dir / "c"));
    }
    SECTION("plain http is refused") {
        Release release = ReleaseWith(data);
        release.assets[0].url = "http://example.test/EmuDEN-x86_64.AppImage";
        CHECK_FALSE(DownloadAsset(source, release, "EmuDEN-x86_64.AppImage", dir / "d").ok);
    }
}

TEST_CASE("the check asks GitHub once and reads its answer") {
    std::vector<std::string> asked;
    Source source;
    source.http = [&](const pal::HttpRequest& request) {
        asked.push_back(request.url);
        for (const auto& [name, value] : request.headers) {
            CHECK(name != "Authorization");
        }
        pal::HttpResponse response;
        response.status = 200;
        response.body = R"({"tag_name": "v0.2.0", "body": "Notes", "assets": []})";
        return response;
    };
    auto result = FetchLatest(source, false);
    REQUIRE(result);
    REQUIRE(result->release);
    CHECK(result->release->version == "0.2.0");
    CHECK(asked == std::vector<std::string>{LatestUrl(UpdateRepo())});
    REQUIRE(FetchLatest(source, true));
    CHECK(asked.back() == ListUrl(UpdateRepo()));
    source.repo = "someone/emuden-releases";
    REQUIRE(FetchLatest(source, false));
    CHECK(asked.back() == "https://api.github.com/repos/someone/emuden-releases/releases/latest");

    source.http = [](const pal::HttpRequest&) {
        pal::HttpResponse response;
        response.status = 403;
        return response;
    };
    CHECK_FALSE(FetchLatest(source, false));
}

TEST_CASE("an AppImage is replaced by a rename beside it") {
    const fs::path dir = FreshDir("appimage");
    Write(dir / "EmuDEN-x86_64.AppImage", "old");
    Write(dir / ".EmuDEN-x86_64.AppImage.new", "new");
    REQUIRE(ReplaceAppImage(dir / ".EmuDEN-x86_64.AppImage.new", dir / "EmuDEN-x86_64.AppImage").ok);
    CHECK(Read(dir / "EmuDEN-x86_64.AppImage") == "new");
    CHECK_FALSE(fs::exists(dir / ".EmuDEN-x86_64.AppImage.new"));
    Write(dir / "sub" / "x", "new");
    CHECK_FALSE(ReplaceAppImage(dir / "sub" / "x", dir / "EmuDEN-x86_64.AppImage").ok);
}

TEST_CASE("test overrides apply only to development builds and this device") {
    // A release build ignores both, so they can never redirect its updates.
    const TestOverrides release = AllowedTestOverrides("0.2.0", "0.0.9", "http://127.0.0.1:8765/latest.json");
    CHECK(release.version.empty());
    CHECK(release.url.empty());

    const TestOverrides dev = AllowedTestOverrides("", "0.0.9", "http://127.0.0.1:8765/latest.json");
    CHECK(dev.version == "0.0.9");
    CHECK(dev.url == "http://127.0.0.1:8765/latest.json");
    CHECK(AllowedTestOverrides("", "", "https://localhost/x").url == "https://localhost/x");
    CHECK(AllowedTestOverrides("", "", "http://[::1]:80/x").url == "http://[::1]:80/x");
    CHECK(AllowedTestOverrides("", "", "https://evil.example/latest.json").url.empty());
    CHECK(AllowedTestOverrides("", "", "https://api.github.com/repos/x/y/releases/latest").url.empty());
}

TEST_CASE("loopback addresses are told from look-alikes") {
    CHECK(IsLoopbackUrl("http://127.0.0.1/latest.json"));
    CHECK(IsLoopbackUrl("http://127.0.0.1:8765"));
    CHECK(IsLoopbackUrl("https://LOCALHOST:443/a?b#c"));
    CHECK(IsLoopbackUrl("http://[::1]:8765/a"));
    CHECK(IsLoopbackUrl("http://[::1]/a"));
    CHECK_FALSE(IsLoopbackUrl("http://127.0.0.1@evil.example/"));
    CHECK_FALSE(IsLoopbackUrl("http://localhost.evil.example/"));
    CHECK_FALSE(IsLoopbackUrl("http://127.0.0.1.evil.example/"));
    CHECK_FALSE(IsLoopbackUrl("http://evil.example/?127.0.0.1"));
    CHECK_FALSE(IsLoopbackUrl("http://127.0.0.1:80x/"));
    CHECK_FALSE(IsLoopbackUrl("http://[::1]x/"));
    CHECK_FALSE(IsLoopbackUrl("http://127.0.0.1\\@evil.example/"));
    CHECK_FALSE(IsLoopbackUrl("ftp://127.0.0.1/"));
    CHECK_FALSE(IsLoopbackUrl("file:///etc/passwd"));
    CHECK_FALSE(IsLoopbackUrl(""));
}

TEST_CASE("loopback downloads only with the test allowance") {
    const std::string data = "zip";
    Release release = ReleaseWith(data);
    release.assets[0].url = "http://127.0.0.1:8765/EmuDEN-x86_64.AppImage";
    release.assets[1].url = "http://127.0.0.1:8765/SHA256SUMS.txt";
    FakeServer server;
    server.files[release.assets[0].url] = data;
    server.files[release.assets[1].url] = Hex(data) + "  EmuDEN-x86_64.AppImage\n";
    Source source;
    source.http = [&](const pal::HttpRequest& request) { return server(request); };
    const fs::path dir = FreshDir("loopback");
    CHECK_FALSE(DownloadAsset(source, release, "EmuDEN-x86_64.AppImage", dir / "a").ok);
    source.allow_loopback = true;
    CHECK(DownloadAsset(source, release, "EmuDEN-x86_64.AppImage", dir / "a").ok);
}
