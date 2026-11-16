// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "den/base/files.h"
#include "den/base/names.h"

using namespace den;

namespace {

// The copies these helpers replaced, as they were: the names and keys they made are on disk.
std::string OldGameFileName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        const bool bad = u < 0x20 || std::strchr("<>:\"/\\|?*", c) != nullptr;
        out.push_back(bad ? '_' : c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
        out.pop_back();
    }
    if (out.size() > 120) {
        out.resize(120);
    }
    return out.empty() ? "game" : out;
}

std::string OldChecksumKey(const std::filesystem::path& path) {
    std::string key = ToLower(PathToUtf8(path.lexically_normal()));
    std::replace(key.begin(), key.end(), '\\', '/');
    return key;
}

std::vector<std::string> Names() {
    std::vector<std::string> names = {
        "",          "...",         "Pokemon Emerald",   "a/b:c?.gb",       "  lead",       "trail. . ",
        "tab\there", "gba:0123abcd", "Caf\xC3\xA9 <1>",   "x*y|z\"w\\v",     ". hidden",     "two  spaces",
    };
    names.push_back(std::string(119, 'a') + " b");   // the cut leaves a space at the end
    names.push_back(std::string(119, 'a') + "\xC3\xA9"); // the cut splits a character
    names.push_back(std::string(200, '.') + "x");
    return names;
}

} // namespace

TEST_CASE("game file names keep the names saves and states were stored under") {
    for (const std::string& name : Names()) {
        INFO(name);
        CHECK(SafeFileName(name) == OldGameFileName(name));
    }
    CHECK(SafeFileName("a/b:c?.gb") == "a_b_c_.gb");
    CHECK(SafeFileName("...") == "game");
}

TEST_CASE("other file name rules") {
    const FileNameRules words{.replacement = ' ', .collapse_spaces = true, .trim_front = " ", .byte_cut = false, .fallback = "Hack"};
    CHECK(SafeFileName("Pokemon: Unbound / v2 ", words) == "Pokemon Unbound v2");
    CHECK(SafeFileName("<>", words) == "Hack");
    CHECK(SafeFileName(std::string(119, 'a') + "\xC3\xA9", words) == std::string(119, 'a'));
    const FileNameRules dropped{.replacement = '\0', .trim_front = ". ", .max_bytes = 0, .fallback = "download"};
    CHECK(SafeFileName("..hack?.bps.", dropped) == "hack.bps");
    CHECK(SafeFileName(std::string(300, 'x'), dropped).size() == 300);
    CHECK(SafeFileName("saves/gba/A:B", {.allow_slash = true}) == "saves/gba/A_B");
    CHECK(IsRefusedNameChar('/'));
    CHECK_FALSE(IsRefusedNameChar('/', true));
    CHECK(IsRefusedNameChar('\x01', true));
    CHECK_FALSE(IsRefusedNameChar('a'));
}

TEST_CASE("path keys match the stored hack checksum keys") {
    const std::vector<std::string> paths = {"C:\\Games\\GBA\\Pokemon.gba", "D:/Roms/./gb/../gba/A.GBA",
                                            "/home/deck/Roms/Hack.gba", "relative/Thing.sfc"};
    for (const std::string& path : paths) {
        INFO(path);
        CHECK(PathKey(PathFromUtf8(path)) == OldChecksumKey(PathFromUtf8(path)));
    }
    CHECK(PathKey(PathFromUtf8("/home/deck/Roms/")) == "/home/deck/roms");
    CHECK(PathKey(PathFromUtf8("/")) == "/");
    CHECK(IsUnderKey(PathFromUtf8("/home/deck/Roms/gba/a.gba"), PathKey(PathFromUtf8("/home/deck/roms/"))));
    CHECK_FALSE(IsUnderKey(PathFromUtf8("/home/deck/Roms"), "/home/deck/roms"));
    CHECK_FALSE(IsUnderKey(PathFromUtf8("/home/deck/Roms2/a"), "/home/deck/roms"));
    CHECK(IsUnderKey(PathFromUtf8("/a"), "/"));
}

TEST_CASE("percent decoding") {
    CHECK(PercentDecode("Pokemon%20Unbound%2Ev2") == "Pokemon Unbound.v2");
    CHECK(PercentDecode("Caf%C3%a9") == "Caf\xC3\xA9");
    CHECK(PercentDecode("100%") == "100%");
    CHECK(PercentDecode("%4") == "%4");
    CHECK(PercentDecode("%zz%41") == "%zzA");
}
