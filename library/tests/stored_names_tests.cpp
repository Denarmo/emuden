// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Names and keys the library has stored keep matching after the move to den/base/names.h.

#include <algorithm>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/store/document.h"
#include "../src/hacks_store.h"

using namespace den;
using namespace den::library;
namespace fs = std::filesystem;

namespace {

// hacks_files.cpp's SafeFileName before the move: hack files and tidied games are named so.
std::string OldHackFileName(std::string_view title) {
    std::string name;
    for (const char c : title) {
        const auto byte = static_cast<unsigned char>(c);
        const bool refused = byte < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos;
        if (!refused && c != ' ') {
            name.push_back(c);
        } else if (!name.empty() && name.back() != ' ') {
            name.push_back(' ');
        }
    }
    constexpr std::size_t LIMIT = 120;
    if (name.size() > LIMIT) {
        std::size_t cut = LIMIT;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        name.resize(cut);
    }
    name = Trim(name);
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
        name.pop_back();
    }
    return name.empty() ? "Hack" : name;
}

} // namespace

TEST_CASE("hack file names are made as before") {
    const std::vector<std::string> titles = {
        "",
        "Pokemon: Unbound (v2.1.1)",
        "  lead / trail .. ",
        "tab\tand\nnewline",
        "a<b>c|d?e*f\"g\\h",
        std::string(119, 'x') + "\xC3\xA9 tail",
        std::string(118, 'x') + "  . end",
        "Caf\xC3\xA9 Hack",
    };
    for (const std::string& title : titles) {
        INFO(title);
        CHECK(HackFileName(title) == OldHackFileName(title));
    }
}

TEST_CASE("a checksum cache written with the old path keys still answers") {
    const fs::path dir = fs::temp_directory_path() / "emuden-library-test" / "stored-keys";
    fs::remove_all(dir);
    fs::create_directories(dir / "Roms" / "GBA");
    const fs::path rom = dir / "Roms" / "GBA" / "Base.GBA";
    std::ofstream(rom, std::ios::binary) << "not really a rom";
    std::error_code ec;
    const std::string written =
        std::to_string(static_cast<std::int64_t>(fs::last_write_time(rom, ec).time_since_epoch().count()));
    // The key as hacks_files.cpp made it before the move.
    std::string old_key = ToLower(PathToUtf8(rom.lexically_normal()));
    std::replace(old_key.begin(), old_key.end(), '\\', '/');
    const JsonValue form = JsonObject({{"form", JsonString("plain")},
                                       {"crc32", JsonString("12345678")},
                                       {"md5", JsonString("cached")},
                                       {"sha1", JsonString("")},
                                       {"size", JsonNumber(16)}});
    JsonValue forms = JsonArray();
    forms.items.push_back(form);
    JsonValue files = JsonArray();
    files.items.push_back(JsonObject({{"path", JsonString(old_key)},
                                      {"size", JsonNumber(16)},
                                      {"written", JsonString(written)},
                                      {"system", JsonString("gba")},
                                      {"forms", std::move(forms)}}));
    REQUIRE(store::SaveDocument(dir / "checksums.json", 1, JsonObject({{"files", std::move(files)}})));

    ChecksumCache cache(dir / "checksums.json");
    const auto got = cache.Get(rom, "gba");
    REQUIRE(got);
    REQUIRE(got.value().size() == 1);
    CHECK(got.value().front().sums.md5 == "cached");
    fs::remove_all(dir);
}
