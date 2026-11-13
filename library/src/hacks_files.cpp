// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <utility>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/store/document.h"
#include "hacks_store.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

constexpr int CHECKSUMS_VERSION = 1;
constexpr std::array<std::string_view, 4> IMAGE_EXT{".png", ".jpg", ".jpeg", ".webp"};
constexpr std::string_view HACKS_FOLDER = "Hacks";

fs::path SidecarPath(const fs::path& rom) {
    fs::path file = rom;
    file += ".hack.json";
    return file;
}

std::string Key(const fs::path& path) {
    std::string key = ToLower(PathToUtf8(path.lexically_normal()));
    std::replace(key.begin(), key.end(), '\\', '/');
    return key;
}

double Number(const JsonValue& object, std::string_view key) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Number ? value->number : 0.0;
}

bool CanWriteIn(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path probe = dir / ".emuden-write-test";
    {
        std::ofstream file(probe, std::ios::binary);
        if (!file) {
            return false;
        }
    }
    fs::remove(probe, ec);
    return true;
}

bool HasSystemFolder(const SystemTable& systems, const fs::path& root) {
    for (const fs::path& entry : ListDirectory(root)) {
        if (IsDirectory(entry) && systems.FromFolderName(PathToUtf8(entry.filename())) != nullptr) {
            return true;
        }
    }
    return false;
}

// The console's folder a file of this library folder is in, as the scan sees it; empty when
// the file is a loose one.
fs::path ConsoleFolder(const fs::path& file, const Folder& folder, const SystemTable& systems) {
    const fs::path root = PathFromUtf8(folder.path);
    const std::string file_key = Key(file);
    const std::string root_key = Key(root);
    if (file_key.size() <= root_key.size() + 1 || !file_key.starts_with(root_key) ||
        file_key[root_key.size()] != '/') {
        return {};
    }
    if (!folder.system.empty() && systems.Find(folder.system) != nullptr) {
        return root;
    }
    if (!HasSystemFolder(systems, root)) {
        fs::path named = root.has_filename() ? root : root.parent_path();
        return systems.FromFolderName(PathToUtf8(named.filename())) != nullptr ? root : fs::path();
    }
    const fs::path below = file.lexically_relative(root);
    if (below.empty() || std::distance(below.begin(), below.end()) < 2) {
        return {};
    }
    const fs::path first = *below.begin();
    return systems.FromFolderName(PathToUtf8(first)) != nullptr ? root / first : fs::path();
}

} // namespace

std::optional<Sidecar> ReadSidecar(const fs::path& rom) {
    const auto text = ReadTextFile(SidecarPath(rom));
    const auto json = text ? ParseJson(*text) : std::nullopt;
    if (!json || json->kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    Sidecar sidecar;
    sidecar.title = json->String("title");
    sidecar.version = json->String("version");
    sidecar.description = json->String("description");
    sidecar.base_id = json->String("base_id");
    sidecar.base_sha1 = json->String("base_sha1");
    sidecar.patch_sha256 = json->String("patch_sha256");
    sidecar.plugin = json->String("plugin");
    sidecar.source = json->String("source");
    sidecar.installed = static_cast<std::int64_t>(Number(*json, "installed"));
    return sidecar;
}

bool WriteSidecar(const fs::path& rom, const Sidecar& sidecar) {
    return WriteFileAtomic(SidecarPath(rom), WriteJson(JsonObject({
                                                 {"title", JsonString(sidecar.title)},
                                                 {"version", JsonString(sidecar.version)},
                                                 {"description", JsonString(sidecar.description)},
                                                 {"base_id", JsonString(sidecar.base_id)},
                                                 {"base_sha1", JsonString(sidecar.base_sha1)},
                                                 {"patch_sha256", JsonString(sidecar.patch_sha256)},
                                                 {"plugin", JsonString(sidecar.plugin)},
                                                 {"source", JsonString(sidecar.source)},
                                                 {"installed", JsonNumber(static_cast<double>(sidecar.installed))},
                                             })));
}

void RemoveSidecar(const fs::path& rom) {
    std::error_code ec;
    fs::remove(SidecarPath(rom), ec);
}

void AddHackFolders(const fs::path& library_dir, std::vector<Folder>& folders) {
    for (const fs::path& dir : ListDirectory(library_dir / "hacks")) {
        if (IsDirectory(dir)) {
            folders.push_back({PathToUtf8(dir), PathToUtf8(dir.filename())});
        }
    }
}

void ApplyHackSidecars(std::vector<Entry>& entries) {
    for (Entry& entry : entries) {
        const fs::path rom = PathFromUtf8(entry.path);
        if (!EqualsNoCase(PathToUtf8(rom.parent_path().filename()), HACKS_FOLDER)) {
            continue;
        }
        const auto sidecar = ReadSidecar(rom);
        if (!sidecar) {
            continue;
        }
        entry.hack_of = sidecar->base_id;
        if (!sidecar->title.empty()) {
            entry.name = sidecar->title;
        }
    }
    for (Entry& entry : entries) {
        if (entry.hack_of.empty() || !entry.cover.empty()) {
            continue;
        }
        const auto base = std::find_if(entries.begin(), entries.end(),
                                       [&](const Entry& other) { return other.id == entry.hack_of; });
        if (base != entries.end() && !base->placeholder_cover) {
            entry.cover = base->cover;
        }
    }
}

HackPlace PlaceForHack(const Entry& base, const std::vector<Folder>& folders, const SystemTable& systems,
                       const fs::path& library_dir) {
    const fs::path file = PathFromUtf8(base.path);
    for (const Folder& folder : folders) {
        const fs::path console = ConsoleFolder(file, folder, systems);
        if (!console.empty() && CanWriteIn(console / PathFromUtf8(HACKS_FOLDER))) {
            return {console / PathFromUtf8(HACKS_FOLDER), console};
        }
    }
    const fs::path root = library_dir / "hacks" / PathFromUtf8(base.system);
    std::error_code ec;
    fs::create_directories(root / PathFromUtf8(HACKS_FOLDER), ec);
    return {root / PathFromUtf8(HACKS_FOLDER), root};
}

std::string SafeFileName(std::string_view title) {
    std::string name;
    for (const char c : title) {
        const auto byte = static_cast<unsigned char>(c);
        const bool refused = byte < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos;
        if (!refused && c != ' ') {
            name.push_back(c);
        } else if (!name.empty() && name.back() != ' ') {
            name.push_back(' ');
        }
    }
    // Not in the middle of a UTF-8 character.
    constexpr std::size_t LIMIT = 120;
    if (name.size() > LIMIT) {
        std::size_t cut = LIMIT;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        name.resize(cut);
    }
    name = Trim(name);
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
        name.pop_back();
    }
    return name.empty() ? "Hack" : name;
}

fs::path CoverBeside(const fs::path& rom) {
    for (const std::string_view extension : IMAGE_EXT) {
        fs::path image = rom;
        image.replace_extension(PathFromUtf8(extension));
        if (IsRegularFile(image)) {
            return image;
        }
    }
    return {};
}

fs::path PutCoverBeside(const fs::path& rom, const fs::path& image) {
    const std::string extension = ToLower(PathToUtf8(image.extension()));
    if (std::find(IMAGE_EXT.begin(), IMAGE_EXT.end(), extension) == IMAGE_EXT.end() || !IsRegularFile(image)) {
        return {};
    }
    std::error_code ec;
    for (fs::path old = CoverBeside(rom); !old.empty(); old = CoverBeside(rom)) {
        if (!fs::remove(old, ec)) {
            return {};
        }
    }
    fs::path cover = rom;
    cover.replace_extension(PathFromUtf8(extension));
    fs::copy_file(image, cover, fs::copy_options::overwrite_existing, ec);
    return ec ? fs::path() : cover;
}

ChecksumCache::ChecksumCache(fs::path file) : file_(std::move(file)) {}

void ChecksumCache::Load() {
    loaded_ = true;
    const auto document = store::LoadDocument(file_);
    const JsonValue* files = document && document->version == CHECKSUMS_VERSION ? document->data.Find("files") : nullptr;
    if (files == nullptr) {
        return;
    }
    for (const JsonValue& item : files->items) {
        Known known;
        known.size = static_cast<std::uint64_t>(Number(item, "size"));
        known.written = item.String("written");
        known.system = item.String("system");
        if (const JsonValue* forms = item.Find("forms"); forms != nullptr) {
            for (const JsonValue& form : forms->items) {
                patching::RomForm rom;
                rom.form = form.String("form");
                rom.sums.crc32 = static_cast<std::uint32_t>(std::strtoul(form.String("crc32").c_str(), nullptr, 16));
                rom.sums.md5 = form.String("md5");
                rom.sums.sha1 = form.String("sha1");
                rom.sums.size = static_cast<std::uint64_t>(Number(form, "size"));
                rom.header_bytes = static_cast<std::uint64_t>(Number(form, "header"));
                const JsonValue* reordered = form.Find("reordered");
                rom.reordered = reordered != nullptr && reordered->boolean;
                known.forms.push_back(std::move(rom));
            }
        }
        if (const std::string path = item.String("path"); !path.empty() && !known.forms.empty()) {
            known_[path] = std::move(known);
        }
    }
}

void ChecksumCache::Save() const {
    JsonValue files = JsonArray();
    for (const auto& [path, known] : known_) {
        JsonValue forms = JsonArray();
        for (const patching::RomForm& form : known.forms) {
            forms.items.push_back(JsonObject({
                {"form", JsonString(form.form)},
                {"crc32", JsonString(patching::CrcText(form.sums.crc32))},
                {"md5", JsonString(form.sums.md5)},
                {"sha1", JsonString(form.sums.sha1)},
                {"size", JsonNumber(static_cast<double>(form.sums.size))},
                {"header", JsonNumber(static_cast<double>(form.header_bytes))},
                {"reordered", JsonBool(form.reordered)},
            }));
        }
        files.items.push_back(JsonObject({
            {"path", JsonString(path)},
            {"size", JsonNumber(static_cast<double>(known.size))},
            {"written", JsonString(known.written)},
            {"system", JsonString(known.system)},
            {"forms", std::move(forms)},
        }));
    }
    store::SaveDocument(file_, CHECKSUMS_VERSION, JsonObject({{"files", std::move(files)}}));
}

Result<std::vector<patching::RomForm>> ChecksumCache::Get(const fs::path& rom, std::string_view system,
                                                          const std::function<bool()>& cancelled) {
    std::error_code ec;
    const std::uint64_t size = fs::file_size(rom, ec);
    if (ec) {
        return Status::Fail("Cannot read " + PathToUtf8(rom));
    }
    const std::string written = std::to_string(fs::last_write_time(rom, ec).time_since_epoch().count());
    const std::string key = Key(rom);
    {
        std::lock_guard lock(mutex_);
        if (!loaded_) {
            Load();
        }
        const auto found = known_.find(key);
        if (found != known_.end() && found->second.size == size && found->second.written == written &&
            found->second.system == system) {
            return found->second.forms;
        }
    }
    auto forms = patching::RomChecksums(rom, system, cancelled);
    if (!forms) {
        return forms;
    }
    std::lock_guard lock(mutex_);
    known_[key] = {size, written, std::string(system), forms.value()};
    dirty_ = true;
    return forms;
}

void ChecksumCache::Flush() {
    std::lock_guard lock(mutex_);
    if (std::exchange(dirty_, false)) {
        Save();
    }
}

} // namespace den::library
