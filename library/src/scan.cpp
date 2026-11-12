// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>

#include "den/base/files.h"
#include "den/base/zip.h"
#include "den/library/content.h"
#include "den/library/names.h"
#include "den/library/scan.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

constexpr std::array<std::string_view, 4> IMAGE_EXT{".png", ".jpg", ".jpeg", ".webp"};
constexpr std::array<std::string_view, 4> ART_FOLDERS{"media", "covers", "boxart", "images"};
constexpr int MAX_DEPTH = 3;

bool Contains(const std::vector<std::string>& names, std::string_view value) {
    return std::find(names.begin(), names.end(), value) != names.end();
}

std::string PathKey(const fs::path& file) {
    return ToLower(PathToUtf8(file.lexically_normal()));
}

std::string Hex16(std::uint64_t value) {
    static constexpr char HEX[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 0; i < 16; ++i) {
        out[15 - i] = HEX[(value >> (4 * i)) & 0xF];
    }
    return out;
}

struct Scan {
    const ScanOptions& options;
    const SystemTable& systems;
    std::set<std::string> seen;
    std::vector<Entry> out;

    bool Cancelled() const {
        return options.cancelled && options.cancelled();
    }
};

// The files that are a game of the system although no index file (cue sheet) names them.
bool IsLooseMember(const SystemDef& def, std::string_view extension) {
    return def.multi_file && def.multi_file->loose_members &&
           Contains(def.multi_file->members, extension);
}

fs::path FindImage(const fs::path& dir, const std::string& stem) {
    for (const std::string_view ext : IMAGE_EXT) {
        const fs::path candidate = dir / PathFromUtf8(stem + std::string(ext));
        if (IsRegularFile(candidate)) {
            return candidate;
        }
    }
    return {};
}

fs::path FindRomCover(const fs::path& rom, const std::string& stem, const SystemDef& def,
                      const ScanOptions& options) {
    if (!options.covers_dir.empty()) {
        if (fs::path found = FindImage(options.covers_dir / PathFromUtf8(def.id), stem);
            !found.empty()) {
            return found;
        }
    }
    const fs::path dir = rom.parent_path();
    // A PICO-8 cart is its own picture; anything else must not match itself.
    if (fs::path found = FindImage(dir, stem); !found.empty() && found != rom) {
        return found;
    }
    for (const std::string_view folder : ART_FOLDERS) {
        if (fs::path found = FindImage(dir / PathFromUtf8(folder), stem); !found.empty()) {
            return found;
        }
    }
    return {};
}

// How well a file of an archive serves as the one to start a core with, lower is better; -1
// for a file that is not a game. A playlist or cue sheet comes before the images it names.
int MemberRank(const SystemDef& def, const std::string& extension) {
    if (extension == "m3u" && Contains(def.extensions, extension)) {
        return 0;
    }
    for (std::size_t i = 0; i < def.extensions.size(); ++i) {
        if (def.extensions[i] == extension) {
            return static_cast<int>(i) + 1;
        }
    }
    // A disc image without its cue sheet still plays.
    return IsLooseMember(def, extension) ? 100 : -1;
}

std::uint64_t FileSize(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    return ec ? 0 : static_cast<std::uint64_t>(size);
}

void AddSwitch2(Scan& scan, const fs::path& file, const std::string& stem, const std::string& relative) {
    Entry entry;
    entry.system = "switch2";
    entry.path = PathToUtf8(file);
    entry.original_name = stem;
    entry.size_bytes = FileSize(file);
    std::uint64_t program_id = ProgramIdFromName(stem);
    std::string title;
    fs::path icon;
    if (!scan.options.icons_dir.empty() && program_id != 0) {
        // Known from an earlier scan: the package need not be read for its picture again.
        icon = FindImage(scan.options.icons_dir, Switch2Id(program_id).substr(8));
    }
    if (auto info = ReadSwitch2Package(file, icon.empty() && !scan.options.icons_dir.empty())) {
        program_id = info->program_id != 0 ? info->program_id : program_id;
        title = std::move(info->title);
        if (!info->icon.empty() && program_id != 0) {
            const bool png = info->icon.starts_with("\x89PNG");
            std::error_code ec;
            fs::create_directories(scan.options.icons_dir, ec);
            icon = scan.options.icons_dir /
                   PathFromUtf8(Switch2Id(program_id).substr(8) + (png ? ".png" : ".jpg"));
            if (!WriteFileAtomic(icon, info->icon)) {
                icon.clear();
            }
        } else if (icon.empty() && !scan.options.icons_dir.empty() && program_id != 0) {
            icon = FindImage(scan.options.icons_dir, Switch2Id(program_id).substr(8));
        }
    }
    entry.id = program_id != 0 ? Switch2Id(program_id) : RomId("switch2", relative);
    if (!scan.seen.insert(entry.id).second) {
        return;
    }
    if (title.empty()) {
        title = std::string(KnownSwitch2Title(program_id));
    }
    entry.name = title.empty() ? ParseGameName(stem).base : CleanStoreTitle(title);
    entry.cover = icon.empty() ? std::string() : PathToUtf8(icon);
    scan.out.push_back(std::move(entry));
}

// `root` is the console's folder the file was found under (empty for a loose file).
void AddRom(Scan& scan, const fs::path& file, const SystemDef* def, const fs::path& root,
            bool loose_image = false) {
    const std::string name = PathToUtf8(file.filename());
    const std::string extension = scan.systems.RomExtension(ToLower(name));
    std::string archive_member;
    // "no": this console's games are never zipped. "pass": its emulator opens the zip itself.
    const std::string zip_rule = def != nullptr ? def->zip : "unpack";
    const bool passed_zip = extension == "zip" && zip_rule == "pass";
    if (extension == "zip" && zip_rule == "no") {
        return;
    }
    if (extension == "zip" && !passed_zip) {
        const auto members = ListZip(file);
        if (!members) {
            return;
        }
        int best = -1;
        for (const ZipEntry& member : *members) {
            const std::string member_extension = scan.systems.RomExtension(ToLower(member.name));
            // Outside a system folder the archive's content says which console it is for.
            const SystemDef* member_def =
                def != nullptr ? def : scan.systems.FromUniqueExtension(member_extension);
            if (member_def == nullptr || !IsSafeRelativePath(member.name) ||
                !CanExtractZipEntry(member)) {
                continue;
            }
            const int rank = MemberRank(*member_def, member_extension);
            if (rank >= 0 && (best < 0 || rank < best)) {
                best = rank;
                archive_member = member.name;
                def = member_def;
            }
        }
        if (archive_member.empty()) {
            return;
        }
    }
    if (def == nullptr || (archive_member.empty() && !loose_image && !passed_zip &&
                           !Contains(def->extensions, extension))) {
        return;
    }
    const std::string stem =
        extension.empty() ? name : name.substr(0, name.size() - extension.size() - 1);
    std::string relative = name;
    if (!root.empty()) {
        if (const fs::path below = file.lexically_relative(root); !below.empty()) {
            relative = PathToUtf8(below);
        }
    }
    if (def->scanner == "switch2") {
        AddSwitch2(scan, file, stem, relative);
        return;
    }
    Entry entry;
    entry.id = RomId(def->id, relative);
    if (!scan.seen.insert(entry.id).second) {
        return;
    }
    const ParsedName parsed = ParseGameName(stem);
    entry.name = parsed.base;
    entry.original_name = stem;
    entry.disc = parsed.disc;
    entry.system = def->id;
    entry.path = PathToUtf8(file);
    entry.archive_member = std::move(archive_member);
    entry.size_bytes = FileSize(file);
    fs::path cover = FindRomCover(file, stem, *def, scan.options);
    // A cart that is a picture is its own cover.
    if (cover.empty() && def->cover_is_rom && extension.ends_with("png")) {
        cover = file;
    }
    entry.cover = cover.empty() ? std::string() : PathToUtf8(cover);
    scan.out.push_back(std::move(entry));
}

void ScanSystemDir(Scan& scan, const fs::path& dir, const SystemDef& def, const fs::path& root,
                   int depth) {
    std::vector<fs::path> entries = ListDirectory(dir);
    std::sort(entries.begin(), entries.end());
    std::vector<fs::path> images;
    std::set<std::string> described;
    const bool loose_members = def.multi_file && def.multi_file->loose_members;
    for (const fs::path& entry : entries) {
        if (scan.Cancelled()) {
            return;
        }
        if (IsDirectory(entry)) {
            const std::string lower = ToLower(PathToUtf8(entry.filename()));
            if (depth < MAX_DEPTH &&
                std::find(ART_FOLDERS.begin(), ART_FOLDERS.end(), lower) == ART_FOLDERS.end() &&
                !lower.starts_with('.')) {
                ScanSystemDir(scan, entry, def, root, depth + 1);
            }
            continue;
        }
        AddRom(scan, entry, &def, root);
        if (!loose_members) {
            continue;
        }
        std::string extension = ToLower(PathToUtf8(entry.extension()));
        if (!extension.empty()) {
            extension.erase(0, 1);
        }
        if (IsLooseMember(def, extension)) {
            images.push_back(entry);
        } else if (extension == "cue") {
            for (std::string& named : FilesNamedBy(entry)) {
                described.insert(std::move(named));
            }
            // A cue sheet that names its image by another path still means the one beside it.
            for (const std::string& member : def.multi_file->members) {
                fs::path beside = entry;
                described.insert(PathKey(beside.replace_extension(PathFromUtf8("." + member))));
            }
        }
    }
    // A disc image without a cue sheet (PlayStation) is a game of its own; the later tracks
    // of one are not.
    for (const fs::path& image : images) {
        const std::string lower = ToLower(PathToUtf8(image.filename()));
        const std::size_t track = def.multi_file->track_naming == "redump" ? lower.find("(track ")
                                                                           : std::string::npos;
        const bool later_track = track != std::string::npos &&
                                 !lower.substr(track).starts_with("(track 1)") &&
                                 !lower.substr(track).starts_with("(track 01)");
        if (!described.contains(PathKey(image)) && !later_track) {
            AddRom(scan, image, &def, root, true);
        }
    }
}

void ScanMixedDir(Scan& scan, const fs::path& root) {
    std::vector<fs::path> entries = ListDirectory(root);
    std::sort(entries.begin(), entries.end());
    for (const fs::path& entry : entries) {
        if (scan.Cancelled()) {
            return;
        }
        const std::string name = PathToUtf8(entry.filename());
        if (IsDirectory(entry)) {
            if (const SystemDef* def = scan.systems.FromFolderName(name)) {
                ScanSystemDir(scan, entry, *def, entry, 0);
            }
            continue;
        }
        const std::string extension = scan.systems.RomExtension(ToLower(name));
        const SystemDef* def = scan.systems.FromUniqueExtension(extension);
        if (def != nullptr || extension == "zip") {
            AddRom(scan, entry, def, {});
        }
    }
}

bool HasSystemFolder(const SystemTable& systems, const fs::path& root) {
    for (const fs::path& entry : ListDirectory(root)) {
        if (IsDirectory(entry) && systems.FromFolderName(PathToUtf8(entry.filename())) != nullptr) {
            return true;
        }
    }
    return false;
}

// Keeps one entry per game where the files differ in region only.
void GroupRegions(std::vector<Entry>& entries, const std::vector<std::string>& order) {
    using Key = std::tuple<std::string, std::string, int>;
    std::map<Key, std::vector<std::size_t>> same_game;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        const std::string match = MatchKey(entry.original_name);
        if (entry.system != "switch2" && !match.empty()) {
            same_game[{entry.system, match, entry.disc}].push_back(i);
        }
    }
    std::set<std::size_t> hidden;
    for (auto& [key, members] : same_game) {
        // Versions differ in region; files of one region are different games (or copies).
        std::set<std::vector<std::string>> regions;
        bool distinct = members.size() > 1;
        for (const std::size_t member : members) {
            const std::vector<std::string> tags = RegionTags(entries[member].original_name);
            distinct = distinct && !tags.empty() && regions.insert(tags).second;
        }
        if (!distinct) {
            continue;
        }
        std::stable_sort(members.begin(), members.end(), [&](std::size_t a, std::size_t b) {
            return RegionRank(RegionTags(entries[a].original_name), order) <
                   RegionRank(RegionTags(entries[b].original_name), order);
        });
        Entry& shown = entries[members.front()];
        for (std::size_t i = 1; i < members.size(); ++i) {
            shown.other_versions.push_back(entries[members[i]].path);
            hidden.insert(members[i]);
        }
    }
    std::size_t index = 0;
    std::erase_if(entries, [&](const Entry&) { return hidden.contains(index++); });
}

} // namespace

std::string RomId(std::string_view system, std::string_view relative_path) {
    std::string key = ToLower(relative_path);
    std::replace(key.begin(), key.end(), '\\', '/');
    return std::string(system) + ":" + Hex16(Fnv1a(FNV_OFFSET, key.data(), key.size()));
}

std::vector<std::string> FilesNamedBy(const fs::path& list) {
    std::vector<std::string> out;
    const auto text = ReadTextFile(list);
    if (!text || text->size() > (1u << 20)) {
        return out;
    }
    const bool cue = ToLower(PathToUtf8(list.extension())) == ".cue";
    std::size_t at = 0;
    while (at < text->size()) {
        const std::size_t end = std::min(text->find('\n', at), text->size());
        std::string line = Trim(text->substr(at, end - at));
        at = end + 1;
        if (cue) {
            const std::size_t open = line.find('"');
            const std::size_t close = line.rfind('"');
            if (!ToLower(line).starts_with("file") || open == std::string::npos || close <= open) {
                continue;
            }
            line = line.substr(open + 1, close - open - 1);
        } else if (line.empty() || line.front() == '#') {
            continue;
        }
        fs::path file = PathFromUtf8(line);
        if (file.is_relative()) {
            file = list.parent_path() / file;
        }
        out.push_back(PathKey(file));
    }
    return out;
}

std::vector<Entry> ScanFolders(const ScanOptions& options) {
    static const SystemTable builtin;
    Scan scan{options, options.systems != nullptr ? *options.systems : builtin, {}, {}};
    for (const Folder& folder : options.folders) {
        if (scan.Cancelled()) {
            break;
        }
        const fs::path root = PathFromUtf8(folder.path);
        const SystemDef* def = folder.system.empty() ? nullptr : scan.systems.Find(folder.system);
        if (def == nullptr && !HasSystemFolder(scan.systems, root)) {
            fs::path named = root;
            if (!named.has_filename()) {
                named = named.parent_path();
            }
            def = scan.systems.FromFolderName(PathToUtf8(named.filename()));
        }
        if (def != nullptr) {
            ScanSystemDir(scan, root, *def, root, 0);
        } else {
            ScanMixedDir(scan, root);
        }
    }
    std::vector<Entry>& out = scan.out;
    // A playlist is the game; the discs it names are not listed beside it.
    std::set<std::string> in_playlists;
    std::set<std::tuple<std::string, std::string, int>> unpacked;
    for (const Entry& entry : out) {
        if (!entry.archive_member.empty()) {
            continue;
        }
        unpacked.insert({entry.system, ToLower(entry.original_name), entry.disc});
        if (ToLower(entry.path).ends_with(".m3u")) {
            for (std::string& named : FilesNamedBy(PathFromUtf8(entry.path))) {
                in_playlists.insert(std::move(named));
            }
        }
    }
    std::erase_if(out, [&](const Entry& entry) {
        if (!entry.archive_member.empty()) {
            // The same game unpacked next to its archive starts without unpacking.
            return unpacked.contains({entry.system, ToLower(entry.original_name), entry.disc});
        }
        return in_playlists.contains(PathKey(PathFromUtf8(entry.path)));
    });
    if (!options.region_order.empty()) {
        GroupRegions(out, options.region_order);
    }
    // A disc number is only worth showing next to another disc of the same game.
    std::map<std::pair<std::string, std::string>, int> discs;
    for (const Entry& entry : out) {
        if (entry.disc != 0) {
            ++discs[{entry.system, entry.name}];
        }
    }
    for (Entry& entry : out) {
        if (entry.disc != 0 && discs[{entry.system, entry.name}] > 1) {
            entry.name += " (Disc " + std::to_string(entry.disc) + ")";
        }
    }
    return out;
}

} // namespace den::library
