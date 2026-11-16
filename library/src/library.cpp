// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <utility>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/base/names.h"
#include "den/library/names.h"
#include "cover_files.h"
#include "hacks_store.h"
#include "library_impl.h"

namespace den::library {

namespace fs = std::filesystem;

std::string FolderKey(const std::string& path) {
    return PathKey(PathFromUtf8(path));
}

bool IsUnder(const std::string& file, const std::string& folder_key) {
    return IsUnderKey(PathFromUtf8(file), folder_key);
}

void SortByName(std::vector<Entry>& entries) {
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const std::string left = ToLower(a.name);
        const std::string right = ToLower(b.name);
        return left != right ? left < right : a.id < b.id;
    });
}

RealLibrary::RealLibrary(const Config& config)
    : config_(config), dir_(config.data_dir / "library"),
      artwork_(dir_ / "artwork",
               config.offline ? HttpFetcher() : config.http ? config.http : HttpFetcher(PlatformGet),
               config.offline || config.http ? HeaderFetcher() : HeaderFetcher(PlatformGetWithHeaders),
               config.http ? 0 : config.cover_request_interval_ms) {
    const auto saved = LoadSaved(dir_ / "library.json");
    if (saved) {
        saved_ = *saved;
    } else {
        // The first run adds no folder of its own; an earlier app's list is taken over.
        fs::path profile = config.import_profile;
        if (profile.empty()) {
            profile = PathFromUtf8(GetEnv("EMUDEN_IMPORT_PROFILE"));
        }
        if (!profile.empty()) {
            const Status imported = ImportFolders(profile);
            log::Info("library", "import from {}: {}", PathToUtf8(profile),
                      imported ? std::to_string(saved_.folders.size()) + " folders" : imported.error);
        }
        SaveSaved(dir_ / "library.json", saved_);
    }
    entries_ = LoadEntryCache(dir_ / "cache.json");
    for (Entry& entry : entries_) {
        ApplyRecord(entry);
        if (!entry.cover.empty() && !IsRegularFile(PathFromUtf8(entry.cover))) {
            entry.cover.clear();
            entry.placeholder_cover = false;
        }
    }
    SortByName(entries_);
    hacks_ = CreateHacks({this, config.data_dir, [this] { return SystemsCopy(); },
                          [this](Entry entry) { AdoptEntry(std::move(entry)); },
                          [this](const std::string& id) { DropEntry(id); }});
    if (!saved_.folders.empty() && config_.jobs != nullptr) {
        Scan();
    }
}

RealLibrary::~RealLibrary() {
    if (scan_job_) {
        scan_job_->Cancel();
    }
    std::unique_lock lock(gate_->mutex);
    gate_->closed = true;
    gate_->idle.wait(lock, [&] { return !gate_->running; });
}

std::vector<Folder> RealLibrary::Folders() const {
    std::lock_guard lock(mutex_);
    return saved_.folders;
}

Status RealLibrary::AddFolder(const std::string& path, const std::string& system) {
    const fs::path folder = PathFromUtf8(path).lexically_normal();
    if (path.empty() || !IsDirectory(folder)) {
        return Status::Fail("There is no folder " + path);
    }
    {
        std::lock_guard lock(mutex_);
        if (!system.empty() && systems_.Find(system) == nullptr) {
            return Status::Fail("Unknown console " + system);
        }
        if (!AddFolderLocked(PathToUtf8(folder), system)) {
            return Status::Fail("That folder is already in the library");
        }
        SaveSaved(dir_ / "library.json", saved_);
        ++revision_;
    }
    if (config_.jobs != nullptr) {
        Scan();
    }
    return {};
}

Status RealLibrary::RemoveFolder(const std::string& path) {
    std::lock_guard lock(mutex_);
    const std::string key = FolderKey(path);
    const std::size_t before = saved_.folders.size();
    std::erase_if(saved_.folders, [&](const Folder& f) { return FolderKey(f.path) == key; });
    if (saved_.folders.size() == before) {
        return Status::Fail("That folder is not in the library");
    }
    std::erase_if(entries_, [&](const Entry& entry) { return IsUnder(entry.path, key); });
    SaveSaved(dir_ / "library.json", saved_);
    SaveEntryCache(dir_ / "cache.json", entries_);
    ++revision_;
    return {};
}

JobPtr RealLibrary::Scan() {
    std::lock_guard lock(mutex_);
    if (config_.jobs == nullptr) {
        return nullptr;
    }
    if (scan_active_ && scan_job_ && !scan_job_->Finished()) {
        // Folders added meanwhile are picked up by one more pass.
        rescan_ = true;
        return scan_job_;
    }
    scan_active_ = true;
    rescan_ = false;
    scan_job_ = config_.jobs->Run("Scanning the library", [this, gate = gate_](Job& job) {
        {
            std::lock_guard enter(gate->mutex);
            if (gate->closed) {
                return Status{};
            }
            gate->running = true;
        }
        do {
            RunScan(job);
        } while (TakeRescan(job));
        std::lock_guard leave(gate->mutex);
        gate->running = false;
        gate->idle.notify_all();
        return Status{};
    });
    return scan_job_;
}

void RealLibrary::SetSystems(std::vector<SystemDef> systems) {
    std::lock_guard lock(mutex_);
    systems_.Set(std::move(systems));
    ++revision_;
}

Status RealLibrary::ImportFolders(const fs::path& old_profile) {
    const auto folders = ReadOldGameFolders(old_profile);
    if (!folders) {
        return Status::Fail("No earlier profile in " + PathToUtf8(old_profile));
    }
    Status status;
    bool added = false;
    {
        std::lock_guard lock(mutex_);
        for (const std::string& folder : *folders) {
            const fs::path path = PathFromUtf8(folder).lexically_normal();
            if (!IsDirectory(path)) {
                status.notes.push_back("Not found: " + folder);
            } else if (AddFolderLocked(PathToUtf8(path), {})) {
                status.notes.push_back("Added " + folder);
                added = true;
            }
        }
        if (added) {
            SaveSaved(dir_ / "library.json", saved_);
            ++revision_;
        }
    }
    if (added && config_.jobs != nullptr && constructed_) {
        Scan();
    }
    return status;
}

void RealLibrary::MarkConstructed() {
    constructed_ = true;
}

Hacks* RealLibrary::HackStore() {
    return hacks_.get();
}

// A game whose console is gone (its plugin was removed) stays in the cache, hidden, until
// the next scan drops it; reinstalling the plugin brings it back.
bool RealLibrary::Shown(const Entry& entry) const {
    return entry.system.empty() || systems_.Find(entry.system) != nullptr;
}

SystemTable RealLibrary::Consoles() const {
    return SystemsCopy();
}

SystemTable RealLibrary::SystemsCopy() const {
    std::lock_guard lock(mutex_);
    return systems_;
}

bool RealLibrary::AddFolderLocked(const std::string& path, const std::string& system) {
    const std::string key = FolderKey(path);
    for (const Folder& folder : saved_.folders) {
        if (FolderKey(folder.path) == key) {
            return false;
        }
    }
    saved_.folders.push_back({path, system});
    return true;
}

SystemTable Library::Consoles() const {
    return SystemTable();
}

std::unique_ptr<Library> CreateLibrary(const Config& config) {
    auto library = std::make_unique<RealLibrary>(config);
    library->MarkConstructed();
    return library;
}

} // namespace den::library
