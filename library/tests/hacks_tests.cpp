// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "../../patching/tests/encoders.h"
#include "../../patching/tests/vcdiff_writer.h"
#include "den/base/hash.h"
#include "den/base/jobs.h"
#include "den/library/hacks.h"

using namespace den;
using namespace den::library;
using namespace den::patching::testing;
namespace fs = std::filesystem;

namespace {

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "emuden-library-test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void Le(Bytes& out, std::uint64_t value, int bytes) {
    PutLe(out, value, bytes);
}

// A zip archive with one stored (uncompressed) file.
Bytes StoredZip(const std::string& name, const Bytes& data) {
    Bytes out, central;
    const std::uint32_t crc = Crc32(data.data(), data.size());
    Le(out, 0x04034b50, 4), Le(out, 20, 2), Le(out, 0, 2), Le(out, 0, 2), Le(out, 0, 4);
    Le(out, crc, 4), Le(out, data.size(), 4), Le(out, data.size(), 4);
    Le(out, name.size(), 2), Le(out, 0, 2);
    Put(out, name);
    Put(out, data.data(), data.size());
    Le(central, 0x02014b50, 4), Le(central, 20, 2), Le(central, 20, 2), Le(central, 0, 2);
    Le(central, 0, 2), Le(central, 0, 4), Le(central, crc, 4);
    Le(central, data.size(), 4), Le(central, data.size(), 4), Le(central, name.size(), 2);
    Le(central, 0, 2), Le(central, 0, 2), Le(central, 0, 2), Le(central, 0, 2), Le(central, 0, 4);
    Le(central, 0, 4);
    Put(central, name);
    const std::size_t start = out.size();
    Put(out, central.data(), central.size());
    Le(out, 0x06054b50, 4), Le(out, 0, 2), Le(out, 0, 2), Le(out, 1, 2);
    Le(out, 1, 2), Le(out, central.size(), 4), Le(out, start, 4), Le(out, 0, 2);
    return out;
}

std::uint32_t CrcOf(const Bytes& data) {
    return Crc32(data.data(), data.size());
}

// A library over <dir>/roms with its data in <dir>/user, scanned.
struct Fixture {
    fs::path dir, roms;
    Jobs jobs{2};
    std::unique_ptr<Library> library;

    explicit Fixture(const char* name) : dir(FreshDir(name)), roms(dir / "roms") {}

    void Open() {
        Config config;
        config.data_dir = dir / "user";
        config.jobs = &jobs;
        config.offline = true;
        library = CreateLibrary(config);
        if (library->Folders().empty()) {
            REQUIRE(library->AddFolder(PathToUtf8(roms)).ok);
        }
        Scan();
    }
    void Scan() {
        jobs.Wait(library->Scan());
    }
    Entry Named(const std::string& name) {
        for (const Entry& entry : library->Entries()) {
            if (entry.name == name) {
                return entry;
            }
        }
        FAIL("no game named " << name);
        return {};
    }
    Hacks& hacks() {
        return HacksOf(*library);
    }
};

} // namespace

TEST_CASE("a base game is found by any checksum, hashed once") {
    Fixture f("hacks-find");
    const Bytes emerald = MakeRom(120000, 1), ruby = MakeRom(120000, 2), tetris = MakeRom(32768, 3);
    WriteFile(f.roms / "gba" / "Emerald Quest (USA).gba", emerald);
    WriteFile(f.roms / "gba" / "Emerald Quest (Europe).gba", ruby);
    WriteFile(f.roms / "gb" / "Blocks (World).zip", StoredZip("Blocks (World).gb", tetris));
    f.Open();

    BaseQuery query;
    query.crc32 = CrcOf(emerald);
    auto match = f.hacks().FindBase(query);
    REQUIRE(match);
    REQUIRE(match->entry.name == "Emerald Quest");
    REQUIRE(match->form == "raw");
    REQUIRE(fs::path(PathFromUtf8(match->path)).filename() == "Emerald Quest (USA).gba");

    // The other region's file is hidden behind the same entry and still found.
    BaseQuery europe;
    europe.sha1 = patching::Sha1Hex(ruby.data(), ruby.size());
    europe.system = "gba";
    match = f.hacks().FindBase(europe);
    REQUIRE(match);
    REQUIRE(fs::path(PathFromUtf8(match->path)).filename() == "Emerald Quest (Europe).gba");

    // A game inside a zip archive.
    BaseQuery packed;
    packed.md5 = Md5Hex(tetris.data(), tetris.size());
    packed.size = tetris.size();
    match = f.hacks().FindBase(packed);
    REQUIRE(match);
    REQUIRE(match->entry.system == "gb");

    BaseQuery other_console = query;
    other_console.systems = {"gb", "nds"};
    REQUIRE(!f.hacks().FindBase(other_console));
    BaseQuery unknown;
    unknown.crc32 = 0x12345678;
    REQUIRE(!f.hacks().FindBase(unknown));
    REQUIRE(!f.hacks().FindBase(BaseQuery{}));

    // Remembered: the file names every checksum, and a new library reads it.
    const std::string remembered = ReadTextFile(f.dir / "user" / "library" / "checksums.json").value_or("");
    REQUIRE(remembered.find(patching::Sha1Hex(emerald.data(), emerald.size())) != std::string::npos);
    REQUIRE(remembered.find(patching::CrcText(CrcOf(ruby))) != std::string::npos);
    f.library.reset();
    f.Open();
    REQUIRE(f.hacks().FindBase(query));
}

TEST_CASE("a hack is installed beside the console's games, updated and removed") {
    Fixture f("hacks-install");
    const Bytes base = MakeRom(150000, 4), other = MakeRom(150000, 5);
    WriteFile(f.roms / "gba" / "Emerald Quest (USA).gba", base);
    WriteFile(f.roms / "gba" / "Sapphire Quest (USA).gba", other);
    f.Open();
    const Entry game = f.Named("Emerald Quest");
    const Bytes v1 = MakeHack(base, 10, 2048), v2 = MakeHack(base, 11, 4096), v3 = MakeHack(base, 12);
    WriteFile(f.dir / "v1.bps", MakeBps(base, v1));
    WriteFile(f.dir / "v2.bps", MakeBps(base, v2));
    WriteFile(f.dir / "v3.ups", MakeUps(base, v3));

    HackRequest request;
    request.base_id = game.id;
    request.patch = f.dir / "v1.bps";
    request.title = "Emerald: Night Shift";
    request.version = "1.0";
    request.plugin = "emuden.romhacks";
    request.source = "https://example.org/night-shift";
    const std::uint64_t before = f.library->Revision();
    float last = -1;
    const auto installed = f.hacks().InstallHack(request, [&](float fraction) {
        last = fraction;
        return true;
    });
    INFO(installed.error());
    REQUIRE(installed.ok());
    const Entry hack = installed.value();
    REQUIRE(last == 1.0f);
    REQUIRE(hack.hack_of == game.id);
    REQUIRE(hack.name == "Emerald: Night Shift");
    REQUIRE(hack.system == "gba");
    REQUIRE(hack.added > 0);
    const fs::path file = PathFromUtf8(hack.path);
    REQUIRE(file == f.roms / "gba" / "Hacks" / "Emerald Night Shift.gba");
    REQUIRE(ReadFile(file) == v1);
    REQUIRE(ReadFile(f.roms / "gba" / "Emerald Quest (USA).gba") == base);
    // In the library at once, without a scan.
    REQUIRE(f.library->Revision() > before);
    REQUIRE(f.library->Find(hack.id));
    REQUIRE(fs::is_empty(f.dir / "user" / "cache" / "patching"));

    // A scan finds the same game under the same id.
    f.Scan();
    const auto scanned = f.library->Find(hack.id);
    REQUIRE(scanned);
    REQUIRE(scanned->hack_of == game.id);
    REQUIRE(scanned->name == "Emerald: Night Shift");
    REQUIRE(f.library->Entries().size() == 3);

    // A newer patch replaces the file; the id stays, so saves and records do.
    f.library->RecordPlay(hack.id, 1000, 60);
    request.patch = f.dir / "v2.bps";
    request.version = "1.1";
    const auto updated = f.hacks().InstallHack(request);
    REQUIRE(updated.ok());
    REQUIRE(updated.value().id == hack.id);
    REQUIRE(updated.value().play_count == 1);
    REQUIRE(updated.value().play_seconds == 60);
    REQUIRE(ReadFile(file) == v2);
    std::vector<Hack> list = f.hacks().List("emuden.romhacks");
    REQUIRE(list.size() == 1);
    REQUIRE(list[0].version == "1.1");
    REQUIRE(list[0].base_id == game.id);
    REQUIRE(list[0].source == "https://example.org/night-shift");
    REQUIRE(list[0].installed > 0);
    REQUIRE(f.hacks().List("someone.else").empty());

    // A version that breaks saves is installed beside the old one.
    request.patch = f.dir / "v3.ups";
    request.version = "2.0";
    request.keep_old = true;
    const auto beside = f.hacks().InstallHack(request);
    REQUIRE(beside.ok());
    REQUIRE(beside.value().id != hack.id);
    REQUIRE(ReadFile(PathFromUtf8(beside.value().path)) == v3);
    REQUIRE(ReadFile(file) == v2);
    REQUIRE(f.hacks().List().size() == 2);

    // Only the plugin that installed a hack changes it; the app may.
    REQUIRE(!f.hacks().Update(hack.id, "someone.else", std::string("Mine"), {}, {}).ok);
    REQUIRE(!f.hacks().Update(game.id, "", std::string("Not a hack"), {}, {}).ok);
    WriteFile(f.dir / "art.png", Bytes{0x89, 'P', 'N', 'G'});
    REQUIRE(f.hacks().Update(hack.id, "emuden.romhacks", std::string("Night Shift"), std::string("Dark."), f.dir / "art.png").ok);
    Entry renamed = *f.library->Find(hack.id);
    REQUIRE(renamed.name == "Night Shift");
    REQUIRE(PathFromUtf8(renamed.cover) == f.roms / "gba" / "Hacks" / "Emerald Night Shift.png");
    f.Scan();
    renamed = *f.library->Find(hack.id);
    REQUIRE(renamed.name == "Night Shift");
    REQUIRE(PathFromUtf8(renamed.cover).filename() == "Emerald Night Shift.png");

    // The wrong game: both checksums and the game's name are in the message.
    request.base_id = f.Named("Sapphire Quest").id;
    request.keep_old = false;
    request.title = "Wrong";
    const auto wrong = f.hacks().InstallHack(request);
    REQUIRE(!wrong.ok());
    REQUIRE(wrong.error().find(patching::CrcText(CrcOf(base))) != std::string::npos);
    REQUIRE(wrong.error().find(patching::CrcText(CrcOf(other))) != std::string::npos);
    REQUIRE(wrong.error().find("Sapphire Quest") != std::string::npos);
    REQUIRE(!fs::exists(f.roms / "gba" / "Hacks" / "Wrong.gba"));

    REQUIRE(!f.hacks().Remove(hack.id, "someone.else").ok);
    REQUIRE(f.hacks().Remove(hack.id, "emuden.romhacks").ok);
    REQUIRE(!f.library->Find(hack.id));
    REQUIRE(!fs::exists(file));
    REQUIRE(!fs::exists(f.roms / "gba" / "Hacks" / "Emerald Night Shift.png"));
    REQUIRE(!fs::exists(f.roms / "gba" / "Hacks" / "Emerald Night Shift.gba.hack.json"));
    REQUIRE(ReadFile(f.roms / "gba" / "Emerald Quest (USA).gba") == base);
    f.Scan();
    REQUIRE(f.library->Entries().size() == 3);
}

TEST_CASE("a patch made for the ROM without its header is applied under the header") {
    Fixture f("hacks-header");
    const Bytes header = {'N', 'E', 'S', 0x1A, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const Bytes body = MakeRom(40960, 6);
    Bytes whole = header;
    whole.insert(whole.end(), body.begin(), body.end());
    WriteFile(f.roms / "nes" / "Plumber (World).nes", whole);
    f.Open();
    const Bytes patched = MakeHack(body, 13);
    WriteFile(f.dir / "hard.bps", MakeBps(body, patched));

    const auto opened = OpenPatchFile(*f.library, f.dir / "hard.bps", f.dir / "work");
    REQUIRE(opened.ok());
    REQUIRE(opened.value().base);
    REQUIRE(opened.value().base->form == "headerless");
    REQUIRE(opened.value().title == "hard");
    HackRequest request;
    request.base_id = opened.value().base->entry.id;
    request.patch = opened.value().patch;
    request.title = "Plumber Hard Mode";
    const auto installed = f.hacks().InstallHack(request);
    INFO(installed.error());
    REQUIRE(installed.ok());
    Bytes expected = header;
    expected.insert(expected.end(), patched.begin(), patched.end());
    REQUIRE(ReadFile(PathFromUtf8(installed.value().path)) == expected);
}

TEST_CASE("a game outside a console folder gets its hacks under the app's data") {
    Fixture f("hacks-data");
    const Bytes base = MakeRom(100000, 7);
    WriteFile(f.roms / "gb" / "Other (World).gb", MakeRom(32768, 8));
    WriteFile(f.roms / "Loose Game (USA).gba", base);
    f.Open();
    const Entry game = f.Named("Loose Game");
    const Bytes patched = MakeHack(base, 14);
    WriteFile(f.dir / "loose.ips", MakeIps(base, patched));
    HackRequest request;
    request.base_id = game.id;
    request.patch = f.dir / "loose.ips";
    request.title = "Loose Game DX";
    const auto installed = f.hacks().InstallHack(request);
    INFO(installed.error());
    REQUIRE(installed.ok());
    const fs::path file = PathFromUtf8(installed.value().path);
    REQUIRE(file == f.dir / "user" / "library" / "hacks" / "gba" / "Hacks" / "Loose Game DX.gba");
    REQUIRE(ReadFile(file) == patched);
    f.Scan();
    const auto scanned = f.library->Find(installed.value().id);
    REQUIRE(scanned);
    REQUIRE(scanned->hack_of == game.id);
    REQUIRE(scanned->name == "Loose Game DX");
    // A file of the user's with that name is not replaced.
    WriteFile(file.parent_path() / "Mine.gba", base);
    request.title = "Mine";
    REQUIRE(!f.hacks().InstallHack(request).ok());
    REQUIRE(ReadFile(file.parent_path() / "Mine.gba") == base);
}

TEST_CASE("Patch a game: from the chosen file to the game it is for") {
    Fixture f("hacks-open");
    const Bytes base = MakeRom(90000, 9), other = MakeRom(64000, 10);
    WriteFile(f.roms / "gba" / "Emerald Quest (USA).gba", base);
    WriteFile(f.roms / "gba" / "Small Game (USA).gba", other);
    f.Open();
    const Bytes patched = MakeHack(base, 15, 100);

    // A BPS inside a zip names its game.
    WriteFile(f.dir / "dl" / "Night Shift v1.2.zip", StoredZip("patches/Night Shift.bps", MakeBps(base, patched)));
    auto opened = OpenPatchFile(*f.library, f.dir / "dl" / "Night Shift v1.2.zip", f.dir / "work");
    INFO(opened.error());
    REQUIRE(opened.ok());
    REQUIRE(opened.value().info.format == patching::Format::Bps);
    REQUIRE(opened.value().title == "Night Shift");
    REQUIRE(opened.value().patch == f.dir / "work" / "Night Shift.bps");
    REQUIRE(opened.value().base);
    REQUIRE(opened.value().base->entry.name == "Emerald Quest");
    REQUIRE(opened.value().choices.empty());

    // An IPS names nothing: the user chooses among the games.
    WriteFile(f.dir / "dl" / "fix.ips", MakeIps(base, patched));
    opened = OpenPatchFile(*f.library, f.dir / "dl" / "fix.ips", f.dir / "work");
    REQUIRE(opened.ok());
    REQUIRE(!opened.value().base);
    REQUIRE(opened.value().choices.size() == 2);
    REQUIRE(opened.value().missing.empty());

    // An APS names the size: only games of that size are offered.
    WriteFile(f.dir / "dl" / "fix.aps", MakeAps(base, patched));
    opened = OpenPatchFile(*f.library, f.dir / "dl" / "fix.aps", f.dir / "work");
    REQUIRE(opened.value().choices.size() == 1);
    REQUIRE(opened.value().choices[0].name == "Emerald Quest");

    // An xdelta is applied to the chosen game and checked window by window.
    WriteFile(f.dir / "dl" / "fix.xdelta", MakeVcdiff(base, patched));
    opened = OpenPatchFile(*f.library, f.dir / "dl" / "fix.xdelta", f.dir / "work");
    REQUIRE(opened.value().choices.size() == 2);
    HackRequest request;
    request.patch = opened.value().patch;
    request.title = opened.value().title;
    request.base_id = f.Named("Small Game").id;
    REQUIRE(!f.hacks().InstallHack(request).ok());
    request.base_id = f.Named("Emerald Quest").id;
    const auto installed = f.hacks().InstallHack(request);
    REQUIRE(installed.ok());
    REQUIRE(ReadFile(PathFromUtf8(installed.value().path)) == patched);

    // A patch for a game the library does not have says which.
    const Bytes absent = MakeRom(5000, 11);
    WriteFile(f.dir / "dl" / "absent.ups", MakeUps(absent, MakeHack(absent, 16)));
    opened = OpenPatchFile(*f.library, f.dir / "dl" / "absent.ups", f.dir / "work");
    REQUIRE(opened.ok());
    REQUIRE(!opened.value().base);
    REQUIRE(opened.value().missing.find(patching::CrcText(CrcOf(absent))) != std::string::npos);

    WriteFile(f.dir / "dl" / "empty.zip", StoredZip("readme.txt", Bytes{'h', 'i'}));
    REQUIRE(!OpenPatchFile(*f.library, f.dir / "dl" / "empty.zip", f.dir / "work").ok());
    WriteFile(f.dir / "dl" / "notes.bps", Bytes{'h', 'i'});
    REQUIRE(!OpenPatchFile(*f.library, f.dir / "dl" / "notes.bps", f.dir / "work").ok());
}

// The real-world check: a GBA ROM of the test library, copied to a scratch folder (the test
// library is never written), with a BPS and an IPS made against it.
TEST_CASE("a real GBA ROM takes a BPS and an IPS", "[.live]") {
    std::string roms = GetEnv("EMUDEN_TEST_ROMS");
    if (roms.empty()) {
        roms = "D:/ROMS";
    }
    fs::path rom;
    for (const fs::path& file : ListDirectory(PathFromUtf8(roms) / "GBA")) {
        std::error_code ec;
        if (ToLower(PathToUtf8(file.extension())) == ".gba" && (rom.empty() || fs::file_size(file, ec) < fs::file_size(rom, ec))) {
            rom = file;
        }
    }
    REQUIRE(!rom.empty());
    Fixture f("hacks-live");
    fs::create_directories(f.roms / "gba");
    fs::copy_file(rom, f.roms / "gba" / rom.filename());
    f.Open();
    REQUIRE(f.library->Entries().size() == 1);
    const Entry game = f.library->Entries()[0];
    const Bytes base = ReadFile(f.roms / "gba" / rom.filename());
    const Bytes patched = MakeHack(base, 20, 65536);
    WriteFile(f.dir / "live.bps", MakeBps(base, patched));
    // IPS cannot reach past 16 MiB: its target stays the base's size.
    const Bytes same_size = MakeHack(base, 21);
    WriteFile(f.dir / "live.ips", MakeIps(base, same_size));
    WARN(PathToUtf8(rom.filename()) << ": " << base.size() << " bytes, CRC32 " << patching::CrcText(CrcOf(base))
                                    << ", BPS " << fs::file_size(f.dir / "live.bps") << " bytes, IPS "
                                    << fs::file_size(f.dir / "live.ips") << " bytes");

    const auto opened = OpenPatchFile(*f.library, f.dir / "live.bps", f.dir / "work");
    REQUIRE(opened.ok());
    REQUIRE(opened.value().base);
    REQUIRE(opened.value().base->entry.id == game.id);
    HackRequest request;
    request.base_id = game.id;
    request.patch = f.dir / "live.bps";
    request.title = "Live BPS";
    auto installed = f.hacks().InstallHack(request);
    INFO(installed.error());
    REQUIRE(installed.ok());
    REQUIRE(ReadFile(PathFromUtf8(installed.value().path)) == patched);
    REQUIRE(installed.value().hack_of == game.id);

    request.patch = f.dir / "live.ips";
    request.title = "Live IPS";
    installed = f.hacks().InstallHack(request);
    REQUIRE(installed.ok());
    REQUIRE(ReadFile(PathFromUtf8(installed.value().path)) == same_size);
    // The copy of the base is as it was, and the scan lists all three.
    REQUIRE(ReadFile(f.roms / "gba" / rom.filename()) == base);
    f.Scan();
    REQUIRE(f.library->Entries().size() == 3);
    f.library.reset();
    fs::remove_all(f.dir);
}
