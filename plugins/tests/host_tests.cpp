// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/sha256.h"
#include "den/plugins/index.h"
#include "den/plugins/installer.h"
#include "den/plugins/signature.h"

using namespace den;
using namespace den::plugins;
namespace fs = std::filesystem;

namespace {

void Put16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xff));
    out.push_back(static_cast<char>(v >> 8));
}

void Put32(std::string& out, std::uint32_t v) {
    Put16(out, static_cast<std::uint16_t>(v & 0xffff));
    Put16(out, static_cast<std::uint16_t>(v >> 16));
}

// A zip with one stored member.
std::string StoredZip(const std::string& name, const std::string& data) {
    const std::uint32_t crc = Crc32(data.data(), data.size());
    const auto size = static_cast<std::uint32_t>(data.size());
    const auto name_size = static_cast<std::uint16_t>(name.size());
    const auto fields = [&](std::string& out) {
        Put16(out, 20); // version needed
        Put16(out, 0);  // flags
        Put16(out, 0);  // stored
        Put32(out, 0);  // time and date
        Put32(out, crc);
        Put32(out, size);
        Put32(out, size);
        Put16(out, name_size);
        Put16(out, 0); // extra
    };
    std::string zip;
    Put32(zip, 0x04034b50);
    fields(zip);
    zip += name + data;
    const auto central_at = static_cast<std::uint32_t>(zip.size());
    Put32(zip, 0x02014b50);
    Put16(zip, 20); // made by
    fields(zip);
    Put16(zip, 0); // comment
    Put16(zip, 0); // disk
    Put16(zip, 0); // internal attributes
    Put32(zip, 0); // external attributes
    Put32(zip, 0); // local header offset
    zip += name;
    const auto central_size = static_cast<std::uint32_t>(zip.size()) - central_at;
    Put32(zip, 0x06054b50);
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, 1);
    Put16(zip, 1);
    Put32(zip, central_size);
    Put32(zip, central_at);
    Put16(zip, 0);
    return zip;
}

std::string Hash(const std::string& data) {
    Sha256 hash;
    hash.Update(data.data(), data.size());
    return hash.FinishHex();
}

// A plugin source served from memory.
struct FakeSource {
    std::map<std::string, std::string> files;
    int requests = 0;

    pal::HttpResponse Get(const pal::HttpRequest& request) {
        ++requests;
        pal::HttpResponse response;
        const auto it = files.find(request.url);
        if (it == files.end()) {
            response.status = 404;
            return response;
        }
        response.status = 200;
        response.received = it->second.size();
        if (request.progress && !request.progress(it->second.size(), it->second.size())) {
            response.error = "cancelled";
            return response;
        }
        if (request.to_file.empty()) {
            response.body = it->second;
        } else {
            std::ofstream(request.to_file, std::ios::binary) << it->second;
        }
        return response;
    }
};

const char* const BASE = "https://raw.githubusercontent.com/tester/plugins/main/";
const std::string CORE_BYTES = "this stands in for a libretro core";

struct Fixture {
    fs::path dir;
    Jobs jobs{1};
    FakeSource source;
    KeyPair key;
    std::string core_zip = StoredZip("testcore_libretro.dll", CORE_BYTES);
    std::int64_t now = *ParseUtcTime("2026-11-13T00:00:00Z");

    Fixture() {
        dir = fs::temp_directory_path() / ("den-plugins-test-" + std::to_string(std::rand()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        std::array<std::uint8_t, 32> seed{};
        seed[3] = 42;
        key = KeyPairFromSeed(seed);
        source.files["https://example.org/cores/testcore.zip"] = core_zip;
        Publish(1, "1.0.0", Hash(core_zip));
    }

    ~Fixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Signs and serves an index offering one plugin whose core download has `core_sha256`.
    void Publish(int sequence, const std::string& version, const std::string& core_sha256) {
        const std::string manifest =
            R"({"manifest_version":1,"id":"emuden.testcon","name":"Test Console","version":")" +
            version +
            R"(","kind":"console","engine":"libretro","publisher":"Tester","api":1,
            "needs":["engine.libretro"],
            "systems":[{"id":"testcon","name":"Test Console","short_name":"TC","group":"other",
              "order":100,"content":{"extensions":["TST"]},"presentation":{"cover_aspect":0.7},
              "cores":["missing","testcore"],
              "bios":[{"label":"Test BIOS","files":[{"path":"test.bin","optional":false}]}]}],
            "components":[{"id":"missing","type":"libretro-core"},
              {"id":"testcore","type":"libretro-core","name":"Test Core","gpu":"opengl",
               "forced_core_options":{"test_mode":"on"},
               "downloads":{"test-plat":{"url":"https://example.org/cores/testcore.zip",
                 "sha256":")" +
            core_sha256 + R"(","size":)" + std::to_string(core_zip.size()) +
            R"(,"archive":"zip","entry":"testcore_libretro.dll"}}}]})";
        const std::string package = StoredZip("plugin.json", manifest);
        const std::string package_path = "packages/emuden.testcon-" + version + ".emuplugin";
        const std::string index =
            R"({"index_version":1,"source":{"name":"Tester"},"sequence":)" +
            std::to_string(sequence) +
            R"(,"generated":"2026-11-12T00:00:00Z","expires":"2027-01-12T00:00:00Z",
            "plugins":[{"id":"emuden.testcon","name":"Test Console","version":")" +
            version + R"(","package":{"url":")" + package_path + R"(","sha256":")" +
            Hash(package) + R"(","size":)" + std::to_string(package.size()) +
            R"(}}],"revoked":[],"blocked_sha256":[]})";
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(index.data()), index.size());
        source.files[std::string(BASE) + package_path] = package;
        source.files[std::string(BASE) + "index.json"] = index;
        source.files[std::string(BASE) + "index.json.sig"] = SignEd25519(bytes, key);
    }

    std::unique_ptr<Host> MakeHost() {
        Config config;
        config.data_dir = dir;
        config.jobs = &jobs;
        HostDeps deps;
        deps.http = [this](const pal::HttpRequest& request) { return source.Get(request); };
        deps.source = "github:tester/plugins@main";
        deps.keys = {key.public_b64};
        deps.platform_key = "test-plat";
        deps.now = [this] { return now; };
        deps.bundled = false;
        return CreateHostWith(config, std::move(deps));
    }

    JobPtr Run(JobPtr job) {
        jobs.Wait(job);
        return job;
    }
};

} // namespace

TEST_CASE("a console is listed, installed, verified and removed") {
    Fixture f;
    auto host = f.MakeHost();
    CHECK(host->Consoles().empty());

    JobPtr refresh = f.Run(host->Refresh());
    INFO(refresh->Error());
    REQUIRE(refresh->State() == JobState::Done);
    const auto consoles = host->Consoles();
    REQUIRE(consoles.size() == 1);
    CHECK(consoles[0].id == "testcon");
    CHECK(consoles[0].short_name == "TC");
    CHECK(consoles[0].extensions == std::vector<std::string>{"tst"});
    CHECK(consoles[0].cover_aspect == 0.7f);
    CHECK(consoles[0].available);
    CHECK_FALSE(consoles[0].installed);
    CHECK_FALSE(host->Installed("testcon"));

    const std::uint64_t revision = host->Revision();
    JobPtr install = f.Run(host->Install("testcon"));
    INFO(install->Error());
    REQUIRE(install->State() == JobState::Done);
    CHECK(host->Revision() > revision);
    CHECK(host->Installed("testcon"));
    const auto core = host->CoreFor("testcon");
    REQUIRE(core);
    CHECK(core->id == "testcore");
    CHECK(core->installed);
    CHECK(core->needs_gl);
    CHECK(core->option_hints ==
          std::vector<std::pair<std::string, std::string>>{{"test_mode", "on"}});
    CHECK(core->missing_bios == std::vector<std::string>{"test.bin"});
    CHECK(core->library == f.dir / "cores" / "testcore_libretro.dll");
    CHECK(ReadTextFile(core->library) == std::optional<std::string>(CORE_BYTES));
    CHECK(host->VerifyCore(core->library).ok);
    CHECK(fs::is_empty(f.dir / "cores" / "tmp"));

    SECTION("a new host finds it without the network") {
        f.source.files.clear();
        auto second = f.MakeHost();
        CHECK(second->Installed("testcon"));
        CHECK(second->FindConsole("testcon")->installed);
    }
    SECTION("a changed core file is refused") {
        std::ofstream(core->library, std::ios::binary) << "something else entirely, not the core";
        CHECK_FALSE(host->VerifyCore(core->library).ok);
        CHECK_FALSE(host->Installed("testcon"));
        CHECK(host->CoreFor("testcon")->library.empty());
    }
    SECTION("a core outside the ledger is refused") {
        const fs::path stray = f.dir / "cores" / "stray_libretro.dll";
        std::ofstream(stray, std::ios::binary) << CORE_BYTES;
        CHECK_FALSE(host->VerifyCore(stray).ok);
    }
    SECTION("removing deletes the core") {
        JobPtr remove = f.Run(host->Remove("testcon"));
        REQUIRE(remove->State() == JobState::Done);
        CHECK_FALSE(host->Installed("testcon"));
        CHECK_FALSE(fs::exists(core->library));
    }
    SECTION("a newer manifest in the index replaces the old one") {
        const std::string other = StoredZip("testcore_libretro.dll", "a newer build of the core");
        f.core_zip = other;
        f.source.files["https://example.org/cores/testcore.zip"] = other;
        f.Publish(2, "1.1.0", Hash(other));
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
        CHECK(host->FindConsole("testcon")->update_available);
        REQUIRE(f.Run(host->Install("testcon"))->State() == JobState::Done);
        CHECK_FALSE(host->FindConsole("testcon")->update_available);
        CHECK(ReadTextFile(host->CoreFor("testcon")->library) ==
              std::optional<std::string>("a newer build of the core"));
    }
}

TEST_CASE("downloads that do not match are not installed") {
    Fixture f;
    auto host = f.MakeHost();
    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);

    SECTION("wrong checksum") {
        std::string other = f.core_zip;
        other[other.size() / 2] ^= 1;
        f.source.files["https://example.org/cores/testcore.zip"] = other;
    }
    SECTION("larger than stated") {
        f.source.files["https://example.org/cores/testcore.zip"] = f.core_zip + "more";
    }
    SECTION("missing") {
        f.source.files.erase("https://example.org/cores/testcore.zip");
    }
    JobPtr install = f.Run(host->Install("testcon"));
    CHECK(install->State() == JobState::Failed);
    CHECK_FALSE(install->Error().empty());
    CHECK_FALSE(host->Installed("testcon"));
    CHECK_FALSE(fs::exists(f.dir / "cores" / "testcore_libretro.dll"));
}

TEST_CASE("an index that cannot be trusted is not used") {
    Fixture f;
    auto host = f.MakeHost();
    const std::string index_url = std::string(BASE) + "index.json";

    SECTION("changed after signing") {
        std::string& index = f.source.files[index_url];
        index.replace(index.find("Tester"), 6, "Tamper");
        JobPtr refresh = f.Run(host->Refresh());
        CHECK(refresh->State() == JobState::Failed);
        CHECK(host->Consoles().empty());
    }
    SECTION("signed by another key") {
        std::array<std::uint8_t, 32> seed{};
        const KeyPair other = KeyPairFromSeed(seed);
        const std::string& index = f.source.files[index_url];
        f.source.files[index_url + ".sig"] = SignEd25519(
            {reinterpret_cast<const std::uint8_t*>(index.data()), index.size()}, other);
        CHECK(f.Run(host->Refresh())->State() == JobState::Failed);
        CHECK(host->Consoles().empty());
    }
    SECTION("older than the last one seen") {
        f.Publish(5, "1.0.0", Hash(f.core_zip));
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
        f.Publish(4, "1.0.0", Hash(f.core_zip));
        CHECK(f.Run(host->Refresh())->State() == JobState::Failed);
    }
    SECTION("expired: listed plugins stay, nothing new comes from it") {
        f.now = *ParseUtcTime("2027-06-01T00:00:00Z");
        CHECK(f.Run(host->Refresh())->State() == JobState::Done);
        CHECK(host->Consoles().empty());
    }
    SECTION("a package that is not the announced plugin") {
        const std::string package_url =
            std::string(BASE) + "packages/emuden.testcon-1.0.0.emuplugin";
        f.source.files[package_url][40] ^= 1;
        f.Run(host->Refresh());
        CHECK(host->Consoles().empty());
    }
}

// Needs the network, so it is hidden: run with  den_plugins_tests "[network]"
TEST_CASE("the official source installs the Game Boy and PSP cores", "[.network]") {
    const fs::path dir = fs::temp_directory_path() / "den-plugins-smoke";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    {
        Jobs jobs(2);
        Config config;
        config.data_dir = dir;
        config.jobs = &jobs;
        HostDeps deps;
        deps.platform_key = "windows-x86_64";
        auto host = CreateHostWith(config, std::move(deps));
        CHECK(host->Consoles().size() >= 19);

        JobPtr refresh = host->Refresh();
        jobs.Wait(refresh);
        INFO(refresh->Error());
        REQUIRE(refresh->State() == JobState::Done);
        CHECK(fs::exists(dir / "plugins" / "cache" / "index.json.sig"));

        for (const char* system : {"gb", "psp"}) {
            JobPtr install = host->Install(system);
            jobs.Wait(install);
            INFO(system << ": " << install->Error());
            REQUIRE(install->State() == JobState::Done);
            const auto core = host->CoreFor(system);
            REQUIRE(core);
            REQUIRE(core->installed);
            CHECK(host->VerifyCore(core->library).ok);
            WARN(system << ": " << PathToUtf8(core->library.filename()) << ", "
                        << fs::file_size(core->library) << " bytes");
        }
        // mGBA is shared: the other Game Boy consoles are ready too.
        CHECK(host->Installed("gba"));
        CHECK(host->Installed("gbc"));
        CHECK_FALSE(host->Installed("psx"));
    }
    fs::remove_all(dir, ec);
}
