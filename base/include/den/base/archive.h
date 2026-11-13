// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Archives a game or a patch may come in: zip (den/base/zip.h) and 7z. Which one a file is
// is read from its first bytes, not from its name. rar is not read.

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "den/base/zip.h"

namespace den {

/// "zip" or "7z" (lower case, no dot).
bool IsArchiveExtension(std::string_view extension);

/// The file starts like a 7z archive.
bool IsSevenZip(const std::filesystem::path& archive);

/// An open archive. A 7z keeps the block it unpacked last in memory, so the files of one
/// solid block are unpacked once however many of them are written out; use one reader for
/// several files and let it go when done.
class ArchiveReader {
public:
    /// Nothing when the file is neither a zip nor a 7z this build can list (a 7z whose
    /// file names are behind a password, a damaged one).
    static std::unique_ptr<ArchiveReader> Open(const std::filesystem::path& archive);
    virtual ~ArchiveReader() = default;

    /// The files (folders left out).
    virtual const std::vector<ZipEntry>& Entries() const = 0;
    /// As ExtractZipEntry: `destination` appears only once complete, the checksum is checked.
    virtual Status Extract(const ZipEntry& entry, const std::filesystem::path& destination,
                           const std::function<bool()>& cancelled = {}) = 0;
};

/// The files of a zip or a 7z archive.
std::optional<std::vector<ZipEntry>> ListArchive(const std::filesystem::path& archive);

/// Zip: as CanExtractZipEntry. 7z: copy, LZMA, LZMA2, the BCJ and delta filters, no
/// password, and a solid block of at most 1 GiB (it is unpacked in memory).
bool CanExtractArchiveEntry(const ZipEntry& entry);

/// One file of the archive. For several files of a 7z use an ArchiveReader.
Status ExtractArchiveEntry(const std::filesystem::path& archive, const ZipEntry& entry,
                           const std::filesystem::path& destination,
                           const std::function<bool()>& cancelled = {});

} // namespace den
