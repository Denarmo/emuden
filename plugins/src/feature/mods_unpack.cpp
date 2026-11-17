// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.mods.unpack: named entries of a zip, a 7z or a tar.xz into the plugin's cache.

#include <algorithm>
#include <map>
#include <system_error>

#include "den/base/archive.h"
#include "den/base/files.h"
#include "den/patching/xz.h"
#include "feature/mods.h"
#include "feature/tar_xz.h"

namespace den::plugins::feature::mods {

namespace {

Status Wrong(const std::string& entry, const char* why) {
    return Status::Fail(entry + " " + why);
}

// Nothing of a failed unpack stays.
void Discard(const std::vector<fs::path>& files) {
    std::error_code ec;
    for (const fs::path& file : files) {
        fs::remove(file, ec);
    }
}

Status UnpackTarXz(const fs::path& archive, const std::vector<std::string>& entries, const fs::path& out_dir,
                   std::uint64_t room, const std::function<bool()>& cancelled) {
    std::map<std::string, bool> done;
    for (const std::string& entry : entries) {
        done[entry] = false;
    }
    std::uint64_t used = 0;
    std::vector<fs::path> written;
    Status read = ReadTarXz(
        archive,
        [&](const TarMember& member) -> Result<fs::path> {
            const auto wanted = done.find(member.name);
            if (wanted == done.end() || wanted->second) {
                return fs::path();
            }
            if (!member.file) {
                return Wrong(member.name, "is not a file of the archive");
            }
            if (member.size > MAX_ENTRY) {
                return Wrong(member.name, "is too large");
            }
            if (member.size > room - used) {
                return Status::Fail("the plugin's folder is full");
            }
            used += member.size;
            wanted->second = true;
            const fs::path destination = out_dir / PathFromUtf8(member.name);
            std::error_code ec;
            fs::create_directories(destination.parent_path(), ec);
            written.push_back(destination);
            return destination;
        },
        cancelled);
    for (const std::string& entry : entries) {
        if (read.ok && !done[entry]) {
            read = Wrong(entry, "is not in the archive");
        }
    }
    if (!read.ok) {
        Discard(written);
    }
    return read;
}

Status UnpackArchive(const fs::path& archive, const std::vector<std::string>& entries, const fs::path& out_dir,
                     std::uint64_t room, const std::function<bool()>& cancelled) {
    const std::unique_ptr<ArchiveReader> reader = ArchiveReader::Open(archive);
    if (!reader) {
        return Status::Fail("the file is not a zip, 7z or tar.xz archive");
    }
    std::vector<const ZipEntry*> wanted;
    std::uint64_t total = 0;
    for (const std::string& entry : entries) {
        const auto found = std::find_if(reader->Entries().begin(), reader->Entries().end(),
                                        [&](const ZipEntry& e) { return e.name == entry; });
        if (found == reader->Entries().end()) {
            return Wrong(entry, "is not in the archive");
        }
        if (found->symlink || found->encrypted || !CanExtractArchiveEntry(*found)) {
            return Wrong(entry, "is not a file this app can unpack");
        }
        if (found->size > MAX_ENTRY) {
            return Wrong(entry, "is too large");
        }
        total += found->size;
        wanted.push_back(&*found);
    }
    if (total > room) {
        return Status::Fail("the plugin's folder is full");
    }
    std::vector<fs::path> made;
    for (const ZipEntry* entry : wanted) {
        const fs::path destination = out_dir / PathFromUtf8(entry->name);
        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        fs::remove(destination, ec);
        made.push_back(destination);
        if (const Status written = reader->Extract(*entry, destination, cancelled); !written.ok) {
            Discard(made);
            return Status::Fail(entry->name + ": " + written.error);
        }
    }
    return {};
}

} // namespace

std::string ArchiveStem(std::string_view file_name) {
    const std::string lower = ToLower(file_name);
    for (const std::string_view extension : {".tar.xz", ".txz", ".zip", ".7z"}) {
        if (lower.size() > extension.size() && lower.ends_with(extension)) {
            return std::string(file_name.substr(0, file_name.size() - extension.size()));
        }
    }
    return std::string(file_name);
}

Status Unpack(const fs::path& archive, const std::vector<std::string>& entries, const fs::path& out_dir,
              std::uint64_t room, const std::function<bool()>& cancelled) {
    if (entries.empty() || entries.size() > MAX_ENTRIES) {
        return Status::Fail("unpack takes 1 to 32 entries");
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!IsInnerPath(entries[i])) {
            return Wrong(entries[i], "is not a path inside the archive");
        }
        if (std::find(entries.begin(), entries.begin() + static_cast<std::ptrdiff_t>(i), entries[i]) !=
            entries.begin() + static_cast<std::ptrdiff_t>(i)) {
            return Wrong(entries[i], "is named twice");
        }
    }
    std::error_code ec;
    if (!fs::is_regular_file(fs::symlink_status(archive, ec))) {
        return Status::Fail("there is no such archive");
    }
    fs::create_directories(out_dir, ec);
    if (ec) {
        return Status::Fail("the plugin's folder cannot be written");
    }
    return patching::IsXzFile(archive) ? UnpackTarXz(archive, entries, out_dir, room, cancelled)
                                       : UnpackArchive(archive, entries, out_dir, room, cancelled);
}

} // namespace den::plugins::feature::mods
