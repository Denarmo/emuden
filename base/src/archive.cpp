// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <fstream>

#include "den/base/archive.h"
#include "seven_zip.h"

namespace den {

namespace fs = std::filesystem;

namespace {

constexpr unsigned char SEVEN_ZIP_SIGNATURE[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};

class ZipReader final : public ArchiveReader {
public:
    ZipReader(fs::path archive, std::vector<ZipEntry> entries)
        : archive_(std::move(archive)), entries_(std::move(entries)) {}

    const std::vector<ZipEntry>& Entries() const override {
        return entries_;
    }

    Status Extract(const ZipEntry& entry, const fs::path& destination,
                   const std::function<bool()>& cancelled) override {
        return ExtractZipEntry(archive_, entry, destination, cancelled);
    }

private:
    fs::path archive_;
    std::vector<ZipEntry> entries_;
};

} // namespace

bool IsArchiveExtension(std::string_view extension) {
    return extension == "zip" || extension == "7z";
}

bool IsSevenZip(const fs::path& archive) {
    std::ifstream file(archive, std::ios::binary);
    char start[sizeof(SEVEN_ZIP_SIGNATURE)] = {};
    file.read(start, sizeof(start));
    return file.gcount() == static_cast<std::streamsize>(sizeof(start)) &&
           std::memcmp(start, SEVEN_ZIP_SIGNATURE, sizeof(start)) == 0;
}

std::unique_ptr<ArchiveReader> ArchiveReader::Open(const fs::path& archive) {
    if (IsSevenZip(archive)) {
        return OpenSevenZip(archive);
    }
    auto entries = ListZip(archive);
    if (!entries) {
        return nullptr;
    }
    return std::make_unique<ZipReader>(archive, std::move(*entries));
}

std::optional<std::vector<ZipEntry>> ListArchive(const fs::path& archive) {
    const auto reader = ArchiveReader::Open(archive);
    if (!reader) {
        return std::nullopt;
    }
    return reader->Entries();
}

bool CanExtractArchiveEntry(const ZipEntry& entry) {
    if (entry.index < 0) {
        return CanExtractZipEntry(entry);
    }
    return !entry.encrypted && !entry.symlink && !entry.unsupported &&
           entry.block_size <= SEVEN_ZIP_MAX_BLOCK;
}

Status ExtractArchiveEntry(const fs::path& archive, const ZipEntry& entry, const fs::path& destination,
                           const std::function<bool()>& cancelled) {
    if (entry.index < 0) {
        return ExtractZipEntry(archive, entry, destination, cancelled);
    }
    const auto reader = OpenSevenZip(archive);
    if (!reader) {
        return Status::Fail("cannot read " + PathToUtf8(archive));
    }
    return reader->Extract(entry, destination, cancelled);
}

} // namespace den
