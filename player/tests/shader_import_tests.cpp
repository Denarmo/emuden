// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <random>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/player/shader_import.h"
#include "zip_writer.h"

using namespace den;
using namespace den::player;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path dir;
    TempDir() {
        dir = fs::temp_directory_path() / ("den_shader_import_" + std::to_string(std::random_device{}()));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(dir, ignored);
    }
};

void WriteFile(const fs::path& file, const std::string& data) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << data;
}

} // namespace

TEST_CASE("GitHub shader links") {
    auto source = GithubShaderSource("https://GitHub.com/libretro/slang-shaders");
    REQUIRE(source);
    CHECK(source->url == "https://codeload.github.com/libretro/slang-shaders/zip/HEAD");
    CHECK(source->name == "slang-shaders");
    source = GithubShaderSource("github.com/owner/repo.git/tree/dev");
    REQUIRE(source);
    CHECK(source->url == "https://codeload.github.com/owner/repo/zip/dev");
    CHECK(source->name == "repo");
    CHECK(GithubShaderSource("owner/repo"));
    CHECK_FALSE(GithubShaderSource("owner"));
    CHECK_FALSE(GithubShaderSource("own er/repo"));
    CHECK(ShaderFolderName("..my shaders!") == "myshaders");
    CHECK(ShaderFolderName(std::string(80, 'a')).size() == 60);
}

TEST_CASE("a shader zip unpacks without its top folder and nothing outside it") {
    TempDir temp;
    const fs::path archive = temp.dir / "download.zip";
    WriteFile(archive, test::StoredZipFiles({
                           {"repo-main/crt/crt.slangp", "shaders = 1"},
                           {"repo-main/crt/shaders/crt.slang", "void main() {}"},
                           {"repo-main/../evil.txt", "x"},
                           {"repo-main/a/../../evil2.txt", "x"},
                           {"repo-main/C:/evil3.txt", "x"},
                           {"repo-main/a\\..\\evil4.txt", "x"},
                       }));
    const fs::path out = temp.dir / "work" / "files";
    REQUIRE(UnpackShaderZip(archive, out, [] { return false; }));
    CHECK(IsRegularFile(out / "crt" / "crt.slangp"));
    CHECK(IsRegularFile(out / "crt" / "shaders" / "crt.slang"));
    CHECK_FALSE(fs::exists(temp.dir / "work" / "evil.txt"));
    CHECK_FALSE(fs::exists(temp.dir / "evil2.txt"));
    CHECK(ListDirectory(out).size() == 1);

    WriteFile(temp.dir / "not.zip", "plain text");
    CHECK_FALSE(UnpackShaderZip(temp.dir / "not.zip", temp.dir / "x", [] { return false; }));
}

TEST_CASE("installing a shader folder replaces an earlier import and needs a preset") {
    TempDir temp;
    const fs::path dir = temp.dir / "shaders";
    WriteFile(dir / "crt" / "old.slangp", "old");
    WriteFile(temp.dir / "new" / "new.glslp", "new");
    REQUIRE(InstallShaderFolder(temp.dir / "new", dir, "crt"));
    CHECK(IsRegularFile(dir / "crt" / "new.glslp"));
    CHECK_FALSE(fs::exists(dir / "crt" / "old.slangp"));

    WriteFile(temp.dir / "empty" / "readme.txt", "no presets");
    const Status none = InstallShaderFolder(temp.dir / "empty", dir, "empty");
    CHECK_FALSE(none);
    CHECK(none.error.find("preset") != std::string::npos);

    Job job("copy");
    WriteFile(temp.dir / "mine" / "look.slangp", "mine");
    REQUIRE(CopyShaderFolder(temp.dir / "mine", "mine", dir, job));
    CHECK(IsRegularFile(dir / "mine" / "look.slangp"));
    CHECK(IsRegularFile(temp.dir / "mine" / "look.slangp"));
    CHECK_FALSE(fs::exists(dir / ".import"));
}
