// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Patch a game": from a patch file the user chose to the game it is for.

#include <algorithm>

#include "den/base/files.h"
#include "den/base/archive.h"
#include "den/library/content.h"
#include "den/library/hacks.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

std::string ExtensionOf(const fs::path& file) {
    const std::string extension = ToLower(PathToUtf8(file.extension()));
    return extension.empty() ? extension : extension.substr(1);
}

// The patch a zip or 7z archive holds, unpacked to `work_dir`.
Result<fs::path> PatchInZip(const fs::path& archive, const fs::path& work_dir, const Job* cancel) {
    const auto members = ListArchive(archive);
    if (!members) {
        return Status::Fail("Cannot read " + PathToUtf8(archive.filename()));
    }
    for (const ZipEntry& member : *members) {
        const fs::path inside = PathFromUtf8(member.name);
        if (!patching::IsPatchExtension(ExtensionOf(inside)) || !IsSafeRelativePath(member.name) ||
            !CanExtractArchiveEntry(member)) {
            continue;
        }
        std::error_code ec;
        fs::create_directories(work_dir, ec);
        const fs::path patch = work_dir / inside.filename();
        const Status unpacked = ExtractArchiveEntry(archive, member, patch, [cancel] {
            return cancel != nullptr && cancel->CancelRequested();
        });
        if (!unpacked) {
            return unpacked;
        }
        return patch;
    }
    return Status::Fail("There is no patch file (IPS, UPS, BPS, APS, PPF, xdelta) in " +
                        PathToUtf8(archive.filename()));
}

} // namespace

Result<PatchFile> OpenPatchFile(Library& library, const fs::path& file, const fs::path& work_dir,
                                const Job* cancel) {
    PatchFile opened;
    opened.patch = file;
    if (const std::string extension = ExtensionOf(file); extension == "rar") {
        return Status::Fail(PathToUtf8(file.filename()) +
                            " is a rar archive, which EmuDEN does not open: unpack it and choose the "
                            "patch file");
    } else if (IsArchiveExtension(extension)) {
        auto inside = PatchInZip(file, work_dir, cancel);
        if (!inside) {
            return inside.status();
        }
        opened.patch = inside.take();
    }
    auto info = patching::Inspect(opened.patch);
    if (!info) {
        return info.status();
    }
    opened.info = info.take();
    opened.title = Trim(PathToUtf8(opened.patch.stem()));

    if (opened.info.source_crc32) {
        // The patch names its game.
        BaseQuery query;
        query.crc32 = opened.info.source_crc32;
        query.size = opened.info.source_size;
        opened.base = HacksOf(library).FindBase(query, cancel);
        if (!opened.base) {
            opened.missing = "This patch needs a game with CRC32 " + patching::CrcText(*query.crc32) +
                             (query.size ? " (" + std::to_string(*query.size) + " bytes)" : "") +
                             " that is not in your library. Add your own copy of that game, then choose "
                             "the patch again.";
        }
        return opened;
    }
    // It does not: the user chooses, among the games of the right size when the patch says it.
    for (Entry& entry : library.Entries()) {
        if (entry.system == "switch2") {
            continue;
        }
        // A cue sheet's own size says nothing about its image.
        if (opened.info.source_size && entry.archive_member.empty() && !ToLower(entry.path).ends_with(".cue")) {
            const std::uint64_t size = entry.size_bytes;
            if (size < *opened.info.source_size || size - *opened.info.source_size > 512) {
                continue;
            }
        }
        opened.choices.push_back(std::move(entry));
    }
    if (opened.choices.empty()) {
        opened.missing = opened.info.source_size
                             ? "This patch needs a game of " + std::to_string(*opened.info.source_size) +
                                   " bytes, and none in your library has that size."
                             : "There is no game in your library to patch yet.";
    }
    return opened;
}

} // namespace den::library
