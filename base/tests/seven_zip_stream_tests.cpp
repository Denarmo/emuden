// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 7z blocks decoded in pieces. The archives are packed here with the LZMA SDK's encoder,
// and the reader's limits are lowered so that small blocks take the path large ones do.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include <catch2/catch_test_macros.hpp>

#include <Bra.h>
#include <LzmaEnc.h>

#include "den/base/archive.h"
#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/sha256.h"
#include "seven_zip.h"
#include "seven_zip_writer.h"

using namespace den;
namespace fs = std::filesystem;

namespace {

using Files = std::vector<std::pair<std::string, std::string>>;

// Every block is decoded in pieces.
constexpr SevenZipLimits STREAM_ALL{0, 1ull << 30, 256ull << 20};

void* Allocate(ISzAllocPtr, std::size_t size) {
    return std::malloc(size);
}

void Release(ISzAllocPtr, void* address) {
    std::free(address);
}

const ISzAlloc ALLOC = {Allocate, Release};

std::string Joined(const Files& files) {
    std::string all;
    for (const auto& file : files) {
        all += file.second;
    }
    return all;
}

// The files as one LZMA block with a dictionary of `dictionary` bytes, through the x86
// branch filter first when `bcj`.
std::string LzmaSevenZip(const Files& files, std::uint32_t dictionary, bool bcj = false) {
    std::string data = Joined(files);
    if (bcj) {
        UInt32 state = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
        z7_BranchConvSt_X86_Enc(reinterpret_cast<Byte*>(data.data()), data.size(), 0, &state);
    }
    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = 1;
    props.dictSize = dictionary;
    test::SevenZipCoder coder{"\x03\x01\x01", std::string(LZMA_PROPS_SIZE, '\0'), {}, {}};
    coder.packed.resize(data.size() + data.size() / 3 + (1u << 16));
    SizeT packed_size = coder.packed.size();
    SizeT props_size = LZMA_PROPS_SIZE;
    const SRes code = LzmaEncode(reinterpret_cast<Byte*>(coder.packed.data()), &packed_size,
                                 reinterpret_cast<const Byte*>(data.data()), data.size(), &props,
                                 reinterpret_cast<Byte*>(coder.props.data()), &props_size, 0, nullptr,
                                 &ALLOC, &ALLOC);
    REQUIRE(code == SZ_OK);
    coder.packed.resize(packed_size);
    if (bcj) {
        coder.filter = "\x03\x03\x01\x03";
    }
    return test::SevenZipOf(files, coder);
}

// The files as one LZMA2 block of uncompressed chunks (the decoder still passes them through
// its window), with a dictionary of 1 MiB.
std::string Lzma2SevenZip(const Files& files) {
    const std::string data = Joined(files);
    test::SevenZipCoder coder{"\x21", "\x10", {}, {}};
    for (std::size_t at = 0; at < data.size(); at += 1u << 16) {
        const std::size_t size = std::min<std::size_t>(1u << 16, data.size() - at);
        coder.packed += at == 0 ? '\x01' : '\x02'; // the first resets the dictionary
        coder.packed += static_cast<char>((size - 1) >> 8);
        coder.packed += static_cast<char>((size - 1) & 0xFF);
        coder.packed.append(data, at, size);
    }
    coder.packed += '\0';
    return test::SevenZipOf(files, coder);
}

fs::path FreshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "emuden-base-test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void Put(const fs::path& file, const std::string& data) {
    std::ofstream(file, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string Get(const fs::path& file) {
    const auto data = ReadBinaryFile(file);
    REQUIRE(data);
    return std::string(data->begin(), data->end());
}

// Compressible, but not the same few bytes over and over.
std::string Pattern(std::size_t size, std::uint32_t seed) {
    std::string out(size, '\0');
    std::uint32_t state = seed;
    for (std::size_t i = 0; i < size; ++i) {
        if (i % 4096 == 0) {
            state = state * 1664525u + 1013904223u;
        }
        out[i] = static_cast<char>((i % 251) ^ (state >> 24));
    }
    return out;
}

Files ThreeFiles() {
    return {{"first.bin", Pattern(300000, 1)}, {"game.iso", Pattern(700000, 2)}, {"readme.txt", "hello\n"}};
}

} // namespace

TEST_CASE("7z stream: the middle file of a solid LZMA block, alone and in order") {
    const fs::path dir = FreshDir("stream-middle");
    const Files files = ThreeFiles();
    Put(dir / "three.7z", LzmaSevenZip(files, 1u << 16));

    auto reader = OpenSevenZip(dir / "three.7z", STREAM_ALL);
    REQUIRE(reader);
    const auto& entries = reader->Entries();
    REQUIRE(entries.size() == 3);
    for (const ZipEntry& entry : entries) {
        CHECK(entry.streams);
        CHECK(entry.window == 1u << 16);
        CHECK(CanExtractArchiveEntry(entry));
    }
    Status status = reader->Extract(entries[1], dir / "game.iso");
    INFO(status.error);
    REQUIRE(status.ok);
    CHECK(Get(dir / "game.iso") == files[1].second);
    CHECK_FALSE(fs::exists(dir / "game.iso.part"));

    // One reader for all three in order goes on from where it stopped; going back starts over.
    reader = OpenSevenZip(dir / "three.7z", STREAM_ALL);
    for (const int i : {0, 1, 2, 0, 2}) {
        const fs::path out = dir / ("out-" + std::to_string(i));
        status = reader->Extract(reader->Entries()[i], out);
        INFO(i << " " << status.error);
        REQUIRE(status.ok);
        CHECK(Get(out) == files[i].second);
    }
}

TEST_CASE("7z stream: a block many times its dictionary, LZMA and LZMA2") {
    const fs::path dir = FreshDir("stream-large");
    const Files files = {{"disc.iso", Pattern(24u << 20, 7)}, {"tail.bin", Pattern(3u << 20, 9)}};
    Put(dir / "lzma.7z", LzmaSevenZip(files, 1u << 20));
    Put(dir / "lzma2.7z", Lzma2SevenZip(files));
    for (const char* name : {"lzma.7z", "lzma2.7z"}) {
        INFO(name);
        auto reader = OpenSevenZip(dir / name, STREAM_ALL);
        REQUIRE(reader);
        REQUIRE(reader->Entries().size() == 2);
        CHECK(reader->Entries()[0].window == 1u << 20);
        for (std::size_t i = 0; i < 2; ++i) {
            const Status status = reader->Extract(reader->Entries()[i], dir / "out.bin");
            INFO(status.error);
            REQUIRE(status.ok);
            const std::string data = Get(dir / "out.bin");
            CHECK(data.size() == files[i].second.size());
            CHECK(data == files[i].second);
        }
    }
}

// Calls and jumps (E8/E9) every few bytes, so the filter changes something in every piece
// and some fall across the pieces' edges.
std::string Code(std::size_t size) {
    std::string out = Pattern(size, 5);
    for (std::size_t i = 0; i + 5 <= size; i += 7 + i % 13) {
        out[i] = static_cast<char>(i % 2 == 0 ? 0xE8 : 0xE9);
        out[i + 4] = static_cast<char>(i % 3 == 0 ? 0x00 : 0xFF);
    }
    return out;
}

TEST_CASE("7z stream: the x86 filter across pieces") {
    const fs::path dir = FreshDir("stream-bcj");
    const Files files = {{"boot.dol", Code(2u << 20)}, {"main.elf", Code((3u << 20) + 3)}};
    Put(dir / "bcj.7z", LzmaSevenZip(files, 1u << 20, true));
    for (const SevenZipLimits& limits : {SevenZipLimits{}, STREAM_ALL}) {
        auto reader = OpenSevenZip(dir / "bcj.7z", limits);
        REQUIRE(reader);
        for (std::size_t i = 0; i < 2; ++i) {
            CHECK(reader->Entries()[i].streams);
            const Status status = reader->Extract(reader->Entries()[i], dir / "out.bin");
            INFO(limits.memory_block << " " << i << " " << status.error);
            REQUIRE(status.ok);
            CHECK(Get(dir / "out.bin") == files[i].second);
        }
    }
}

TEST_CASE("7z stream: what 7-Zip wrote unpacks the same in pieces") {
    const fs::path dir = FreshDir("stream-vectors");
    const fs::path data = DEN_ARCHIVE_FIXTURES;
    for (const char* vector : {"lzma2-solid.7z", "lzma-each.7z", "delta-lzma2.7z", "copy.7z", "bcj-lzma.7z",
                               "bcj2-lzma.7z"}) {
        INFO(vector);
        auto whole = OpenSevenZip(data / vector);
        auto pieces = OpenSevenZip(data / vector, STREAM_ALL);
        REQUIRE(whole);
        REQUIRE(pieces);
        for (const ZipEntry& entry : pieces->Entries()) {
            INFO(entry.name);
            // BCJ2 has four streams: it stays in memory. An empty file is in no block.
            CHECK(entry.streams == (entry.block_size > 0 && std::string(vector) != "bcj2-lzma.7z"));
            REQUIRE(whole->Extract(entry, dir / "whole.bin").ok);
            const Status status = pieces->Extract(entry, dir / "pieces.bin");
            INFO(status.error);
            REQUIRE(status.ok);
            CHECK(Get(dir / "pieces.bin") == Get(dir / "whole.bin"));
        }
    }
}

TEST_CASE("7z stream: damage, cancelling and a dictionary too large leave no file") {
    const fs::path dir = FreshDir("stream-refused");
    const Files files = ThreeFiles();
    const std::string bytes = LzmaSevenZip(files, 1u << 16);
    Put(dir / "three.7z", bytes);

    // Too large for memory, and its dictionary too large to decode it in pieces.
    auto reader = OpenSevenZip(dir / "three.7z", {0, 1u << 16, 1u << 15});
    REQUIRE(reader);
    Status status = reader->Extract(reader->Entries()[1], dir / "game.iso");
    CHECK(status.error.starts_with("three.7z was packed with a dictionary of"));
    CHECK_FALSE(fs::exists(dir / "game.iso"));

    int asked = 0;
    reader = OpenSevenZip(dir / "three.7z", STREAM_ALL);
    status = reader->Extract(reader->Entries()[1], dir / "game.iso", [&] { return ++asked > 2; });
    CHECK(status.error == "cancelled");
    CHECK_FALSE(fs::exists(dir / "game.iso"));
    CHECK_FALSE(fs::exists(dir / "game.iso.part"));

    // A byte changed halfway through the packed data, which starts after the 32-byte start
    // header and ends where the header starts (its offset is at byte 12).
    std::uint64_t packed = 0;
    for (int i = 7; i >= 0; --i) {
        packed = (packed << 8) | static_cast<unsigned char>(bytes[12 + i]);
    }
    std::string flipped = bytes;
    const std::size_t at = 32 + static_cast<std::size_t>(packed / 2);
    flipped[at] = static_cast<char>(flipped[at] ^ 0x5A);
    Put(dir / "flipped.7z", flipped);
    reader = OpenSevenZip(dir / "flipped.7z", STREAM_ALL);
    REQUIRE(reader);
    status = reader->Extract(reader->Entries()[1], dir / "game.iso");
    CHECK_FALSE(status.ok);
    CHECK(status.error.find("flipped.7z is damaged") != std::string::npos);
    CHECK_FALSE(fs::exists(dir / "game.iso"));
    CHECK_FALSE(fs::exists(dir / "game.iso.part"));
}

TEST_CASE("7z stream: how large a block may be") {
    ZipEntry entry;
    entry.index = 0;
    entry.name = "disc.ciso";
    entry.block_size = 3ull << 30;
    entry.streams = true;
    entry.window = 64ull << 20;
    CHECK(CanExtractArchiveEntry(entry));
    CHECK(ArchiveEntryRefusal("game.7z", entry).empty());
    entry.window = 512ull << 20;
    CHECK(ArchiveEntryRefusal("game.7z", entry) ==
          "game.7z was packed with a dictionary of 512 MiB, more than EmuDEN unpacks with (256 MiB)");
    entry.block_size = 768ull << 20;
    CHECK(CanExtractArchiveEntry(entry)); // in memory instead
    entry.block_size = 3ull << 30;
    entry.streams = false;
    CHECK_FALSE(CanExtractArchiveEntry(entry));
    CHECK(ArchiveEntryRefusal("game.7z", entry).starts_with("game.7z packs its files as one block of 3072 MiB"));
    entry.block_size = 512ull << 20;
    CHECK(CanExtractArchiveEntry(entry)); // in memory
}

// A real archive: EMUDEN_7Z_CHECK=<archive>, EMUDEN_7Z_CHECK_OUT=<scratch folder>. Unpacks
// the largest file with the default limits and prints its SHA-256.
TEST_CASE("7z stream: a real archive named by EMUDEN_7Z_CHECK") {
    const char* archive = std::getenv("EMUDEN_7Z_CHECK");
    const char* out = std::getenv("EMUDEN_7Z_CHECK_OUT");
    if (archive == nullptr || out == nullptr) {
        return;
    }
    const auto reader = ArchiveReader::Open(PathFromUtf8(archive));
    REQUIRE(reader);
    const ZipEntry* largest = nullptr;
    for (const ZipEntry& entry : reader->Entries()) {
        std::cout << entry.name << " " << entry.size << " block " << entry.block_size << " window "
                  << entry.window << (entry.streams ? " streams" : "") << "\n";
        if (largest == nullptr || entry.size > largest->size) {
            largest = &entry;
        }
    }
    REQUIRE(largest != nullptr);
    REQUIRE(CanExtractArchiveEntry(*largest));
    const fs::path target = PathFromUtf8(out) / "unpacked.bin";
    const auto start = std::chrono::steady_clock::now();
    const Status status = reader->Extract(*largest, target);
    INFO(status.error);
    REQUIRE(status.ok);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto sha = Sha256File(target);
    REQUIRE(sha);
    std::cout << largest->name << " unpacked in " << seconds << " s, sha256 " << *sha << "\n";
}
