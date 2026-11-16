// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>

#include "den/base/files.h"
#include "den/base/hash.h"
#include "den/base/log.h"
#include "den/base/archive.h"
#include "den/library/content.h"
#include "den/library/hacks.h"
#include "den/library/scan.h"
#include "hacks_store.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool IsZip(const std::string& path) {
    const std::string lower = ToLower(path);
    return lower.ends_with(".zip") || lower.ends_with(".7z");
}

bool Wanted(const BaseQuery& query, const patching::RomForm& form) {
    return (!query.crc32 || *query.crc32 == form.sums.crc32) &&
           (query.sha1.empty() || ToLower(query.sha1) == form.sums.sha1) &&
           (query.md5.empty() || ToLower(query.md5) == form.sums.md5) &&
           (!query.size || *query.size == form.sums.size);
}

// A header some forms leave out is this long at most.
bool SizeMayFit(std::uint64_t file_size, std::uint64_t wanted) {
    return file_size >= wanted && file_size - wanted <= 512;
}

// The archive knows the size and CRC-32 of the game in it: only a likely match is unpacked.
bool ZipMayMatch(const fs::path& archive, const Entry& entry, const BaseQuery& query) {
    const auto members = ListArchive(archive);
    if (!members) {
        return false;
    }
    // Consoles whose ROMs have a second form, which the archive's CRC says nothing about.
    const bool other_forms =
        entry.system == "nes" || entry.system == "snes" || entry.system == "n64" || entry.system == "lynx";
    for (const ZipEntry& member : *members) {
        if (member.name == entry.archive_member) {
            return (!query.size || SizeMayFit(member.size, *query.size)) &&
                   (!query.crc32 || !member.crc_known || member.crc == *query.crc32 || other_forms);
        }
    }
    return false;
}

// Moves a finished file into place, also across volumes.
bool MoveFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) {
        return true;
    }
    fs::path partial = to;
    partial += ".part";
    fs::copy_file(from, partial, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
        fs::rename(partial, to, ec);
    }
    if (ec) {
        fs::remove(partial, ec);
        return false;
    }
    fs::remove(from, ec);
    return true;
}

// The file of a game that a patch changes: the ROM itself, or the image a cue sheet names.
fs::path PatchedFileOf(const fs::path& rom) {
    if (ToLower(PathToUtf8(rom.extension())) != ".cue") {
        return rom;
    }
    for (const std::string& named : FilesNamedBy(rom)) {
        const fs::path image = PathFromUtf8(named);
        if (IsRegularFile(image)) {
            return rom.parent_path() / image.filename();
        }
    }
    return rom;
}

class RealHacks final : public Hacks {
public:
    explicit RealHacks(HackHooks hooks)
        : hooks_(std::move(hooks)), library_dir_(hooks_.data_dir / "library"),
          checksums_(library_dir_ / "checksums.json") {}

    std::optional<BaseMatch> FindBase(const BaseQuery& query, const Job* cancel) override {
        if (!query.crc32 && query.sha1.empty() && query.md5.empty()) {
            return std::nullopt;
        }
        const auto cancelled = [cancel] { return cancel != nullptr && cancel->CancelRequested(); };
        std::vector<Entry> entries = hooks_.library->Entries();
        // The user's own dumps before hacks made of them.
        std::stable_partition(entries.begin(), entries.end(), [](const Entry& e) { return e.hack_of.empty(); });
        std::optional<BaseMatch> match;
        for (const Entry& entry : entries) {
            if (cancelled()) {
                break;
            }
            const bool asked = query.system.empty()
                                   ? query.systems.empty() ||
                                         std::find(query.systems.begin(), query.systems.end(), entry.system) !=
                                             query.systems.end()
                                   : entry.system == query.system;
            if (asked && (match = MatchEntry(entry, query, cancelled))) {
                break;
            }
        }
        checksums_.Flush();
        return match;
    }

    Result<Entry> InstallHack(const HackRequest& request, std::function<bool(float)> progress) override {
        const auto base = hooks_.library->Find(request.base_id);
        if (!base) {
            return Status::Fail("That game is not in the library any more");
        }
        if (!request.patched.empty()) {
            return Place(request, *base, request.patched, PathToUtf8(request.patched.extension()), {});
        }
        const auto info = patching::Inspect(request.patch);
        if (!info) {
            return info.status();
        }
        const fs::path work = hooks_.data_dir / "cache" / "patching" /
                              PathFromUtf8(std::to_string(UnixNow()) + "-" + std::to_string(++counter_));
        std::error_code ec;
        fs::create_directories(work, ec);
        Result<Entry> result = Install(request, *base, info.value(), work, std::move(progress));
        fs::remove_all(work, ec);
        checksums_.Flush();
        if (!result) {
            log::Warn("library", "hack {} not installed: {}", request.title, result.error());
        }
        return result;
    }

    std::vector<Hack> List(const std::string& plugin) const override {
        std::vector<Hack> hacks;
        for (const Entry& entry : hooks_.library->Entries()) {
            if (entry.hack_of.empty()) {
                continue;
            }
            const Sidecar sidecar = ReadSidecar(PathFromUtf8(entry.path)).value_or(Sidecar{});
            if (plugin.empty() || sidecar.plugin == plugin) {
                hacks.push_back({entry.id, entry.hack_of, entry.name, sidecar.version, sidecar.plugin,
                                 sidecar.source, sidecar.installed});
            }
        }
        return hacks;
    }

    Status Update(const std::string& id, const std::string& plugin, const std::optional<std::string>& title,
                  const std::optional<std::string>& description, const fs::path& cover) override {
        Entry entry;
        Sidecar sidecar;
        if (Status allowed = Own(id, plugin, entry, sidecar); !allowed) {
            return allowed;
        }
        const fs::path rom = PathFromUtf8(entry.path);
        if (title && !Trim(*title).empty()) {
            sidecar.title = Trim(*title);
            entry.name = sidecar.title;
        }
        if (description) {
            sidecar.description = *description;
        }
        if (!cover.empty()) {
            const fs::path placed = PutCoverBeside(rom, cover);
            if (placed.empty()) {
                return Status::Fail("Cannot use " + PathToUtf8(cover.filename()) + " as the cover");
            }
            entry.cover = PathToUtf8(placed);
            entry.placeholder_cover = false;
        }
        if (!WriteSidecar(rom, sidecar)) {
            return Status::Fail("Cannot write beside " + entry.path);
        }
        hooks_.adopt(std::move(entry));
        return {};
    }

    Status Remove(const std::string& id, const std::string& plugin) override {
        Entry entry;
        Sidecar sidecar;
        if (Status allowed = Own(id, plugin, entry, sidecar); !allowed) {
            return allowed;
        }
        const fs::path rom = PathFromUtf8(entry.path);
        std::error_code ec;
        fs::remove(rom, ec);
        if (ec) {
            return Status::Fail("Cannot delete " + entry.path);
        }
        RemoveSidecar(rom);
        for (fs::path cover = CoverBeside(rom); !cover.empty() && fs::remove(cover, ec);) {
            cover = CoverBeside(rom);
        }
        // The last hack takes its Hacks folder with it.
        if (EqualsNoCase(PathToUtf8(rom.parent_path().filename()), "Hacks") && fs::is_empty(rom.parent_path(), ec)) {
            fs::remove(rom.parent_path(), ec);
        }
        hooks_.drop(id);
        return {};
    }

private:
    // The entry's file or regional version that has the wanted checksum.
    std::optional<BaseMatch> MatchEntry(const Entry& entry, const BaseQuery& query,
                                        const std::function<bool()>& cancelled) {
        std::vector<std::string> files{entry.path};
        files.insert(files.end(), entry.other_versions.begin(), entry.other_versions.end());
        for (const std::string& path : files) {
            const bool own = &path == &files.front();
            fs::path file = PathFromUtf8(path);
            if (own && !entry.archive_member.empty()) {
                if (!ZipMayMatch(file, entry, query)) {
                    continue;
                }
                auto unpacked = PrepareRom(entry, hooks_.data_dir / "cache" / "content", cancelled);
                if (!unpacked) {
                    continue;
                }
                file = unpacked.take();
            } else if (IsZip(path)) {
                continue;
            } else {
                file = PatchedFileOf(file);
                std::error_code ec;
                const std::uint64_t size = fs::file_size(file, ec);
                if (ec || (query.size && !SizeMayFit(size, *query.size))) {
                    continue;
                }
            }
            const auto forms = checksums_.Get(file, entry.system, cancelled);
            if (!forms) {
                continue;
            }
            for (const patching::RomForm& form : forms.value()) {
                if (Wanted(query, form)) {
                    return BaseMatch{entry, form.form, path};
                }
            }
        }
        return std::nullopt;
    }

    Result<Entry> Install(const HackRequest& request, const Entry& base, const patching::PatchInfo& info,
                          const fs::path& work, std::function<bool(float)> progress) {
        // Which of the game's files, in which form, the patch was made for.
        BaseQuery query;
        query.crc32 = info.source_crc32;
        query.size = info.source_size;
        std::optional<BaseMatch> match;
        if (query.crc32) {
            match = MatchEntry(base, query, {});
        }
        Entry source_entry = base;
        if (match && match->path != base.path) {
            source_entry.path = match->path;
            source_entry.archive_member.clear();
        }
        auto prepared = PrepareRom(source_entry, hooks_.data_dir / "cache" / "content");
        if (!prepared) {
            return prepared.status();
        }
        const fs::path original = PatchedFileOf(prepared.take());
        auto forms = checksums_.Get(original, base.system);
        if (!forms) {
            return forms.status();
        }
        patching::RomForm form = forms.value().front();
        for (const patching::RomForm& candidate : forms.value()) {
            if (match ? candidate.form == match->form
                      : !info.source_crc32 && info.source_size && candidate.sums.size == *info.source_size) {
                form = candidate;
                break;
            }
        }
        if (info.source_crc32 && !match) {
            return Status::Fail("This patch is for another game or version: it needs CRC32 " +
                                patching::CrcText(*info.source_crc32) +
                                (info.source_size ? " (" + std::to_string(*info.source_size) + " bytes)" : "") +
                                "; your " + base.name + " has CRC32 " + patching::CrcText(form.sums.crc32) + " (" +
                                std::to_string(form.sums.size) + " bytes)");
        }

        fs::path source = original;
        if (form.form != "raw") {
            source = work / "source";
            if (Status written = patching::WriteForm(original, base.system, form.form, source); !written) {
                return written;
            }
        }
        const fs::path patched = work / "patched";
        patching::ApplyOptions options;
        options.progress = std::move(progress);
        if (Status applied = patching::Apply(request.patch, source, patched, options); !applied) {
            return applied;
        }
        if (Status restored = patching::RestoreHeader(original, form, patched); !restored) {
            return restored;
        }
        return Place(request, base, patched, form.reordered ? ".z64" : PathToUtf8(original.extension()),
                     forms.value().front().sums.sha1);
    }

    // Moves a patched ROM into the hacks folder under the title, with what says it is a hack.
    Result<Entry> Place(const HackRequest& request, const Entry& base, const fs::path& patched,
                        const std::string& extension, const std::string& base_sha1) {
        const HackPlace place = PlaceForHack(base, hooks_.library->Folders(), hooks_.systems(), library_dir_);
        const std::string title = Trim(request.title).empty() ? base.name + " (Hack)" : Trim(request.title);
        std::string file_name = SafeFileName(title);
        fs::path target = place.dir / PathFromUtf8(file_name + extension);
        const auto installed = ReadSidecar(target);
        if (IsRegularFile(target) && !installed) {
            return Status::Fail("There is already a file named " + PathToUtf8(target.filename()) + " in " +
                                PathToUtf8(place.dir) + " that is not a hack this app made");
        }
        if (installed && request.keep_old && installed->version != request.version && !request.version.empty()) {
            // The installed version stays as it is, with its saves; this one is a game beside it.
            file_name = SafeFileName(title + " (" + request.version + ")");
            target = place.dir / PathFromUtf8(file_name + extension);
        }
        if (!MoveFile(patched, target)) {
            return Status::Fail("Cannot write " + PathToUtf8(target));
        }

        Sidecar sidecar;
        sidecar.title = title;
        sidecar.version = request.version;
        sidecar.description = request.description;
        sidecar.base_id = base.id;
        sidecar.base_sha1 = base_sha1;
        sidecar.patch_sha256 = request.patch.empty() ? std::string() : Sha256File(request.patch).value_or(std::string());
        sidecar.plugin = request.plugin;
        sidecar.source = request.source;
        sidecar.installed = UnixNow();
        if (!WriteSidecar(target, sidecar)) {
            return Status::Fail("Cannot write beside " + PathToUtf8(target));
        }

        Entry entry;
        entry.id = RomId(base.system, PathToUtf8(target.lexically_relative(place.root)));
        entry.name = title;
        entry.system = base.system;
        entry.path = PathToUtf8(target);
        entry.original_name = file_name;
        entry.hack_of = base.id;
        std::error_code ec;
        entry.size_bytes = fs::file_size(target, ec);
        fs::path cover = request.cover.empty() ? CoverBeside(target) : PutCoverBeside(target, request.cover);
        if (!cover.empty()) {
            entry.cover = PathToUtf8(cover);
        } else if (!base.placeholder_cover) {
            entry.cover = base.cover;
        }
        hooks_.adopt(entry);
        return hooks_.library->Find(entry.id).value_or(entry);
    }

    // The hack `id` when `plugin` may change it.
    Status Own(const std::string& id, const std::string& plugin, Entry& entry, Sidecar& sidecar) const {
        const auto found = hooks_.library->Find(id);
        const auto beside = found ? ReadSidecar(PathFromUtf8(found->path)) : std::nullopt;
        if (!found || found->hack_of.empty() || !beside) {
            return Status::Fail("That is not an installed hack");
        }
        if (!plugin.empty() && beside->plugin != plugin) {
            return Status::Fail("That hack was not installed by this plugin");
        }
        entry = *found;
        sidecar = *beside;
        return {};
    }

    HackHooks hooks_;
    fs::path library_dir_;
    ChecksumCache checksums_;
    std::atomic<int> counter_{0};
};

class NoHacks final : public Hacks {
public:
    std::optional<BaseMatch> FindBase(const BaseQuery&, const Job*) override {
        return std::nullopt;
    }
    Result<Entry> InstallHack(const HackRequest&, std::function<bool(float)>) override {
        return Status::Fail("This library cannot hold hacks");
    }
    std::vector<Hack> List(const std::string&) const override {
        return {};
    }
    Status Update(const std::string&, const std::string&, const std::optional<std::string>&,
                  const std::optional<std::string>&, const fs::path&) override {
        return Status::Fail("This library cannot hold hacks");
    }
    Status Remove(const std::string&, const std::string&) override {
        return Status::Fail("This library cannot hold hacks");
    }
};

} // namespace

std::unique_ptr<Hacks> CreateHacks(HackHooks hooks) {
    return std::make_unique<RealHacks>(std::move(hooks));
}

Hacks& HacksOf(Library& library) {
    static NoHacks none;
    Hacks* hacks = library.HackStore();
    return hacks != nullptr ? *hacks : none;
}

} // namespace den::library
