// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 7z archives. The files under base/tests/data were written by 7-Zip 26.00
// (make_vectors.py there); the sizes and checksums below are the ones it printed.

#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "den/base/archive.h"
#include "den/base/files.h"
#include "seven_zip_writer.h"

using namespace den;
namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t TEXT_SIZE = 34124, CODE_SIZE = 24000;
constexpr std::uint32_t TEXT_CRC = 0x7E8D9736, CODE_CRC = 0xEFCB5B3E;
const std::string UNICODE_NAME = "Pok\xC3\xA9mon \xE3\x83\x9D\xE3\x82\xB1.txt";

fs::path Vector(const char* name) {
    return fs::path(DEN_ARCHIVE_FIXTURES) / name;
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

const ZipEntry* Named(const std::vector<ZipEntry>& entries, const std::string& name) {
    for (const ZipEntry& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

// Unpacks `name` from the vector and checks what was written.
void CheckFile(const char* vector, const std::string& name, std::uint64_t size, std::uint32_t crc) {
    INFO(vector << " " << name);
    const auto entries = ListArchive(Vector(vector));
    REQUIRE(entries);
    const ZipEntry* entry = Named(*entries, name);
    REQUIRE(entry != nullptr);
    CHECK(entry->size == size);
    CHECK(entry->crc_known);
    CHECK(entry->crc == crc);
    REQUIRE(CanExtractArchiveEntry(*entry));
    const fs::path out = FreshDir("unpack") / "file.bin";
    const Status status = ExtractArchiveEntry(Vector(vector), *entry, out);
    INFO(status.error);
    REQUIRE(status.ok);
    const std::string data = Get(out);
    CHECK(data.size() == size);
    CHECK(Crc32(data.data(), data.size()) == crc);
    CHECK_FALSE(fs::exists(out.parent_path() / "file.bin.part"));
}

} // namespace

TEST_CASE("7z: LZMA, LZMA2, BCJ, BCJ2, delta and stored files are unpacked") {
    for (const char* vector : {"lzma2-solid.7z", "lzma-each.7z", "delta-lzma2.7z", "copy.7z"}) {
        CheckFile(vector, "text.txt", TEXT_SIZE, TEXT_CRC);
        CheckFile(vector, "sub/code.bin", CODE_SIZE, CODE_CRC);
    }
    CheckFile("bcj-lzma.7z", "sub/code.bin", CODE_SIZE, CODE_CRC);
    CheckFile("bcj2-lzma.7z", "sub/code.bin", CODE_SIZE, CODE_CRC);
}

TEST_CASE("7z: names, empty files and one reader for a solid block") {
    const auto reader = ArchiveReader::Open(Vector("lzma2-solid.7z"));
    REQUIRE(reader);
    const std::vector<ZipEntry>& entries = reader->Entries();
    CHECK(entries.size() == 4); // the folder "sub" is not a file
    const ZipEntry* unicode = Named(entries, UNICODE_NAME);
    const ZipEntry* empty = Named(entries, "empty.txt");
    const ZipEntry* text = Named(entries, "text.txt");
    const ZipEntry* code = Named(entries, "sub/code.bin");
    REQUIRE((unicode && empty && text && code));
    CHECK(text->index >= 0);
    CHECK(text->block_size == TEXT_SIZE + CODE_SIZE + 25);
    CHECK_FALSE(text->encrypted);
    CHECK(empty->size == 0);

    const fs::path dir = FreshDir("solid");
    REQUIRE(reader->Extract(*code, dir / "code.bin").ok);
    REQUIRE(reader->Extract(*unicode, dir / "name.txt").ok);
    REQUIRE(reader->Extract(*empty, dir / "empty.txt").ok);
    REQUIRE(reader->Extract(*text, dir / "text.txt").ok);
    CHECK(Get(dir / "name.txt") == "names are UTF-16 in a 7z\n");
    CHECK(fs::file_size(dir / "empty.txt") == 0);
    CHECK(fs::file_size(dir / "text.txt") == TEXT_SIZE);
    CHECK(Crc32(Get(dir / "code.bin").data(), CODE_SIZE) == CODE_CRC);

    // An entry of another archive is not taken for one of this.
    ZipEntry other = *text;
    other.size += 1;
    CHECK_FALSE(reader->Extract(other, dir / "other.txt").ok);
    // Cancelled before anything is written.
    CHECK(reader->Extract(*text, dir / "never.txt", [] { return true; }).error == "cancelled");
    CHECK_FALSE(fs::exists(dir / "never.txt"));
}

TEST_CASE("7z: a password and a method that is not read are refused, not guessed") {
    auto entries = ListArchive(Vector("password.7z"));
    REQUIRE(entries);
    REQUIRE(entries->size() == 1);
    CHECK(entries->front().name == "text.txt");
    CHECK(entries->front().encrypted);
    CHECK_FALSE(CanExtractArchiveEntry(entries->front()));
    const fs::path dir = FreshDir("refused");
    Status status = ExtractArchiveEntry(Vector("password.7z"), entries->front(), dir / "text.txt");
    CHECK(status.error == "password.7z needs a password, which EmuDEN does not ask for");
    CHECK(fs::is_empty(dir));

    // With the names behind the password too there is nothing to list.
    CHECK_FALSE(ListArchive(Vector("password-names.7z")));

    entries = ListArchive(Vector("ppmd.7z"));
    REQUIRE(entries);
    REQUIRE(entries->size() == 1);
    CHECK(entries->front().unsupported);
    CHECK_FALSE(CanExtractArchiveEntry(entries->front()));
    status = ExtractArchiveEntry(Vector("ppmd.7z"), entries->front(), dir / "text.txt");
    CHECK(status.error == "text.txt is packed in a way this build cannot read");
    CHECK(fs::is_empty(dir));
}

TEST_CASE("7z: a damaged archive gives an error and no file") {
    const fs::path dir = FreshDir("damaged");
    const std::string bytes = Get(Vector("lzma-each.7z"));
    REQUIRE(IsSevenZip(Vector("lzma-each.7z")));

    // A byte of the packed data: the header still lists the files.
    std::string flipped = bytes;
    flipped[2000] = static_cast<char>(flipped[2000] ^ 0x55);
    Put(dir / "flipped.7z", flipped);
    const auto entries = ListArchive(dir / "flipped.7z");
    REQUIRE(entries);
    REQUIRE(entries->size() == 2);
    // Each file is a block of its own here: the one the byte is in fails, the other is whole.
    int damaged = 0;
    for (const ZipEntry& entry : *entries) {
        const fs::path out = dir / PathFromUtf8(entry.name).filename();
        const Status status = ExtractArchiveEntry(dir / "flipped.7z", entry, out);
        if (!status.ok) {
            ++damaged;
            CHECK(status.error.find("is damaged (" + entry.name + ")") != std::string::npos);
            CHECK_FALSE(fs::exists(out));
            CHECK_FALSE(fs::exists(out.string() + ".part"));
        }
    }
    CHECK(damaged == 1);

    // Cut short, a broken header, and the bare signature.
    Put(dir / "short.7z", bytes.substr(0, bytes.size() - 40));
    CHECK_FALSE(ListArchive(dir / "short.7z"));
    std::string header = bytes;
    header[header.size() - 5] = static_cast<char>(header[header.size() - 5] ^ 0x55);
    Put(dir / "header.7z", header);
    CHECK_FALSE(ListArchive(dir / "header.7z"));
    Put(dir / "start.7z", bytes.substr(0, 6));
    CHECK_FALSE(ListArchive(dir / "start.7z"));
    Put(dir / "text.7z", "not an archive at all");
    CHECK_FALSE(IsSevenZip(dir / "text.7z"));
    CHECK_FALSE(ListArchive(dir / "text.7z"));
    CHECK_FALSE(ListArchive(dir / "missing.7z"));
}

TEST_CASE("7z: the stored archive the tests write is one a reader takes") {
    const fs::path dir = FreshDir("written");
    const std::string big(70000, 'x');
    Put(dir / "three.7z", test::StoredSevenZip({{"readme.txt", "hello"},
                                                {"hack/Crystal Clear.bps", "BPS1 more"},
                                                {"caf\xC3\xA9/big.bin", big}}));
    const auto reader = ArchiveReader::Open(dir / "three.7z");
    REQUIRE(reader);
    REQUIRE(reader->Entries().size() == 3);
    CHECK(reader->Entries()[1].name == "hack/Crystal Clear.bps");
    CHECK(reader->Entries()[1].crc == Crc32("BPS1 more", 9));
    CHECK(reader->Entries()[2].name == "caf\xC3\xA9/big.bin");
    REQUIRE(reader->Extract(reader->Entries()[1], dir / "patch.bps").ok);
    CHECK(Get(dir / "patch.bps") == "BPS1 more");
    REQUIRE(reader->Extract(reader->Entries()[2], dir / "big.bin").ok);
    CHECK(Get(dir / "big.bin") == big);

    Put(dir / "one.7z", test::StoredSevenZip({{"only.ips", "PATCHEOF"}}));
    const auto one = ListArchive(dir / "one.7z");
    REQUIRE(one);
    REQUIRE(one->size() == 1);
    REQUIRE(ExtractArchiveEntry(dir / "one.7z", one->front(), dir / "only.ips").ok);
    CHECK(Get(dir / "only.ips") == "PATCHEOF");

    // What a file is comes from its first bytes: a 7z under a zip's name is still read.
    fs::copy_file(dir / "one.7z", dir / "renamed.zip");
    const auto renamed = ListArchive(dir / "renamed.zip");
    REQUIRE(renamed);
    CHECK(renamed->front().name == "only.ips");
    CHECK(IsArchiveExtension("7z"));
    CHECK(IsArchiveExtension("zip"));
    CHECK_FALSE(IsArchiveExtension("rar"));
}
