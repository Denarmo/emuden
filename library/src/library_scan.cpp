// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A scan's work, and the covers: looked up after a scan, chosen or set by the user.

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

#include "den/base/files.h"
#include "den/library/names.h"
#include "den/library/scan.h"
#include "den/store/settings.h"
#include "cover_files.h"
#include "hacks_store.h"
#include "library_impl.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

constexpr unsigned ARTWORK_THREADS = 4;

} // namespace

// Whether the scan has to run once more; if not, the next Scan() starts a new job.
bool RealLibrary::TakeRescan(const Job& job) {
    std::lock_guard lock(mutex_);
    if (std::exchange(rescan_, false) && !job.CancelRequested()) {
        return true;
    }
    scan_active_ = false;
    return false;
}

void RealLibrary::RunScan(Job& job) {
    ScanOptions options;
    SystemTable systems;
    {
        std::lock_guard lock(mutex_);
        options.folders = saved_.folders;
        systems = systems_;
    }
    AddHackFolders(dir_, options.folders);
    options.systems = &systems;
    options.covers_dir = dir_ / "covers";
    options.icons_dir = dir_ / "artwork" / "switch2";
    options.region_order = config_.settings != nullptr
                               ? config_.settings->GetList("library.region_order")
                               : std::vector<std::string>();
    if (options.region_order.empty()) {
        options.region_order = DefaultRegionOrder();
    }
    options.cancelled = [&job] { return job.CancelRequested(); };
    job.SetMessage("Looking for games");
    job.SetProgress(0.0f);
    std::vector<Entry> found = ScanFolders(options);
    if (job.CancelRequested()) {
        return;
    }
    ApplyHackSidecars(found);
    SortByName(found);

    artwork_.SetHackOptions(ReadHackOptions(config_.settings));
    // What has no cover of its own on disk: looked up after the list is shown. Covers of
    // earlier scans are asked for again (from the cache when nothing changed), so a hack
    // that had the box of its original gets its own art once a source has it.
    std::vector<Entry> wanted;
    {
        std::lock_guard lock(mutex_);
        std::map<std::string, const Entry*> before;
        for (const Entry& entry : entries_) {
            before[entry.id] = &entry;
        }
        // Games of the first scan are as old as their files; later ones were added now.
        const bool first_scan =
            std::none_of(saved_.records.begin(), saved_.records.end(),
                         [](const auto& record) { return record.second.added > 0; });
        const std::int64_t now = UnixNow();
        bool dated = false;
        for (Entry& entry : found) {
            if (Record& record = saved_.records[entry.id]; record.added <= 0) {
                const std::int64_t written = first_scan ? WrittenUnix(PathFromUtf8(entry.path)) : 0;
                record.added = written > 0 ? std::min(written, now) : now;
                dated = true;
            }
            ApplyRecord(entry);
            if (entry.cover.empty()) {
                // The cover of the last scan stays up until the lookup has answered.
                if (const auto old = before.find(entry.id);
                    old != before.end() && !old->second->cover.empty()) {
                    entry.cover = old->second->cover;
                    entry.placeholder_cover = old->second->placeholder_cover;
                }
                wanted.push_back(entry);
            }
        }
        entries_ = std::move(found);
        SaveEntryCache(dir_ / "cache.json", entries_);
        if (dated) {
            SaveSaved(dir_ / "library.json", saved_);
        }
        ++revision_;
    }
    job.SetProgress(0.2f);
    job.SetMessage("Getting covers");

    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::atomic<std::int64_t> last_bump{0};
    const auto work = [&] {
        for (std::size_t i = next.fetch_add(1); i < wanted.size(); i = next.fetch_add(1)) {
            if (job.CancelRequested()) {
                return;
            }
            Entry& entry = wanted[i];
            Entry lookup = entry;
            lookup.cover.clear();
            lookup.placeholder_cover = false;
            const ArtworkResult result =
                artwork_.Resolve(lookup, systems.Find(entry.system), [&job] { return job.CancelRequested(); });
            const std::string cover = PathToUtf8(result.path);
            const bool placeholder = result.source == ArtworkSource::Placeholder;
            {
                std::lock_guard lock(mutex_);
                for (Entry& shown : entries_) {
                    // A hack that shows the box of its base game keeps it over a placeholder.
                    const bool keeps = placeholder && !shown.hack_of.empty() && !shown.cover.empty() &&
                                       !shown.placeholder_cover;
                    if (shown.id == entry.id && !keeps &&
                        (shown.cover != cover || shown.placeholder_cover != placeholder)) {
                        shown.cover = cover;
                        shown.placeholder_cover = placeholder;
                        changed_ = true;
                    }
                }
            }
            job.SetProgress(0.2f + 0.8f * static_cast<float>(done.fetch_add(1) + 1) /
                                       static_cast<float>(wanted.size()));
            // Screens rebuild on a revision: a few times a second is enough.
            const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count();
            std::int64_t last = last_bump.load();
            if (now - last > 300 && last_bump.compare_exchange_strong(last, now)) {
                BumpIfChanged();
            }
        }
    };
    std::vector<std::thread> threads;
    const unsigned count = static_cast<unsigned>(std::min<std::size_t>(ARTWORK_THREADS, wanted.size()));
    for (unsigned i = 1; i < count; ++i) {
        threads.emplace_back(work);
    }
    work();
    for (std::thread& thread : threads) {
        thread.join();
    }
    {
        std::lock_guard lock(mutex_);
        changed_ = UseBaseCovers(entries_) || changed_;
        SaveEntryCache(dir_ / "cache.json", entries_);
    }
    BumpIfChanged();
    job.SetProgress(1.0f);
    job.SetMessage({});
}

void RealLibrary::BumpIfChanged() {
    std::lock_guard lock(mutex_);
    if (std::exchange(changed_, false)) {
        ++revision_;
    }
}

std::vector<CoverChoice> RealLibrary::CoverChoices(const std::string& id,
                                                   const std::function<bool()>& stopping) {
    const std::optional<Entry> entry = Find(id);
    if (!entry) {
        return {};
    }
    SystemTable systems;
    {
        std::lock_guard lock(mutex_);
        systems = systems_;
    }
    artwork_.SetHackOptions(ReadHackOptions(config_.settings));
    return artwork_.Choices(*entry, systems.Find(entry->system), stopping);
}

GameFaces RealLibrary::Faces(const std::string& id, const std::function<bool()>& stopping) {
    const std::optional<Entry> entry = Find(id);
    if (!entry) {
        return {};
    }
    SystemTable systems;
    {
        std::lock_guard lock(mutex_);
        systems = systems_;
    }
    std::string base;
    if (!entry->hack_of.empty()) {
        if (const std::optional<Entry> original = Find(entry->hack_of)) {
            base = original->original_name.empty() ? original->name : original->original_name;
        }
    }
    return artwork_.Faces(*entry, systems.Find(entry->system), stopping, base);
}

GameDetails RealLibrary::Details(const std::string& id, const std::function<bool()>& stopping) {
    const std::optional<Entry> entry = Find(id);
    if (!entry) {
        return {};
    }
    SystemTable systems;
    {
        std::lock_guard lock(mutex_);
        systems = systems_;
    }
    return artwork_.Details(*entry, systems.Find(entry->system), stopping);
}

std::string RealLibrary::Device(const std::string& id, const std::string& url,
                                const std::function<bool()>& stopping) {
    const std::optional<Entry> entry = Find(id);
    if (!entry) {
        return {};
    }
    SystemTable systems;
    {
        std::lock_guard lock(mutex_);
        systems = systems_;
    }
    return artwork_.Device(systems.Find(entry->system), url, stopping);
}

Status RealLibrary::SetCover(const std::string& id, const std::string& image_path) {
    std::lock_guard lock(mutex_);
    for (Entry& entry : entries_) {
        if (entry.id == id) {
            fs::path file;
            if (const Status written = WriteOwnCover(OwnCoverStem(dir_, entry), image_path, file); !written.ok) {
                return written;
            }
            entry.cover = PathToUtf8(file);
            entry.placeholder_cover = false;
            SaveEntryCache(dir_ / "cache.json", entries_);
            ++revision_;
            return {};
        }
    }
    return Status::Fail("That game is not in the library");
}

void RealLibrary::ResetCover(const std::string& id) {
    {
        std::lock_guard lock(mutex_);
        for (Entry& entry : entries_) {
            if (entry.id == id) {
                RemoveOwnCover(OwnCoverStem(dir_, entry));
                entry.cover.clear();
                entry.placeholder_cover = false;
            }
        }
        ++revision_;
    }
    Scan();
}

void RealLibrary::RetryCovers() {
    const SystemTable systems = SystemsCopy();
    for (const Entry& entry : Entries()) {
        if (entry.placeholder_cover || entry.cover.empty()) {
            artwork_.ForgetMisses(entry, systems.Find(entry.system));
        }
    }
}

} // namespace den::library
