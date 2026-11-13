// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Feature plugins end to end: the manifest and the signed index, install, update and remove,
// and the sample plugin (ROM Hacks, stage 1) producing its UI model.

#include <array>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "den/plugins/feature_manifest.h"
#include "den/plugins/signature.h"
#include "den/store/settings.h"
#include "feature_test_util.h"

using namespace den;
using namespace den::plugins;
using namespace den::plugins::testing;

namespace {

const fs::path SAMPLE = fs::path(DEN_PLUGINS_FIXTURES) / "feature" / "emuden.romhacks";
const char* const BASE = "https://raw.githubusercontent.com/tester/plugins/main/";

std::string ReadFixture(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string ManifestOf(const std::string& id, const std::string& version,
                       const std::string& feature) {
    return R"({"id":")" + id + R"(","name":"Notes","version":")" + version +
           R"(","kind":"feature","api":1,"needs":["kind.feature","feature.lua.1"],
              "publisher":"Tester","summary":"Keeps notes.","feature":)" +
           feature + "}";
}

const char* const NOTES_SCRIPT = R"(
    local plugin = { actions = {} }
    function plugin.open()
      den.ui.set{ title = "Notes " .. den.plugin.version,
                  body = { { type = "button", id = "add", label = "Add", action = "add", arg = 5 } } }
    end
    function plugin.actions.add(arg, value)
      den.ui.push{ title = "Added " .. tostring(arg) .. " " .. tostring(value), body = {} }
    end
    return plugin
)";

struct Fixture {
    ScratchDir scratch;
    Jobs jobs{2};
    FakeWeb web;
    std::shared_ptr<FakeServices> services = std::make_shared<FakeServices>();
    KeyPair key;
    std::optional<Index> index;
    std::int64_t now = *ParseUtcTime("2026-11-13T00:00:00Z");
    FeaturePlugin::Trust trust = FeaturePlugin::Trust::Official;
    std::string owner;
    std::unique_ptr<store::Settings> settings;

    Fixture() {
        std::array<std::uint8_t, 32> seed{};
        seed[5] = 9;
        key = KeyPairFromSeed(seed);
        settings = std::make_unique<store::Settings>(scratch.dir / "settings.json");
    }

    // Signs an index offering one feature plugin and serves its package.
    void Publish(int sequence, const std::string& id, const std::string& version,
                 const std::string& feature, const std::string& announced,
                 std::vector<std::pair<std::string, std::string>> extra = {},
                 const std::string& revoked = "[]") {
        std::vector<std::pair<std::string, std::string>> files = {
            {"plugin.json", ManifestOf(id, version, feature)}, {"main.lua", NOTES_SCRIPT}};
        files.insert(files.end(), extra.begin(), extra.end());
        const std::string package = StoredZipOf(files);
        const std::string path = "packages/" + id + "-" + version + ".emuplugin";
        const std::string text =
            R"({"index_version":1,"source":{"name":"Tester"},"sequence":)" +
            std::to_string(sequence) +
            R"(,"generated":"2026-11-12T00:00:00Z","expires":"2027-01-12T00:00:00Z",
            "plugins":[{"id":")" + id + R"(","name":"Notes","version":")" + version +
            R"(","kind":"feature","publisher":"Tester","summary":"Keeps notes.",)" + announced +
            R"("package":{"url":")" + path + R"(","sha256":")" + HashOf(package) +
            R"(","size":)" + std::to_string(package.size()) + R"(}}],"revoked":)" + revoked +
            R"(,"blocked_sha256":[]})";
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
        const std::array<std::string, 1> keys = {key.public_b64};
        Index verified;
        const IndexVerdict verdict =
            VerifyIndex(text, SignEd25519(bytes, key), keys, 0, now, verified);
        REQUIRE(verdict.check == IndexCheck::Ok);
        index = verified;
        web.files[std::string(BASE) + path] = package;
    }

    std::unique_ptr<FeatureHost> MakeHost(const fs::path& bundled = {}) {
        FeatureConfig config;
        config.data_dir = scratch.dir / "data";
        config.jobs = &jobs;
        config.settings = settings.get();
        config.app_version = "1.2.3";
        FeatureDeps deps;
        deps.http = web.Getter();
        deps.services = services;
        deps.bundled_dir = bundled;
        deps.downloads_dir = scratch.dir / "Downloads";
        deps.platform = "test";
        deps.now = [this] { return now; };
        deps.index = [this] { return index; };
        deps.index_url = std::string(BASE) + "index.json";
        deps.source_trust = trust;
        deps.source_owner = owner;
        deps.defaults = false;
        deps.idle_seconds = 0;
        deps.instruction_limit = 5'000'000;
        return CreateFeatureHostWith(config, std::move(deps));
    }

    JobPtr Run(JobPtr job) {
        jobs.Wait(job);
        return job;
    }
};

std::optional<FeaturePlugin> Find(const FeatureHost& host, const std::string& id) {
    for (const FeaturePlugin& plugin : host.Plugins()) {
        if (plugin.id == id) {
            return plugin;
        }
    }
    return std::nullopt;
}

// Waits until the plugin's pages satisfy `done` (its script runs on its own thread).
template <typename Done>
std::vector<PageModel> PagesWhen(FeatureHost& host, const std::string& id, Done done) {
    std::vector<PageModel> pages;
    for (int i = 0; i < 400; ++i) {
        std::uint64_t revision = 0;
        pages = host.Pages(id, revision);
        if (done(pages)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pages;
}

const Element* FindType(const PageModel& page, ElementType type) {
    for (const Element& element : page.body) {
        if (element.type == type) {
            return &element;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a feature manifest is read and its permissions are explained") {
    const Result<FeatureManifest> manifest =
        ParseFeatureManifest(ReadFixture(SAMPLE / "plugin.json"));
    REQUIRE(manifest.ok());
    const FeatureManifest& m = manifest.value();
    CHECK(m.id == "emuden.romhacks");
    CHECK(m.entry == "main.lua");
    CHECK(m.access.Has(PERMISSION_PATCH));
    CHECK_FALSE(m.access.Has(PERMISSION_HTTP));
    CHECK(m.access.http_hosts.empty()); // stage 1 declares no host: no request can be made
    CHECK(m.access.open_hosts == std::vector<std::string>{"www.hackdex.app"});
    CHECK(m.access.SeesSystem("gba"));
    CHECK_FALSE(m.access.SeesSystem("psx"));
    CHECK(m.memory_mb == 32);
    const std::vector<PermissionLine> lines = PermissionLines(m.access);
    REQUIRE(lines.size() == 6);
    CHECK(lines[0].text == "Open pages of www.hackdex.app in your browser");
    CHECK(lines[3].text == "See which games of these consoles you have: gb, gbc, gba, nds");
    CHECK(CheckTrust(m, FeaturePlugin::Trust::Official, {}).ok);
    CHECK_FALSE(IsFeatureManifest(R"({"id":"emuden.gba","kind":"console"})"));
}

TEST_CASE("a feature manifest that asks for what it may not is refused") {
    const auto refused = [](const std::string& feature) {
        return !ParseFeatureManifest(ManifestOf("tester.notes", "1.0.0", feature)).ok();
    };
    CHECK_FALSE(refused(R"({"entry":"main.lua","permissions":[]})"));
    CHECK(refused(R"({"entry":"main.lua","permissions":["shell"]})"));
    CHECK(refused(R"({"entry":"../main.lua","permissions":[]})"));
    CHECK(refused(R"({"entry":"main.luac","permissions":[]})"));
    CHECK(refused(R"({"entry":"main.lua","permissions":["http"]})"));
    CHECK(refused(R"({"entry":"main.lua","permissions":["http"],"http":{"hosts":["*.example.org"]}})"));
    CHECK(refused(R"({"entry":"main.lua","permissions":["http"],"http":{"hosts":["example.org:8080"]}})"));
    CHECK(refused(R"({"entry":"main.lua","permissions":["http"],"http":{"hosts":["https://example.org"]}})"));
    CHECK(refused(R"({"entry":"main.lua","permissions":["open-url"]})"));
    CHECK(refused(R"({"entry":"main.lua","permissions":["library.read"]})"));
    CHECK(refused(R"({"entry":"main.lua","permissions":[],"limits":{"memory_mb":512}})"));
    CHECK_FALSE(ParseFeatureManifest(R"({"id":"tester.notes","name":"N","version":"1.0.0",
        "kind":"feature","api":1,"needs":["kind.feature","feature.lua.9"],
        "feature":{"entry":"main.lua","permissions":[]}})").ok());

    // Sources: only the official one sees every console; a third party stays in its namespace.
    const FeatureManifest all = ParseFeatureManifest(ManifestOf("tester.notes", "1.0.0",
        R"({"entry":"main.lua","permissions":["library.read"],"systems":["*"]})")).take();
    CHECK_FALSE(CheckTrust(all, FeaturePlugin::Trust::ThirdParty, "tester").ok);
    CHECK_FALSE(CheckTrust(all, FeaturePlugin::Trust::Developer, {}).ok);
    const FeatureManifest watch = ParseFeatureManifest(ManifestOf("tester.notes", "1.0.0",
        R"({"entry":"main.lua","permissions":["downloads.watch"],"file_types":["bps"]})")).take();
    CHECK_FALSE(CheckTrust(watch, FeaturePlugin::Trust::ThirdParty, "tester").ok);
    const FeatureManifest plain = ParseFeatureManifest(ManifestOf("tester.notes", "1.0.0",
        R"({"entry":"main.lua","permissions":[]})")).take();
    CHECK(CheckTrust(plain, FeaturePlugin::Trust::ThirdParty, "Tester").ok);
    CHECK_FALSE(CheckTrust(plain, FeaturePlugin::Trust::ThirdParty, "other").ok);
    CHECK_FALSE(CheckTrust(plain, FeaturePlugin::Trust::Official, {}).ok);
}

TEST_CASE("a feature plugin is listed from the signed index, installed, run and removed") {
    Fixture f;
    f.trust = FeaturePlugin::Trust::ThirdParty;
    f.owner = "tester";
    const std::string feature =
        R"({"entry":"main.lua","permissions":["http"],"http":{"hosts":["api.example.org"]}})";
    const std::string announced = R"("permissions":["http"],"http_hosts":["api.example.org"],)";
    f.Publish(1, "tester.notes", "1.0.0", feature, announced);
    const auto host = f.MakeHost();

    // Listed before any download, with what it will ask for.
    std::optional<FeaturePlugin> listed = Find(*host, "tester.notes");
    REQUIRE(listed);
    CHECK_FALSE(listed->installed);
    CHECK(listed->needs_consent);
    REQUIRE(listed->permissions.size() == 1);
    CHECK(listed->permissions[0].text == "Connect to: api.example.org");
    CHECK(f.web.Count() == 0);

    // A third-party plugin is not installed without consent.
    CHECK(f.Run(host->Install("tester.notes", false))->State() == JobState::Failed);
    CHECK_FALSE(host->Open("tester.notes").ok);
    const JobPtr installed = f.Run(host->Install("tester.notes", true));
    CHECK(installed->Error() == "");
    listed = Find(*host, "tester.notes");
    REQUIRE(listed);
    CHECK(listed->installed);
    CHECK_FALSE(listed->needs_consent);
    CHECK(fs::exists(f.scratch.dir / "data/plugins/tester.notes/1.0.0/main.lua"));
    CHECK(fs::exists(f.scratch.dir / "data/plugin-data/tester.notes/grant.json"));

    // Opened: the script's page, an event, Back.
    REQUIRE(host->Open("tester.notes").ok);
    std::vector<PageModel> pages = PagesWhen(*host, "tester.notes",
                                             [](const auto& p) { return p.size() == 1; });
    REQUIRE(pages.size() == 1);
    CHECK(pages[0].title == "Notes 1.0.0");
    REQUIRE(pages[0].body.size() == 1);
    CHECK(pages[0].body[0].arg_json == "5");
    host->Send("tester.notes", {"add", pages[0].body[0].arg_json, "typed"});
    pages = PagesWhen(*host, "tester.notes", [](const auto& p) { return p.size() == 2; });
    REQUIRE(pages.size() == 2);
    CHECK(pages[1].title == "Added 5 typed");
    host->Back("tester.notes");
    std::uint64_t revision = 0;
    CHECK(host->Pages("tester.notes", revision).size() == 1);
    CHECK(Find(*host, "tester.notes")->running);

    // An update that asks for another host waits for consent; the data stays.
    f.Publish(2, "tester.notes", "1.1.0",
              R"({"entry":"main.lua","permissions":["http"],
                  "http":{"hosts":["api.example.org","cdn.example.org"]}})",
              R"("permissions":["http"],"http_hosts":["api.example.org","cdn.example.org"],)");
    listed = Find(*host, "tester.notes");
    CHECK(listed->update_available);
    CHECK(listed->needs_consent);
    REQUIRE(listed->new_permissions.size() == 1);
    CHECK(listed->new_permissions[0].text == "Connect to: cdn.example.org");
    CHECK(f.Run(host->Install("tester.notes", false))->State() == JobState::Failed);
    CHECK(f.Run(host->Install("tester.notes", true))->Error() == "");
    CHECK(Find(*host, "tester.notes")->version == "1.1.0");

    // Closed and idle: the script is dropped; the next start runs the new version.
    host->Close("tester.notes");
    host->Pump();
    CHECK_FALSE(Find(*host, "tester.notes")->running);
    REQUIRE(host->Open("tester.notes").ok);
    pages = PagesWhen(*host, "tester.notes", [](const auto& p) { return p.size() == 1; });
    REQUIRE(pages.size() == 1);
    CHECK(pages[0].title == "Notes 1.1.0");

    // A revoked version is stopped and does not start again.
    f.Publish(3, "tester.notes", "1.1.0", feature, announced, {},
              R"([{"id":"tester.notes","versions":"*","reason":"Withdrawn."}])");
    host->Pump();
    CHECK_FALSE(Find(*host, "tester.notes")->running);
    CHECK(host->Open("tester.notes").error == "Withdrawn.");

    CHECK(f.Run(host->Remove("tester.notes", true, false))->Error() == "");
    CHECK_FALSE(fs::exists(f.scratch.dir / "data/plugins/tester.notes"));
    CHECK_FALSE(fs::exists(f.scratch.dir / "data/plugin-data/tester.notes"));
}

TEST_CASE("a package that is not what the index announced is refused") {
    Fixture f;
    // More than was announced: the user accepted less.
    f.Publish(1, "emuden.notes", "1.0.0",
              R"({"entry":"main.lua","permissions":["http"],"http":{"hosts":["api.example.org"]}})",
              "");
    auto host = f.MakeHost();
    JobPtr job = f.Run(host->Install("emuden.notes", true));
    CHECK(job->Error() == "The plugin asks for more than its source announced.");
    CHECK_FALSE(Find(*host, "emuden.notes")->installed);

    // A file a package may not hold.
    f.Publish(2, "emuden.notes", "1.0.0", R"({"entry":"main.lua","permissions":[]})", "",
              {{"native.dll", "MZ"}});
    job = f.Run(host->Install("emuden.notes", true));
    CHECK(job->State() == JobState::Failed);
    CHECK(job->Error().find("native.dll") != std::string::npos);

    // A path that leaves the package folder.
    f.Publish(3, "emuden.notes", "1.0.0", R"({"entry":"main.lua","permissions":[]})", "",
              {{"../evil.lua", "x"}});
    CHECK(f.Run(host->Install("emuden.notes", true))->State() == JobState::Failed);
    CHECK_FALSE(fs::exists(f.scratch.dir / "data/plugins/evil.lua"));

    // Tampered bytes do not match the signed checksum.
    f.Publish(4, "emuden.notes", "1.0.0", R"({"entry":"main.lua","permissions":[]})", "");
    f.web.files[std::string(BASE) + "packages/emuden.notes-1.0.0.emuplugin"] += "x";
    CHECK(f.Run(host->Install("emuden.notes", true))->State() == JobState::Failed);

    // The official source installs in one press.
    f.Publish(5, "emuden.notes", "1.0.0", R"({"entry":"main.lua","permissions":[]})", "");
    CHECK(f.Run(host->Install("emuden.notes", false))->Error() == "");
    CHECK(Find(*host, "emuden.notes")->installed);
}

TEST_CASE("the sample plugin installs a hack from a patch file") {
    Fixture f;
    f.scratch.Write("Downloads/Emerald_Kaizo.bps", "BPS1 and the rest of a patch");
    f.scratch.Write("Downloads/holiday.jpg", "not a patch");
    // It ships with the app: installed and granted without a download.
    const auto host = f.MakeHost(fs::path(DEN_PLUGINS_FIXTURES) / "feature");
    const std::string id = "emuden.romhacks";
    std::optional<FeaturePlugin> plugin = Find(*host, id);
    REQUIRE(plugin);
    CHECK(plugin->installed);
    CHECK_FALSE(plugin->needs_consent);
    CHECK(plugin->permissions.size() == 6);

    REQUIRE(host->Open(id).ok);
    std::vector<PageModel> pages =
        PagesWhen(*host, id, [](const auto& p) { return p.size() == 1; });
    REQUIRE(pages.size() == 1);
    CHECK(pages[0].title == "ROM Hacks");
    const Element* shelf = FindType(pages[0], ElementType::Shelf);
    REQUIRE(shelf != nullptr);
    REQUIRE(shelf->items.size() == 1); // the picture is not a patch file
    CHECK(shelf->items[0].title == "Emerald_Kaizo.bps");
    CHECK(FindType(pages[0], ElementType::Empty) != nullptr);

    // "Open Hackdex" asks the screen to open the browser; the plugin itself connects nowhere.
    host->Send(id, {"browse", "", ""});
    std::vector<Ask> asks;
    for (int i = 0; i < 400 && asks.empty(); ++i) {
        asks = host->TakeAsks(id);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(asks.size() == 1);
    CHECK(asks[0].kind == AskKind::OpenUrl);
    CHECK(asks[0].value == "https://www.hackdex.app/discover");

    // The patch from Downloads: its base is found by checksum, the install page follows.
    host->Send(id, {"take", shelf->items[0].arg_json, ""});
    pages = PagesWhen(*host, id, [](const auto& p) { return p.size() == 2; });
    REQUIRE(pages.size() == 2);
    CHECK(pages[1].title == "Install a hack");
    const Element* input = FindType(pages[1], ElementType::Input);
    REQUIRE(input != nullptr);
    CHECK(input->value == "Emerald Kaizo");
    const Element* detail = FindType(pages[1], ElementType::Detail);
    REQUIRE(detail != nullptr);
    CHECK(detail->image == "game:g-emerald");
    REQUIRE(detail->facts.size() == 3);
    CHECK(detail->facts[0].value == "Pokemon Emerald");
    CHECK(detail->facts[1].value == "bps");
    REQUIRE(detail->items.size() == 1);

    host->Send(id, {detail->items[0].action, "", ""});
    pages = PagesWhen(*host, id, [](const auto& p) {
        return p.size() == 1 && FindType(p[0], ElementType::Grid) != nullptr;
    });
    REQUIRE(pages.size() == 1);
    REQUIRE(f.services->orders.size() == 1);
    CHECK(f.services->orders[0].title == "Emerald Kaizo");
    CHECK(f.services->orders[0].base_id == "g-emerald");
    CHECK(f.services->orders[0].plugin == id);
    CHECK(f.services->orders[0].patch.filename() == "Emerald_Kaizo.bps");
    const Element* grid = FindType(pages[0], ElementType::Grid);
    REQUIRE(grid->items.size() == 1);
    CHECK(grid->items[0].badge == "Hack");
    CHECK(fs::exists(f.scratch.dir / "Downloads/Emerald_Kaizo.bps")); // the original stays
    CHECK(f.web.Count() == 0);

    // A patch opened with the app reaches the same flow.
    f.scratch.Write("elsewhere/Other Hack.bps", "BPS1 another");
    CHECK(host->RouteFile(f.scratch.dir / "elsewhere/Other Hack.bps") == id);
    CHECK(host->RouteFile(f.scratch.dir / "Downloads/holiday.jpg").empty());
    pages = PagesWhen(*host, id, [](const auto& p) { return p.size() == 2; });
    REQUIRE(pages.size() == 2);
    CHECK(FindType(pages[1], ElementType::Input)->value == "Other Hack");
}
