// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Console plugins installed from a file, and the ones an update no longer brings.

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

// A zip of stored members.
std::string Zip(const std::vector<std::pair<std::string, std::string>>& members) {
    std::string zip, central;
    for (const auto& [name, data] : members) {
        const std::uint32_t crc = Crc32(data.data(), data.size());
        const auto offset = static_cast<std::uint32_t>(zip.size());
        const auto fields = [&](std::string& out) {
            Put16(out, 20);
            Put16(out, 0);
            Put16(out, 0);
            Put32(out, 0);
            Put32(out, crc);
            Put32(out, static_cast<std::uint32_t>(data.size()));
            Put32(out, static_cast<std::uint32_t>(data.size()));
            Put16(out, static_cast<std::uint16_t>(name.size()));
            Put16(out, 0);
        };
        Put32(zip, 0x04034b50);
        fields(zip);
        zip += name + data;
        Put32(central, 0x02014b50);
        Put16(central, 20);
        fields(central);
        Put16(central, 0);
        Put16(central, 0);
        Put16(central, 0);
        Put32(central, 0);
        Put32(central, offset);
        central += name;
    }
    const auto central_at = static_cast<std::uint32_t>(zip.size());
    zip += central;
    Put32(zip, 0x06054b50);
    Put16(zip, 0);
    Put16(zip, 0);
    Put16(zip, static_cast<std::uint16_t>(members.size()));
    Put16(zip, static_cast<std::uint16_t>(members.size()));
    Put32(zip, static_cast<std::uint32_t>(central.size()));
    Put32(zip, central_at);
    Put16(zip, 0);
    return zip;
}

std::string Hash(const std::string& data) {
    Sha256 hash;
    hash.Update(data.data(), data.size());
    return hash.FinishHex();
}

std::string Sign(const std::string& data, const KeyPair& key) {
    return SignEd25519(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()),
                       key);
}

const char* const BASE = "https://raw.githubusercontent.com/tester/plugins/main/";
const std::string CORE_BYTES = "this stands in for a libretro core";

// What the Switch 2 plugin is: a console whose games run in an engine program.
const std::string ENGINE_MANIFEST = R"({"manifest_version":1,"id":"emuden.switch2","name":"Nintendo Switch 2",
    "version":"1.0.2","kind":"console","engine":"switch2","publisher":"EmuDEN","api":1,"needs":["engine.switch2"],
    "systems":[{"id":"switch2","name":"Nintendo Switch 2","short_name":"Switch 2","group":"nintendo","order":0,
      "content":{"extensions":["nup","nsp","xci"],"scanner":"switch2","folder_aliases":["switch 2"],"zip":"no"}}],
    "components":[{"id":"nx2","type":"engine","name":"Switch 2 engine","delivery":{"test-plat":"manual"}}]})";

// The same plugin in an earlier version.
std::string Earlier(const std::string& manifest) {
    std::string out = manifest;
    out.replace(out.find("1.0.2"), 5, "1.0.1");
    return out;
}

struct Fixture {
    fs::path dir;
    Jobs jobs{1};
    KeyPair key, other_key;
    std::map<std::string, std::string> web;
    std::string core_zip = Zip({{"jcore_libretro.dll", CORE_BYTES}});
    std::int64_t now = *ParseUtcTime("2026-11-16T00:00:00Z");

    Fixture() {
        dir = fs::temp_directory_path() / ("den-local-plugins-" + std::to_string(std::rand()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        std::array<std::uint8_t, 32> seed{};
        seed[3] = 42;
        key = KeyPairFromSeed(seed);
        seed[3] = 7;
        other_key = KeyPairFromSeed(seed);
        web["https://example.org/jcore.zip"] = core_zip;
    }

    ~Fixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // A third party's console with a pinned core.
    std::string ThirdParty(const std::string& id) const {
        return R"({"manifest_version":1,"id":")" + id + R"(","name":"J Console","version":"1.0.0",
            "kind":"console","engine":"libretro","publisher":"J. Doe","api":1,"needs":["engine.libretro"],
            "systems":[{"id":"jcon","name":"J Console","content":{"extensions":["jjj"]},"cores":["jcore"]}],
            "components":[{"id":"jcore","type":"libretro-core","name":"J Core","downloads":{"test-plat":
              {"url":"https://example.org/jcore.zip","sha256":")" +
               Hash(core_zip) + R"(","size":)" + std::to_string(core_zip.size()) +
               R"(,"archive":"zip","entry":"jcore_libretro.dll"}}}]})";
    }

    fs::path Write(const std::string& name, const std::string& data) const {
        const fs::path path = dir / "picked" / name;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << data;
        return path;
    }

    // The official list, offering what is given (entries as the index tool writes them).
    void PublishIndex(int sequence, const std::string& entries) {
        const std::string index = R"({"index_version":1,"source":{"name":"Tester"},"sequence":)" +
                                  std::to_string(sequence) +
                                  R"(,"generated":"2026-11-15T00:00:00Z","expires":"2027-01-12T00:00:00Z",
            "plugins":[)" + entries + R"(],"revoked":[],"blocked_sha256":[]})";
        web[std::string(BASE) + "index.json"] = index;
        web[std::string(BASE) + "index.json.sig"] = Sign(index, key);
    }

    std::string Entry(const std::string& id, const std::string& version, const std::string& manifest,
                      const std::string& system) {
        const std::string package = Zip({{"plugin.json", manifest}});
        const std::string path = "packages/" + id + "-" + version + ".emuplugin";
        web[std::string(BASE) + path] = package;
        return R"({"id":")" + id + R"(","name":")" + id + R"(","version":")" + version +
               R"(","kind":"console","systems":[{"id":")" + system + R"(","name":"x"}],"package":{"url":")" + path +
               R"(","sha256":")" + Hash(package) + R"(","size":)" + std::to_string(package.size()) + "}}";
    }

    std::unique_ptr<Host> MakeHost() {
        Config config;
        config.data_dir = dir / "data";
        config.jobs = &jobs;
        HostDeps deps;
        deps.http = [this](const pal::HttpRequest& request) {
            pal::HttpResponse response;
            const auto it = web.find(request.url);
            response.status = it == web.end() ? 404 : 200;
            if (it != web.end()) {
                response.received = it->second.size();
                if (request.to_file.empty()) {
                    response.body = it->second;
                } else {
                    std::ofstream(request.to_file, std::ios::binary) << it->second;
                }
            }
            return response;
        };
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

TEST_CASE("a console plugin signed by EmuDEN installs from a file as one of its own") {
    Fixture f;
    auto host = f.MakeHost();
    CHECK(host->Consoles().empty()); // nothing of it until it is installed
    const fs::path package =
        f.Write("switch2.emuplugin", Zip({{"plugin.json", ENGINE_MANIFEST}, {"plugin.json.sig", Sign(ENGINE_MANIFEST, f.key)}}));
    Result<PackageProbe> probe = host->ProbePackage(package);
    INFO(probe.error());
    REQUIRE(probe.ok());
    CHECK(probe->official);
    CHECK(probe->engine);
    CHECK(probe->id == "emuden.switch2");
    CHECK(probe->version == "1.0.2");
    CHECK(probe->systems == std::vector<std::string>{"switch2"});
    CHECK(probe->consoles == std::vector<std::string>{"Nintendo Switch 2"});
    CHECK(probe->replaces.empty());
    REQUIRE(host->InstallPackage(*probe).ok);

    const auto console = host->FindConsole("switch2");
    REQUIRE(console);
    CHECK(console->runner == Runner::Engine);
    CHECK(console->installed);
    CHECK(console->source.empty()); // EmuDEN's own
    CHECK(console->scan.folder_aliases == std::vector<std::string>{"switch 2"});
    CHECK(IsRegularFile(f.dir / "data" / "plugins" / "emuden.switch2" / "plugin.json"));
    // The same file again is a reinstall of the version on the device.
    REQUIRE(host->ProbePackage(package).ok());
    CHECK(host->ProbePackage(package)->replaces == "1.0.2");

    // It stays after a restart, and the list not offering it does not take it away.
    f.PublishIndex(1, "");
    host = f.MakeHost();
    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
    REQUIRE(host->FindConsole("switch2"));
    CHECK(host->CarryOvers().empty());

    // Removing the console removes its plugin.
    REQUIRE(f.Run(host->Remove("switch2"))->State() == JobState::Done);
    CHECK_FALSE(host->FindConsole("switch2"));
    host = f.MakeHost();
    CHECK_FALSE(host->FindConsole("switch2"));
}

TEST_CASE("an unsigned console plugin is a third party's") {
    Fixture f;
    auto host = f.MakeHost();
    const std::string manifest = f.ThirdParty("jdoe.jcon");
    const fs::path package = f.Write("jcon.emuplugin", Zip({{"plugin.json", manifest}}));
    Result<PackageProbe> probe = host->ProbePackage(package);
    INFO(probe.error());
    REQUIRE(probe.ok());
    CHECK_FALSE(probe->official);
    CHECK_FALSE(probe->engine);
    REQUIRE(host->InstallPackage(*probe).ok);
    auto console = host->FindConsole("jcon");
    REQUIRE(console);
    CHECK(console->source == "a file");
    CHECK_FALSE(console->installed);
    CHECK(IsRegularFile(f.dir / "data" / "plugins-local" / "jdoe.jcon" / "plugin.json"));

    // Its core installs from the download its manifest pins, and survives a restart.
    JobPtr install = f.Run(host->Install("jcon"));
    INFO(install->Error());
    REQUIRE(install->State() == JobState::Done);
    CHECK(host->FindConsole("jcon")->installed);
    host = f.MakeHost();
    REQUIRE(host->FindConsole("jcon"));
    CHECK(host->FindConsole("jcon")->installed);
    REQUIRE(host->CoreFor("jcon"));
    CHECK(host->VerifyCore(host->CoreFor("jcon")->library).ok);

    // Signed with another key it is still unsigned to EmuDEN.
    const fs::path resigned =
        f.Write("jcon2.emuplugin", Zip({{"plugin.json", manifest}, {"plugin.json.sig", Sign(manifest, f.other_key)}}));
    REQUIRE(host->ProbePackage(resigned).ok());
    CHECK_FALSE(host->ProbePackage(resigned)->official);
    CHECK(host->ProbePackage(resigned)->replaces == "1.0.0");
}

TEST_CASE("an unsigned file may not be one of EmuDEN's plugins or run in its engine") {
    Fixture f;
    auto host = f.MakeHost();
    // The Switch 2 plugin without EmuDEN's signature.
    const fs::path bare = f.Write("bare.emuplugin", Zip({{"plugin.json", ENGINE_MANIFEST}}));
    CHECK_FALSE(host->ProbePackage(bare).ok());
    const fs::path other = f.Write(
        "other.emuplugin", Zip({{"plugin.json", ENGINE_MANIFEST}, {"plugin.json.sig", Sign(ENGINE_MANIFEST, f.other_key)}}));
    CHECK(host->ProbePackage(other).error().find("not signed by EmuDEN") != std::string::npos);
    // Signed for another text: the file was changed after it was signed.
    std::string changed = ENGINE_MANIFEST;
    changed.replace(changed.find("1.0.2"), 5, "9.9.9");
    const fs::path tampered =
        f.Write("tampered.emuplugin", Zip({{"plugin.json", changed}, {"plugin.json.sig", Sign(ENGINE_MANIFEST, f.key)}}));
    CHECK_FALSE(host->ProbePackage(tampered).ok());
    // A third party's id with EmuDEN's engine.
    std::string engine = ENGINE_MANIFEST;
    engine.replace(engine.find("emuden.switch2"), 14, "jdoe.switchtwo");
    CHECK_FALSE(host->ProbePackage(f.Write("engine.emuplugin", Zip({{"plugin.json", engine}}))).ok());
    CHECK(host->Consoles().empty());
}

TEST_CASE("a broken file is refused with a reason") {
    Fixture f;
    auto host = f.MakeHost();
    CHECK_FALSE(host->ProbePackage(f.dir / "nowhere.emuplugin").ok());
    CHECK_FALSE(host->ProbePackage(f.Write("text.emuplugin", "not a zip at all")).ok());
    CHECK_FALSE(host->ProbePackage(f.Write("empty.emuplugin", Zip({{"README.md", "hello"}}))).ok());
    const auto broken = host->ProbePackage(f.Write("broken.emuplugin", Zip({{"plugin.json", "{\"id\": "}})));
    REQUIRE_FALSE(broken.ok());
    CHECK(broken.error().find("cannot read") != std::string::npos);
    const std::string feature = R"({"manifest_version":1,"id":"jdoe.notes","name":"Notes","version":"1.0.0",
        "kind":"feature","api":1,"needs":[],"feature":{"entry":"main.lua","permissions":[]}})";
    CHECK_FALSE(host->ProbePackage(f.Write("feature.emuplugin", Zip({{"plugin.json", feature}}))).ok());
    CHECK(host->Consoles().empty());
}

TEST_CASE("a plugin folder installs through its plugin.json") {
    Fixture f;
    auto host = f.MakeHost();
    const fs::path manifest = f.Write("folder/plugin.json", ENGINE_MANIFEST);
    f.Write("folder/plugin.json.sig", Sign(ENGINE_MANIFEST, f.key));
    for (const fs::path& picked : {manifest, manifest.parent_path()}) {
        Result<PackageProbe> probe = host->ProbePackage(picked);
        INFO(probe.error());
        REQUIRE(probe.ok());
        CHECK(probe->official);
    }
    REQUIRE(host->InstallPackage(*host->ProbePackage(manifest)).ok);
    CHECK(host->FindConsole("switch2"));
}

TEST_CASE("a console plugin an update no longer brings stays where it was, never on a new device") {
    Fixture f;
    const std::string signed_package =
        Zip({{"plugin.json", ENGINE_MANIFEST}, {"plugin.json.sig", Sign(ENGINE_MANIFEST, f.key)}});
    f.web[std::string(BASE) + ManualPackagePath("emuden.switch2")] = signed_package;
    {
        // A new device: the list does not offer it, and nothing brings it.
        auto host = f.MakeHost();
        CHECK(host->CarryOvers().empty());
        f.PublishIndex(2, f.Entry("emuden.gbx", "1.0.0", f.ThirdParty("emuden.gbx"), "jcon"));
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
        CHECK_FALSE(host->FindConsole("switch2"));
        host = f.MakeHost();
        CHECK(host->CarryOvers().empty());
    }
    fs::remove_all(f.dir / "data");

    // An earlier version had the plugin built in, and its cached list offered it.
    const std::string old = f.Entry("emuden.switch2", "1.0.1", Earlier(ENGINE_MANIFEST), "switch2");
    f.PublishIndex(3, old);
    {
        auto host = f.MakeHost();
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
    }
    fs::remove_all(f.dir / "data" / "plugins" / "emuden.switch2"); // it was built in, not fetched
    fs::remove(f.dir / "data" / "plugins" / "carry-over.json");     // written by this version only
    f.PublishIndex(4, "");
    {
        auto host = f.MakeHost();
        const std::vector<CarryOver> carried = host->CarryOvers();
        REQUIRE(carried.size() == 1);
        CHECK(carried[0].plugin_id == "emuden.switch2");
        CHECK(carried[0].systems == std::vector<std::string>{"switch2"});
        CHECK_FALSE(host->FindConsole("switch2"));
        // Kept (the app keeps it where the library has its games): the signed package beside the list.
        JobPtr kept = f.Run(host->SettleCarryOver("emuden.switch2", true));
        INFO(kept->Error());
        REQUIRE(kept->State() == JobState::Done);
        REQUIRE(host->FindConsole("switch2"));
        CHECK(host->CarryOvers().empty());
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
    }
    {
        auto host = f.MakeHost();
        CHECK(host->FindConsole("switch2"));
        CHECK(host->CarryOvers().empty());
        // Let go, it is not brought back.
        REQUIRE(f.Run(host->Remove("switch2"))->State() == JobState::Done);
        host = f.MakeHost();
        CHECK_FALSE(host->FindConsole("switch2"));
        CHECK(host->CarryOvers().empty());
    }
}

TEST_CASE("a carried-over plugin with no games is let go") {
    Fixture f;
    f.PublishIndex(3, f.Entry("emuden.switch2", "1.0.1", Earlier(ENGINE_MANIFEST), "switch2"));
    {
        auto host = f.MakeHost();
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
    }
    fs::remove_all(f.dir / "data" / "plugins" / "emuden.switch2");
    fs::remove(f.dir / "data" / "plugins" / "carry-over.json");
    f.PublishIndex(4, "");
    auto host = f.MakeHost();
    REQUIRE(host->CarryOvers().size() == 1);
    CHECK_FALSE(host->CarryOvers()[0].installed);
    // A package that is not signed by EmuDEN is not kept.
    f.web[std::string(BASE) + ManualPackagePath("emuden.switch2")] = Zip({{"plugin.json", ENGINE_MANIFEST}});
    CHECK(f.Run(host->SettleCarryOver("emuden.switch2", true))->State() == JobState::Failed);
    CHECK(host->CarryOvers().size() == 1);
    REQUIRE(f.Run(host->SettleCarryOver("emuden.switch2", false))->State() == JobState::Done);
    CHECK(host->CarryOvers().empty());
    host = f.MakeHost();
    CHECK(host->CarryOvers().empty());
    CHECK_FALSE(host->FindConsole("switch2"));
}

TEST_CASE("a carried-over plugin the list had brought stays and is brought up to date") {
    Fixture f;
    // An earlier version had fetched the plugin's manifest from the list.
    f.PublishIndex(3, f.Entry("emuden.switch2", "1.0.1", Earlier(ENGINE_MANIFEST), "switch2"));
    {
        auto host = f.MakeHost();
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
        REQUIRE(host->FindConsole("switch2"));
    }
    fs::remove(f.dir / "data" / "plugins" / "carry-over.json");
    f.PublishIndex(4, "");
    const std::string package = Zip({{"plugin.json", ENGINE_MANIFEST}, {"plugin.json.sig", Sign(ENGINE_MANIFEST, f.key)}});
    SECTION("no package beside the list: the copy stays") {
        auto host = f.MakeHost();
        REQUIRE(host->CarryOvers().size() == 1);
        CHECK(host->CarryOvers()[0].installed);
        REQUIRE(f.Run(host->SettleCarryOver("emuden.switch2", true))->State() == JobState::Done);
        CHECK(host->CarryOvers().empty());
        REQUIRE(host->FindConsole("switch2"));
        CHECK(host->ProbePackage(f.Write("p.emuplugin", package))->replaces == "1.0.1");
    }
    SECTION("the signed package replaces it") {
        f.web[std::string(BASE) + ManualPackagePath("emuden.switch2")] = package;
        auto host = f.MakeHost();
        REQUIRE(host->CarryOvers().size() == 1);
        REQUIRE(f.Run(host->SettleCarryOver("emuden.switch2", true))->State() == JobState::Done);
        host = f.MakeHost();
        REQUIRE(host->FindConsole("switch2"));
        CHECK(host->ProbePackage(f.Write("p.emuplugin", package))->replaces == "1.0.2");
        CHECK(host->CarryOvers().empty());
    }
}

#ifdef DEN_EMUDEN_PLUGINS_DIR
TEST_CASE("the Switch 2 package of an emuden-plugins checkout is signed with EmuDEN's key") {
    const fs::path package = fs::path(DEN_EMUDEN_PLUGINS_DIR) / ManualPackagePath("emuden.switch2");
    if (!IsRegularFile(package)) {
        SKIP("no manual package in " DEN_EMUDEN_PLUGINS_DIR);
    }
    Fixture f;
    Config config;
    config.data_dir = f.dir / "official";
    config.jobs = &f.jobs;
    HostDeps deps;
    deps.http = [](const pal::HttpRequest&) { return pal::HttpResponse{}; };
    deps.bundled = false;
    auto host = CreateHostWith(config, std::move(deps)); // the official keys
    Result<PackageProbe> probe = host->ProbePackage(package);
    INFO(probe.error());
    REQUIRE(probe.ok());
    CHECK(probe->official);
    CHECK(probe->engine);
    CHECK(probe->systems == std::vector<std::string>{"switch2"});
}
#endif
