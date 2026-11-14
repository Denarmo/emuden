// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin sources besides the official one: adding one with its key pinned on first use,
// their rules, the merged catalogue, a withdrawn key, removing a source.

#include <array>

#include <catch2/catch_test_macros.hpp>

#include "den/plugins/feature_host.h"
#include "den/plugins/installer.h"
#include "den/plugins/signature.h"
#include "den/plugins/sources.h"
#include "den/store/settings.h"
#include "feature/index_source.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::testing;

namespace {

const std::string OFFICIAL = "https://raw.githubusercontent.com/tester/plugins/main/";
const std::string JDOE = "https://raw.githubusercontent.com/jdoe/consoles/main/";
const std::string MORE = "https://raw.githubusercontent.com/jdoe/more/main/";

KeyPair KeyOf(std::uint8_t n) {
    std::array<std::uint8_t, 32> seed{};
    seed[0] = n;
    return KeyPairFromSeed(seed);
}

struct Fixture {
    ScratchDir scratch;
    Jobs jobs{2};
    FakeWeb web;
    std::unique_ptr<store::Settings> settings;
    std::int64_t now = *ParseUtcTime("2026-11-13T00:00:00Z");
    KeyPair official = KeyOf(1), jdoe = KeyOf(2), more = KeyOf(3);

    Fixture() {
        settings = std::make_unique<store::Settings>(scratch.dir / "settings.json");
        Serve(OFFICIAL, official, 1, {Console(OFFICIAL, "emuden.testcon", "testcon", "testcore")});
    }

    // A console plugin's package and its core, served beside `base`; the index entry.
    std::string Console(const std::string& base, const std::string& id, const std::string& system,
                        const std::string& core, const std::string& engine = "libretro",
                        const std::string& version = "1.0.0") {
        const std::string core_zip = StoredZipOf({{core + "_libretro.dll", "core " + id + "/" + core}});
        const std::string core_url = "https://example.org/" + id + "/" + core + ".zip";
        web.files[core_url] = core_zip;
        const std::string manifest =
            R"({"manifest_version":1,"id":")" + id + R"(","name":")" + id + R"(","version":")" + version +
            R"(","kind":"console","engine":"libretro","publisher":"P","api":1,"needs":["engine.libretro"],
            "systems":[{"id":")" + system + R"(","name":"Console )" + system + R"(","short_name":"C",
              "group":"other","order":100,"content":{"extensions":["c)" + system + R"("]},
              "presentation":{"cover_aspect":1},"cores":[")" + core + R"("]}],
            "components":[{"id":")" + core + R"(","type":"libretro-core","name":")" + core + R"(",
              "downloads":{"test-plat":{"url":")" + core_url + R"(","sha256":")" + HashOf(core_zip) +
            R"(","size":)" + std::to_string(core_zip.size()) + R"(,"archive":"zip","entry":")" + core +
            R"(_libretro.dll"}}}]})";
        const std::string package = StoredZipOf({{"plugin.json", manifest}});
        const std::string path = "packages/" + id + "-" + version + ".emuplugin";
        web.files[base + path] = package;
        return R"({"id":")" + id + R"(","name":")" + id + R"(","version":")" + version +
               R"(","kind":"console","engine":")" + engine + R"(","package":{"url":")" + path +
               R"(","sha256":")" + HashOf(package) + R"(","size":)" + std::to_string(package.size()) + "}}";
    }

    // A feature plugin's package; the index entry.
    std::string Feature(const std::string& base, const std::string& id) {
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Notes","version":"1.0.0","kind":"feature","api":1,
            "needs":["kind.feature","feature.lua.1"],"publisher":"J","summary":"Notes.",
            "feature":{"entry":"main.lua","permissions":[]}})";
        const std::string package =
            StoredZipOf({{"plugin.json", manifest}, {"main.lua", "return { actions = {} }"}});
        const std::string path = "packages/" + id + "-1.0.0.emuplugin";
        web.files[base + path] = package;
        return R"({"id":")" + id + R"(","name":"Notes","version":"1.0.0","kind":"feature","publisher":"J",
               "summary":"Notes.","package":{"url":")" + path + R"(","sha256":")" + HashOf(package) +
               R"(","size":)" + std::to_string(package.size()) + "}}";
    }

    void Serve(const std::string& base, const KeyPair& key, int sequence, const std::vector<std::string>& entries,
               const std::string& extra = {}) {
        std::string plugins;
        for (const std::string& entry : entries) {
            plugins += (plugins.empty() ? "" : ",") + entry;
        }
        const std::string index = R"({"index_version":1,"source":{"name":"Some list"},"sequence":)" +
                                  std::to_string(sequence) +
                                  R"(,"generated":"2026-11-12T00:00:00Z","expires":"2027-01-12T00:00:00Z",
                                  "plugins":[)" + plugins + R"(],"revoked":[],"blocked_sha256":[])" + extra + "}";
        const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(index.data()), index.size());
        web.files[base + "index.json"] = index;
        web.files[base + "index.json.sig"] = SignEd25519(bytes, key);
        web.files[base + "keys/source.pub"] = key.public_b64 + "\n";
    }

    std::unique_ptr<Host> MakeHost() {
        Config config;
        config.data_dir = scratch.dir / "data";
        config.jobs = &jobs;
        config.settings = settings.get();
        HostDeps deps;
        deps.http = web.Getter();
        deps.source = "github:tester/plugins@main";
        deps.keys = {official.public_b64};
        deps.platform_key = "test-plat";
        deps.now = [this] { return now; };
        deps.bundled = false;
        return CreateHostWith(config, std::move(deps));
    }

    JobPtr Run(JobPtr job) {
        jobs.Wait(job);
        return job;
    }

    // Probes the source as the user typed it; the probe, or the job's error.
    std::pair<SourceProbe, std::string> Probe(Host& host, const std::string& text) {
        auto probe = std::make_shared<SourceProbe>();
        const JobPtr job = Run(host.ProbeSource(text, probe));
        return {*probe, job->State() == JobState::Done ? std::string() : job->Error()};
    }

    void Add(Host& host, const std::string& text) {
        const auto [probe, error] = Probe(host, text);
        REQUIRE(error == "");
        REQUIRE(host.AddSource(probe).ok);
    }
};

std::optional<SourceInfo> SourceOf(const Host& host, const std::string& label) {
    for (SourceInfo& info : host.Sources()) {
        if (info.label == label) {
            return info;
        }
    }
    return std::nullopt;
}

bool HasNote(const SourceInfo& info, const std::string& part) {
    return std::any_of(info.notes.begin(), info.notes.end(),
                       [&](const std::string& note) { return note.find(part) != std::string::npos; });
}

} // namespace

TEST_CASE("source addresses, fingerprints and the settings line") {
    auto request = ParseSourceRequest("jdoe/Consoles");
    REQUIRE(request);
    CHECK(request->address.text == "github:jdoe/Consoles@main");
    CHECK(request->address.label == "jdoe/Consoles");
    CHECK(request->address.ns == "jdoe");
    CHECK(request->address.key_url == "https://raw.githubusercontent.com/jdoe/Consoles/main/keys/source.pub");
    CHECK(request->fingerprint.empty());

    request = ParseSourceRequest("https://github.com/JDoe/consoles/tree/dev#3FA9 12c4 77e0 b5d1");
    REQUIRE(request);
    CHECK(request->address.text == "github:JDoe/consoles@dev");
    CHECK(request->address.label == "JDoe/consoles@dev");
    CHECK(request->address.ns == "jdoe");
    CHECK(request->fingerprint == "3fa912c477e0b5d1");

    request = ParseSourceRequest("https://Plugins.Example.org/emu/index.json");
    REQUIRE(request);
    CHECK(request->address.text == "https://plugins.example.org/emu/");
    CHECK(request->address.index_url == "https://plugins.example.org/emu/index.json");
    CHECK(request->address.ns == "plugins.example.org");
    CHECK(request->address.label == "plugins.example.org/emu");

    CHECK_FALSE(ParseSourceRequest("http://plugins.example.org/"));
    CHECK_FALSE(ParseSourceRequest("jdoe"));
    CHECK_FALSE(ParseSourceRequest("jdoe/consoles#12"));

    const KeyPair key = KeyOf(2);
    CHECK(KeyFingerprint(key.public_b64).size() == 19);
    CHECK(KeyFingerprint("not a key").empty());
    const SourceEntry entry{"github:jdoe/consoles@main", {key.public_b64}, false};
    const std::string line = FormatSourceEntry(entry);
    CHECK(line == "github:jdoe/consoles@main " + key.public_b64 + " off");
    const auto read = ParseSourceEntry(line);
    REQUIRE(read);
    CHECK(read->address == entry.address);
    CHECK(read->keys == entry.keys);
    CHECK_FALSE(read->enabled);
    CHECK_FALSE(ParseSourceEntry("github:jdoe/consoles@main nokey on"));

    CHECK(CheckSourceNamespace("jdoe.gba", "jdoe").ok);
    CHECK_FALSE(CheckSourceNamespace("jdoex.gba", "jdoe").ok);
    CHECK_FALSE(CheckSourceNamespace("emuden.gba", "emuden").ok);
}

TEST_CASE("a source is added with its key pinned on first use") {
    Fixture f;
    f.Serve(JDOE, f.jdoe, 1, {f.Console(JDOE, "jdoe.console", "jcon", "jcore")});
    auto host = f.MakeHost();
    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);

    const auto [probe, error] = f.Probe(*host, "jdoe/consoles");
    REQUIRE(error == "");
    CHECK(probe.fingerprint == KeyFingerprint(f.jdoe.public_b64));
    CHECK_FALSE(probe.pinned); // the user is asked
    CHECK(probe.name == "Some list");
    CHECK(probe.plugins == 1);
    CHECK(host->Sources().size() == 1); // nothing is added by probing
    REQUIRE(host->AddSource(probe).ok);
    CHECK(f.settings->GetList(SOURCES_KEY) ==
          std::vector<std::string>{"github:jdoe/consoles@main " + f.jdoe.public_b64 + " on"});
    CHECK_FALSE(host->AddSource(probe).ok); // once

    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
    const auto console = host->FindConsole("jcon");
    REQUIRE(console);
    CHECK(console->source == "jdoe/consoles");
    CHECK(host->FindConsole("testcon")->source.empty());
    const auto info = SourceOf(*host, "jdoe/consoles");
    REQUIRE(info);
    CHECK(info->fingerprint == probe.fingerprint);
    CHECK(info->checked == f.now);
    CHECK(info->error.empty());
    CHECK(info->offered == 1);
    CHECK_FALSE(info->official);

    const JobPtr install = f.Run(host->Install("jcon"));
    INFO(install->Error());
    REQUIRE(install->State() == JobState::Done);
    const auto core = host->CoreFor("jcon");
    REQUIRE(core);
    REQUIRE(core->installed);
    CHECK(host->VerifyCore(core->library).ok);
    const CoreLedger ledger(f.scratch.dir / "data" / "cores");
    REQUIRE(ledger.Find("jdoe.console/jcore") != nullptr);
    CHECK(ledger.Find("jdoe.console/jcore")->origin == "github:jdoe/consoles@main");
    CHECK(ReadOrigin(f.scratch.dir / "data" / "plugins" / "jdoe.console") == "github:jdoe/consoles@main");

    SECTION("a new host keeps the source and its console without the network") {
        f.web.files.clear();
        auto second = f.MakeHost();
        REQUIRE(second->Sources().size() == 2);
        CHECK(second->Installed("jcon"));
        CHECK(second->FindConsole("jcon")->source == "jdoe/consoles");
    }
    SECTION("a list signed with another key is refused") {
        f.Serve(JDOE, f.more, 2, {});
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done); // the official source is fine
        CHECK(SourceOf(*host, "jdoe/consoles")->error == "The plugin list is not signed by its source.");
        CHECK(host->Installed("jcon"));
    }
    SECTION("a switched-off source is not checked and offers nothing new") {
        REQUIRE(host->SetSourceEnabled("github:jdoe/consoles@main", false).ok);
        CHECK(f.settings->GetList(SOURCES_KEY).front().ends_with(" off"));
        const int asked = f.web.Count();
        f.Serve(JDOE, f.jdoe, 2, {f.Console(JDOE, "jdoe.console", "jcon", "jcore", "libretro", "1.1.0")});
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
        CHECK(f.web.Count() == asked + 2); // the official list and its signature only
        CHECK_FALSE(host->FindConsole("jcon")->update_available);
        CHECK(host->Installed("jcon"));
        CHECK_FALSE(host->SetSourceEnabled("github:tester/plugins@main", false).ok);
    }
}

TEST_CASE("a pinned fingerprint is checked") {
    Fixture f;
    f.Serve(JDOE, f.jdoe, 1, {});
    auto host = f.MakeHost();
    const std::string fingerprint = CompactFingerprint(KeyFingerprint(f.jdoe.public_b64));

    auto [refused, error] = f.Probe(*host, "jdoe/consoles#0123456789abcdef");
    CHECK(error.find("not the 0123456789abcdef you gave") != std::string::npos);
    CHECK(refused.address.empty());
    CHECK(host->Sources().size() == 1);

    const auto [pinned, ok] = f.Probe(*host, "jdoe/consoles#" + fingerprint);
    CHECK(ok == "");
    CHECK(pinned.pinned);

    // Neither the official source nor a source without a key is added.
    CHECK(f.Probe(*host, "tester/plugins").second.find("EmuDEN's own source") != std::string::npos);
    CHECK(f.Probe(*host, "nobody/here").second.find("publishes no key") != std::string::npos);
}

TEST_CASE("an added source stays in its namespace and never shadows EmuDEN") {
    Fixture f;
    f.Serve(JDOE, f.jdoe, 1,
            {f.Console(JDOE, "jdoe.console", "jcon", "jcore"), f.Console(JDOE, "other.console", "ocon", "ocore"),
             f.Console(JDOE, "emuden.testcon", "testcon", "evilcore"),
             f.Console(JDOE, "emuden.fresh", "fresh", "freshcore"),
             f.Console(JDOE, "jdoe.engine", "engcon", "engcore", "switch2"),
             f.Console(JDOE, "jdoe.shadow", "testcon", "testcore")});
    auto host = f.MakeHost();
    f.Add(*host, "jdoe/consoles");
    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);

    CHECK(host->FindConsole("jcon"));
    CHECK_FALSE(host->FindConsole("ocon"));
    CHECK_FALSE(host->FindConsole("fresh"));
    CHECK_FALSE(host->FindConsole("engcon"));
    // The official console keeps its system id; the added one with the same is not used.
    REQUIRE(host->FindConsole("testcon"));
    CHECK(host->FindConsole("testcon")->plugin_id == "emuden.testcon");
    CHECK(host->FindConsole("testcon")->source.empty());

    const auto info = SourceOf(*host, "jdoe/consoles");
    REQUIRE(info);
    CHECK(info->offered == 2); // jdoe.console, jdoe.shadow
    CHECK(HasNote(*info, "other.console: this source publishes only jdoe. plugins"));
    CHECK(HasNote(*info, "emuden.fresh: only EmuDEN publishes emuden. plugins"));
    CHECK(HasNote(*info, "emuden.testcon: only EmuDEN publishes"));
    CHECK(HasNote(*info, "jdoe.engine: the switch2 engine is only for EmuDEN's own plugins"));
    CHECK(HasNote(*info, "Console testcon (jdoe.shadow) comes from EmuDEN"));

    // The official core is installed from the official manifest, the shadow's core is not.
    REQUIRE(f.Run(host->Install("testcon"))->State() == JobState::Done);
    const CoreLedger ledger(f.scratch.dir / "data" / "cores");
    REQUIRE(ledger.Find("testcore") != nullptr);
    CHECK(ledger.Find("testcore")->origin == "official");
    CHECK(ledger.Find("jdoe.shadow/testcore") == nullptr);

    SECTION("a manifest that breaks the rules is refused when it is read") {
        Result<Manifest> manifest = ParseManifest(
            R"({"manifest_version":1,"id":"jdoe.x","name":"X","version":"1.0.0","kind":"console",
            "engine":"libretro","publisher":"P","api":1,"needs":["engine.libretro"],
            "systems":[{"id":"xcon","name":"X","short_name":"X","group":"other","order":1,
              "content":{"extensions":["x"]},"cores":[]}],"components":[]})");
        REQUIRE(manifest);
        CHECK(CheckThirdPartyManifest(*manifest, "jdoe").ok);
        CHECK_FALSE(CheckThirdPartyManifest(*manifest, "other").ok);
        manifest->engine = "switch2";
        CHECK_FALSE(CheckThirdPartyManifest(*manifest, "jdoe").ok);
        manifest->engine = "libretro";
        manifest->systems[0].console.scan.scanner = "switch2";
        CHECK_FALSE(CheckThirdPartyManifest(*manifest, "jdoe").ok);
    }
    SECTION("an added source's core never takes another core's file") {
        f.Serve(JDOE, f.jdoe, 2, {f.Console(JDOE, "jdoe.taker", "takecon", "testcore")});
        REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
        const JobPtr install = f.Run(host->Install("takecon"));
        CHECK(install->State() == JobState::Failed);
        CHECK(install->Error().find("would replace the core file testcore_libretro.dll") != std::string::npos);
        CHECK(host->Installed("testcon"));
    }
}

TEST_CASE("two added sources are merged and the first one added wins an id") {
    Fixture f;
    f.Serve(JDOE, f.jdoe, 1, {f.Console(JDOE, "jdoe.console", "jcon", "jcore")});
    f.Serve(MORE, f.more, 1,
            {f.Console(MORE, "jdoe.console", "jcon", "morecore"), f.Console(MORE, "jdoe.extra", "xcon", "xcore")});
    auto host = f.MakeHost();
    f.Add(*host, "jdoe/consoles");
    f.Add(*host, "jdoe/more");
    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);

    const auto sources = host->Sources();
    REQUIRE(sources.size() == 3);
    CHECK(sources[0].official);
    CHECK(sources[1].label == "jdoe/consoles");
    CHECK(sources[2].label == "jdoe/more");
    CHECK(sources[1].offered == 1);
    CHECK(sources[2].offered == 1);
    CHECK(HasNote(sources[2], "jdoe.console is also offered by jdoe/consoles, which is used"));

    REQUIRE(host->FindConsole("jcon"));
    CHECK(host->FindConsole("jcon")->source == "jdoe/consoles");
    REQUIRE(host->CoreFor("jcon"));
    CHECK(host->CoreFor("jcon")->id == "jcore");
    REQUIRE(host->FindConsole("xcon"));
    CHECK(host->FindConsole("xcon")->source == "jdoe/more");
    CHECK(host->Consoles().size() == 3);
}

TEST_CASE("a source whose key EmuDEN withdrew is not used") {
    Fixture f;
    f.Serve(JDOE, f.jdoe, 1, {f.Console(JDOE, "jdoe.console", "jcon", "jcore")});
    auto host = f.MakeHost();
    f.Add(*host, "jdoe/consoles");
    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
    REQUIRE(f.Run(host->Install("jcon"))->State() == JobState::Done);
    const std::filesystem::path library = host->CoreFor("jcon")->library;
    REQUIRE(host->VerifyCore(library).ok);

    const std::string revoked = R"(,"revoked_keys":[{"fingerprint":")" + KeyFingerprint(f.jdoe.public_b64) +
                                R"(","reason":"the key leaked"}])";
    f.Serve(OFFICIAL, f.official, 2, {f.Console(OFFICIAL, "emuden.testcon", "testcon", "testcore")}, revoked);
    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);

    CHECK_FALSE(host->FindConsole("jcon"));
    CHECK_FALSE(host->Installed("jcon"));
    const Status verified = host->VerifyCore(library);
    CHECK_FALSE(verified.ok);
    CHECK(verified.error.find("the key leaked") != std::string::npos);
    CHECK(SourceOf(*host, "jdoe/consoles")->error == "EmuDEN withdrew this source's key: the key leaked");

    // Nor can it be added again under another address with the same key.
    f.Serve(MORE, f.jdoe, 1, {});
    CHECK(f.Probe(*host, "jdoe/more").second.find("the key leaked") != std::string::npos);
}

TEST_CASE("removing a source removes what it installed") {
    Fixture f;
    f.Serve(JDOE, f.jdoe, 1, {f.Console(JDOE, "jdoe.console", "jcon", "jcore"), f.Feature(JDOE, "jdoe.notes")});
    auto host = f.MakeHost();
    f.Add(*host, "jdoe/consoles");
    REQUIRE(f.Run(host->Refresh())->State() == JobState::Done);
    REQUIRE(f.Run(host->Install("jcon"))->State() == JobState::Done);
    REQUIRE(f.Run(host->Install("testcon"))->State() == JobState::Done);
    const std::filesystem::path library = host->CoreFor("jcon")->library;

    // Its feature plugin through the feature host, which reads the same sources.
    FeatureConfig config;
    config.data_dir = f.scratch.dir / "data";
    config.jobs = &f.jobs;
    config.settings = f.settings.get();
    config.plugins = host.get();
    FeatureDeps deps;
    deps.http = f.web.Getter();
    deps.platform = "test";
    deps.now = [&f] { return f.now; };
    deps.defaults = false;
    auto features = CreateFeatureHostWith(config, std::move(deps));
    const auto find = [&](const std::string& id) -> std::optional<FeaturePlugin> {
        for (FeaturePlugin& plugin : features->Plugins()) {
            if (plugin.id == id) {
                return plugin;
            }
        }
        return std::nullopt;
    };
    REQUIRE(find("jdoe.notes"));
    CHECK(find("jdoe.notes")->source_name == "jdoe/consoles");
    CHECK(find("jdoe.notes")->trust == FeaturePlugin::Trust::ThirdParty);
    CHECK(f.Run(features->Install("jdoe.notes", false))->State() == JobState::Failed); // asks first
    const JobPtr feature_install = f.Run(features->Install("jdoe.notes", true));
    INFO(feature_install->Error());
    REQUIRE(feature_install->State() == JobState::Done);
    REQUIRE(find("jdoe.notes")->installed);
    CHECK(find("jdoe.notes")->trust == FeaturePlugin::Trust::ThirdParty);
    CHECK(ReadOrigin(f.scratch.dir / "data" / "plugins" / "jdoe.notes") == "github:jdoe/consoles@main");
    CHECK(SourceOf(*host, "jdoe/consoles")->installed == 2);
    std::ofstream(f.scratch.dir / "data" / "plugin-data" / "jdoe.notes" / "notes.txt") << "mine";

    const JobPtr removed = f.Run(host->RemoveSource("github:jdoe/consoles@main"));
    INFO(removed->Error());
    REQUIRE(removed->State() == JobState::Done);
    CHECK(host->Sources().size() == 1);
    CHECK(f.settings->GetList(SOURCES_KEY).empty());
    CHECK_FALSE(host->FindConsole("jcon"));
    CHECK_FALSE(std::filesystem::exists(library));
    CHECK_FALSE(std::filesystem::exists(f.scratch.dir / "data" / "plugins" / "jdoe.console"));
    CHECK_FALSE(std::filesystem::exists(f.scratch.dir / "data" / "plugins" / "jdoe.notes"));
    CHECK(host->Installed("testcon")); // the official source's stay
    // The plugin's own data stays; what was granted does not.
    CHECK(std::filesystem::exists(f.scratch.dir / "data" / "plugin-data" / "jdoe.notes" / "notes.txt"));
    CHECK_FALSE(std::filesystem::exists(f.scratch.dir / "data" / "plugin-data" / "jdoe.notes" / "grant.json"));
    features->Pump();
    CHECK_FALSE(find("jdoe.notes"));

    CHECK_FALSE(f.Run(host->RemoveSource("github:tester/plugins@main"))->State() == JobState::Done);
    auto second = f.MakeHost();
    CHECK(second->Sources().size() == 1);
    CHECK_FALSE(second->FindConsole("jcon"));
}
