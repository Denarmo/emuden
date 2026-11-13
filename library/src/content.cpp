// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>

#include "den/base/files.h"
#include "den/base/archive.h"
#include "den/library/content.h"
#include "den/library/names.h"
#include "den/library/systems.h"

namespace den::library {

namespace fs = std::filesystem;

bool IsSafeRelativePath(std::string_view relative) {
    if (relative.empty() || relative.find('\0') != std::string_view::npos) {
        return false;
    }
    const fs::path path = PathFromUtf8(relative);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const fs::path& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

std::string ReadRomHeaderTitle(const SystemDef& system, const fs::path& rom) {
    // The first field that holds something. Game Boy: the last bytes of the full field are a
    // maker code and a flag on later cartridges, so its manifest names a shorter one.
    for (const HeaderTitle& field : system.header_title) {
        std::ifstream file(rom, std::ios::binary);
        std::string title(field.length, '\0');
        if (!file.seekg(static_cast<std::streamoff>(field.offset)) ||
            !file.read(title.data(), static_cast<std::streamsize>(title.size()))) {
            continue;
        }
        for (std::size_t i = 0; i < title.size(); ++i) {
            const auto c = static_cast<unsigned char>(title[i]);
            if (c < 0x20 || c >= 0x7F) {
                title.resize(i);
                break;
            }
        }
        if (std::string trimmed = Trim(title); !trimmed.empty()) {
            return trimmed;
        }
    }
    return {};
}

std::string DiscSetName(const fs::path& disc) {
    const std::string file = PathToUtf8(disc.filename());
    std::string name = NameBeforeDisc(file);
    if (RegionTags(name).empty()) {
        std::string regions;
        for (const std::string& region : RegionTags(file)) {
            regions += (regions.empty() ? "" : ", ") + region;
        }
        if (!regions.empty()) {
            name += " (" + regions + ")";
        }
    }
    return name;
}

std::vector<fs::path> DiscSet(const fs::path& disc) {
    const ParsedName wanted = ParseGameName(PathToUtf8(disc.filename()));
    if (wanted.disc == 0) {
        return {disc};
    }
    // Not the cleaned title: that is the same for every region and revision of the game.
    const std::string wanted_name = ToLower(DiscSetName(disc));
    const std::string extension = ToLower(PathToUtf8(disc.extension()));
    std::map<int, fs::path> found{{wanted.disc, disc}};
    const auto look = [&](const fs::path& dir) {
        std::vector<fs::path> files = ListDirectory(dir);
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            if (ToLower(PathToUtf8(file.extension())) != extension || !IsRegularFile(file)) {
                continue;
            }
            const ParsedName name = ParseGameName(PathToUtf8(file.filename()));
            if (name.disc != 0 && ToLower(DiscSetName(file)) == wanted_name) {
                found.try_emplace(name.disc, file);
            }
        }
    };
    const fs::path folder = disc.parent_path();
    look(folder);
    if (ParseGameName(PathToUtf8(folder.filename())).disc != 0) {
        std::vector<fs::path> dirs = ListDirectory(folder.parent_path());
        std::sort(dirs.begin(), dirs.end());
        for (const fs::path& dir : dirs) {
            if (dir != folder && IsDirectory(dir) &&
                ParseGameName(PathToUtf8(dir.filename())).base == wanted.base) {
                look(dir);
            }
        }
    }
    if (found.size() < 2) {
        return {disc};
    }
    std::vector<fs::path> discs;
    for (const auto& [number, file] : found) {
        discs.push_back(file);
    }
    return discs;
}

Result<fs::path> PrepareRom(const Entry& entry, const fs::path& unpack_dir,
                            const std::function<bool()>& cancelled) {
    const fs::path archive = PathFromUtf8(entry.path);
    if (entry.archive_member.empty()) {
        return archive;
    }
    // One reader for all the files: a solid 7z block is unpacked once.
    const auto reader = ArchiveReader::Open(archive);
    if (!reader || !IsSafeRelativePath(entry.archive_member)) {
        return Status::Fail("cannot read " + entry.path);
    }
    const std::vector<ZipEntry>* members = &reader->Entries();

    // One folder per archive as it is now: a changed archive is unpacked again.
    std::error_code ec;
    const std::uint64_t size = fs::file_size(archive, ec);
    const auto changed = fs::last_write_time(archive, ec).time_since_epoch().count();
    const std::string identity =
        entry.path + "|" + std::to_string(size) + "|" +
        std::to_string(static_cast<std::int64_t>(changed));
    static constexpr char HEX[] = "0123456789abcdef";
    std::uint64_t hash = Fnv1a(FNV_OFFSET, identity.data(), identity.size());
    std::string folder_name;
    for (int i = 0; i < 16; ++i) {
        folder_name.push_back(HEX[hash & 0xF]);
        hash >>= 4;
    }
    const fs::path folder = unpack_dir / PathFromUtf8(folder_name);
    const fs::path marker = folder / ".complete";
    const fs::path member = folder / PathFromUtf8(entry.archive_member);
    if (IsRegularFile(marker) && IsRegularFile(member)) {
        fs::last_write_time(marker, fs::file_time_type::clock::now(), ec);
        return member;
    }

    // Games not played for a month, and unpacking that was interrupted, make room first.
    constexpr auto KEPT = std::chrono::hours(24 * 30);
    for (const fs::path& other : ListDirectory(unpack_dir)) {
        const auto used = fs::last_write_time(other / ".complete", ec);
        if (IsDirectory(other) && (ec || fs::file_time_type::clock::now() - used > KEPT)) {
            fs::remove_all(other, ec);
        }
    }
    fs::create_directories(folder, ec);
    std::uint64_t total = 0;
    for (const ZipEntry& packed : *members) {
        total += packed.size;
    }
    const fs::space_info space = fs::space(folder, ec);
    constexpr std::uint64_t SPARE = 256ull << 20;
    if (!ec && space.available < total + SPARE) {
        fs::remove_all(folder, ec);
        return Status::Fail("not enough free disk space to unpack the game (" +
                              std::to_string((total >> 20) + 1) + " MiB)");
    }
    for (const ZipEntry& packed : *members) {
        if (!IsSafeRelativePath(packed.name)) {
            continue;
        }
        const fs::path file = folder / PathFromUtf8(packed.name);
        fs::create_directories(file.parent_path(), ec);
        if (Status result = reader->Extract(packed, file, cancelled); !result) {
            fs::remove_all(folder, ec);
            return result;
        }
    }
    if (!IsRegularFile(member) || !WriteFileAtomic(marker, "")) {
        fs::remove_all(folder, ec);
        return Status::Fail("cannot unpack " + entry.path);
    }
    return member;
}

} // namespace den::library
