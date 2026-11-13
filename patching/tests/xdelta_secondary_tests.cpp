// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// xdelta patches whose sections went through a secondary compressor, written by xdelta3
// itself (data/README.md).

#include <catch2/catch_test_macros.hpp>

#include "den/base/hash.h"
#include "den/patching/patch.h"
#include "encoders.h"
#include "vcdiff_writer.h"

using namespace den;
using namespace den::patching;
using namespace den::patching::testing;
namespace fs = std::filesystem;

namespace {

struct Vector {
    const char* name;
    std::size_t target_size;
    std::uint32_t target_crc32;
    bool has_source;
};
// What data/make_vectors.py printed.
constexpr Vector VECTORS[] = {
    {"lzma", 80500, 0x5b530119, true},
    {"djw", 92500, 0x3fd06ea7, true},
    {"fgk", 40000, 0x7aaee3b9, false},
};

fs::path Scratch(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / "emuden-patching-test" / ("secondary-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

Status ApplyBytes(const std::string& name, const Bytes& patch, Bytes* result = nullptr) {
    const fs::path dir = Scratch(name);
    WriteFile(dir / "source.rom", MakeRom(200000, 1));
    WriteFile(dir / "patch.xdelta", patch);
    const Status status = Apply(dir / "patch.xdelta", dir / "source.rom", dir / "out.rom");
    if (result != nullptr) {
        *result = ReadFile(dir / "out.rom");
    }
    REQUIRE(fs::exists(dir / "out.rom") == status.ok);
    return status;
}

Bytes Load(const std::string& name) {
    const Bytes patch = ReadFile(fs::path(DEN_PATCHING_TEST_DATA) / (name + ".xdelta"));
    REQUIRE(patch.size() > 1000);
    return patch;
}

} // namespace

TEST_CASE("xdelta3's own patches: LZMA, DJW and FGK sections") {
    for (const Vector& vector : VECTORS) {
        const std::string name = vector.name;
        INFO(name);
        const Bytes patch = Load(name);
        const fs::path dir = Scratch("info-" + name);
        WriteFile(dir / "p.xdelta", patch);
        auto info = Inspect(dir / "p.xdelta");
        REQUIRE(info.ok());
        REQUIRE(info->format_name == "xdelta");
        REQUIRE(*info->target_size == vector.target_size);

        Bytes result;
        const Status status = ApplyBytes(name, patch, &result);
        INFO(status.error);
        REQUIRE(status.ok);
        REQUIRE(result.size() == vector.target_size);
        REQUIRE(Crc32Update(0, result.data(), result.size()) == vector.target_crc32);
    }
}

TEST_CASE("xdelta3's own patches against another ROM, and damaged") {
    for (const Vector& vector : VECTORS) {
        const std::string name = vector.name;
        INFO(name);
        const Bytes patch = Load(name);
        if (vector.has_source) {
            // Another source: the sections unpack, the window's check fails.
            const fs::path dir = Scratch("other-" + name);
            WriteFile(dir / "source.rom", MakeRom(200000, 2));
            WriteFile(dir / "p.xdelta", patch);
            const Status other = Apply(dir / "p.xdelta", dir / "source.rom", dir / "out.rom");
            REQUIRE(!other.ok);
            REQUIRE(other.error.find("another ROM") != std::string::npos);
            REQUIRE(!fs::exists(dir / "out.rom"));
        }

        // Cut anywhere, or with any byte changed, it is refused and never crashes.
        for (std::size_t cut = 5; cut < patch.size(); cut += 211) {
            REQUIRE(!ApplyBytes(name, Bytes(patch.begin(), patch.begin() + cut)).ok);
        }
        std::uint32_t state = 99;
        for (int round = 0; round < 150; ++round) {
            Bytes broken = patch;
            state = state * 1664525u + 1013904223u;
            const std::size_t at = 5 + (state >> 8) % (broken.size() - 5);
            broken[at] ^= static_cast<std::uint8_t>(1u << (state >> 29));
            Bytes result;
            const Status status = ApplyBytes(name, broken, &result);
            // A flipped bit the format has no check for (padding) may still give the target.
            if (status.ok) {
                REQUIRE(Crc32Update(0, result.data(), result.size()) == vector.target_crc32);
            }
        }
    }
}

TEST_CASE("VCDIFF: sections that claim a compressor") {
    const Bytes rom = MakeRom(200000, 1);
    // No bytes behind the claim.
    VcdiffWriter empty(2);
    empty.Begin(1 | 4, rom.size(), 0);
    empty.Copy(0, rom.data(), 100);
    empty.End(1);
    REQUIRE(!ApplyBytes("empty", empty.out).ok);

    // A compressor nobody knows is named in the message.
    VcdiffWriter unknown(9);
    unknown.Begin(1 | 4, rom.size(), 0);
    unknown.Copy(0, rom.data(), 100);
    unknown.End(2);
    const Status status = ApplyBytes("unknown", unknown.out);
    REQUIRE(!status.ok);
    REQUIRE(status.error.find("does not know") != std::string::npos);
}
