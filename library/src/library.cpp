// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/library/artwork.h"
#include "den/library/library.h"
#include "den/library/names.h"
#include "den/library/scan.h"
#include "den/store/settings.h"
#include "cover_files.h"
#include "hacks_store.h"
#include "state.h"

namespace den::library {

namespace fs = std::filesystem;

namespace {

constexpr unsigned ARTWORK_THREADS = 4;
constexpr std::size_t SESSION_LOG_LIMIT = 5000;

std::string FolderKey(const std::string& path) {
    std::string key = ToLower(PathToUtf8(PathFromUtf8(path).lexically_normal()));
    std::replace(key.begin(), key.end(), '\\', '/');
    while (key.size() > 1 && key.back() == '/') {
        key.pop_back();
    }
    return key;
}

bool IsUnder(const std::string& file, const std::string& folder_key) {
    const std::string key = FolderKey(file);
    return key.size() > folder_key.size() && key.starts_with(folder_key) &&
           key[folder_key.size()] == '/';
}

void SortByName(std::vector<Entry>& entries) {
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const std::string left = ToLower(a.name);
        const std::string right = ToLower(b.name);
        return left != right ? left < right : a.id < b.id;
    });
}

// Lets the library go away while a scan is queued or running: a queued scan then does
// nothing, a running one is waited for.
struct Gate {
    std::mutex mutex;
    std::condition_variable idle;
    bool closed = false;
    bool running = false;
};

class RealLibrary final : public Library {
public:
    explicit RealLibrary(const Config& config)
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

    ~RealLibrary() override {
        if (scan_job_) {
            scan_job_->Cancel();
        }
        std::unique_lock lock(gate_->mutex);
        gate_->closed = true;
        gate_->idle.wait(lock, [&] { return !gate_->running; });
    }

    std::vector<Folder> Folders() const override {
        std::lock_guard lock(mutex_);
        return saved_.folders;
    }

    Status AddFolder(const std::string& path, const std::string& system) override {
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

    Status RemoveFolder(const std::string& path) override {
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

    JobPtr Scan() override {
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

    std::uint64_t Revision() const override {
        return revision_.load();
    }

    std::vector<Entry> Entries() const override {
        std::lock_guard lock(mutex_);
        return entries_;
    }

    std::optional<Entry> Find(const std::string& id) const override {
        std::lock_guard lock(mutex_);
        for (const Entry& entry : entries_) {
            if (entry.id == id) {
                return entry;
            }
        }
        return std::nullopt;
    }

    std::vector<SystemInfo> Systems() const override {
        std::lock_guard lock(mutex_);
        std::vector<SystemInfo> out;
        for (const SystemDef& def : systems_.All()) {
            const int count = static_cast<int>(std::count_if(
                entries_.begin(), entries_.end(), [&](const Entry& e) { return e.system == def.id; }));
            if (count > 0) {
                out.push_back({def.id, def.name, def.short_name.empty() ? def.name : def.short_name, count});
            }
        }
        return out;
    }

    std::vector<Entry> RecentlyPlayed(int limit) const override {
        std::lock_guard lock(mutex_);
        std::vector<Entry> out;
        for (const Entry& entry : entries_) {
            if (entry.last_played > 0) {
                out.push_back(entry);
            }
        }
        std::stable_sort(out.begin(), out.end(),
                         [](const Entry& a, const Entry& b) { return a.last_played > b.last_played; });
        if (limit >= 0 && static_cast<int>(out.size()) > limit) {
            out.resize(static_cast<std::size_t>(limit));
        }
        return out;
    }

    std::vector<Entry> Favourites() const override {
        std::lock_guard lock(mutex_);
        std::vector<Entry> out;
        for (const Entry& entry : entries_) {
            if (entry.favourite) {
                out.push_back(entry);
            }
        }
        return out;
    }

    std::vector<Entry> RecentlyAdded(int limit) const override {
        std::lock_guard lock(mutex_);
        std::vector<Entry> out;
        for (const Entry& entry : entries_) {
            if (entry.added > 0) {
                out.push_back(entry);
            }
        }
        std::stable_sort(out.begin(), out.end(),
                         [](const Entry& a, const Entry& b) { return a.added > b.added; });
        if (limit >= 0 && static_cast<int>(out.size()) > limit) {
            out.resize(static_cast<std::size_t>(limit));
        }
        return out;
    }

    std::vector<Session> Sessions(std::int64_t since_unix) const override {
        std::lock_guard lock(mutex_);
        std::vector<Session> out;
        for (const Session& session : saved_.sessions) {
            if (session.started + session.seconds > since_unix) {
                out.push_back(session);
            }
        }
        return out;
    }

    void SetFavourite(const std::string& id, bool favourite) override {
        std::lock_guard lock(mutex_);
        saved_.records[id].favourite = favourite;
        RecordChanged(id);
    }

    void RecordPlay(const std::string& id, std::int64_t started_unix, std::int64_t seconds) override {
        std::lock_guard lock(mutex_);
        seconds = std::max<std::int64_t>(seconds, 0);
        // A game opened over this one was booked on its own: its time is not this game's.
        const std::int64_t end = started_unix + seconds;
        std::int64_t others = 0;
        for (const Session& other : saved_.sessions) {
            if (other.id != id) {
                others += std::max<std::int64_t>(
                    std::min(end, other.started + other.seconds) - std::max(started_unix, other.started), 0);
            }
        }
        seconds = std::max<std::int64_t>(seconds - others, 0);
        saved_.sessions.push_back({id, started_unix, seconds});
        if (saved_.sessions.size() > SESSION_LOG_LIMIT) {
            saved_.sessions.erase(saved_.sessions.begin(),
                                  saved_.sessions.end() - static_cast<std::ptrdiff_t>(SESSION_LOG_LIMIT));
        }
        Record& record = saved_.records[id];
        record.last_played = std::max(record.last_played, started_unix);
        record.play_seconds += seconds;
        ++record.play_count;
        RecordChanged(id);
    }

    std::vector<CoverChoice> CoverChoices(const std::string& id,
                                          const std::function<bool()>& stopping) override {
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

    Status SetCover(const std::string& id, const std::string& image_path) override {
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

    void ResetCover(const std::string& id) override {
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

    void SetSystems(std::vector<SystemDef> systems) override {
        std::lock_guard lock(mutex_);
        systems_.Merge(std::move(systems));
        ++revision_;
    }

    Status ImportFolders(const fs::path& old_profile) override {
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

    void MarkConstructed() {
        constructed_ = true;
    }

    Hacks* HackStore() override {
        return hacks_.get();
    }

private:
    SystemTable SystemsCopy() const {
        std::lock_guard lock(mutex_);
        return systems_;
    }

    // A game made by the app itself (a hack) is listed at once, without a scan.
    void AdoptEntry(Entry entry) {
        std::lock_guard lock(mutex_);
        if (Record& record = saved_.records[entry.id]; record.added <= 0) {
            record.added = UnixNow();
            SaveSaved(dir_ / "library.json", saved_);
        }
        ApplyRecord(entry);
        std::erase_if(entries_, [&](const Entry& other) { return other.id == entry.id; });
        entries_.push_back(std::move(entry));
        SortByName(entries_);
        SaveEntryCache(dir_ / "cache.json", entries_);
        rescan_ = rescan_ || scan_active_; // a scan under way has not seen it
        ++revision_;
    }

    void DropEntry(const std::string& id) {
        std::lock_guard lock(mutex_);
        std::erase_if(entries_, [&](const Entry& other) { return other.id == id; });
        SaveEntryCache(dir_ / "cache.json", entries_);
        ++revision_;
    }

    bool AddFolderLocked(const std::string& path, const std::string& system) {
        const std::string key = FolderKey(path);
        for (const Folder& folder : saved_.folders) {
            if (FolderKey(folder.path) == key) {
                return false;
            }
        }
        saved_.folders.push_back({path, system});
        return true;
    }

    void ApplyRecord(Entry& entry) const {
        const auto found = saved_.records.find(entry.id);
        const Record record = found != saved_.records.end() ? found->second : Record{};
        entry.favourite = record.favourite;
        entry.last_played = record.last_played;
        entry.play_seconds = record.play_seconds;
        entry.play_count = record.play_count;
        entry.added = record.added;
    }

    void RecordChanged(const std::string& id) {
        for (Entry& entry : entries_) {
            if (entry.id == id) {
                ApplyRecord(entry);
            }
        }
        SaveSaved(dir_ / "library.json", saved_);
        ++revision_;
    }

    // Whether the scan has to run once more; if not, the next Scan() starts a new job.
    bool TakeRescan(const Job& job) {
        std::lock_guard lock(mutex_);
        if (std::exchange(rescan_, false) && !job.CancelRequested()) {
            return true;
        }
        scan_active_ = false;
        return false;
    }

    void RunScan(Job& job) {
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
                        if (shown.id == entry.id &&
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
            SaveEntryCache(dir_ / "cache.json", entries_);
        }
        BumpIfChanged();
        job.SetProgress(1.0f);
        job.SetMessage({});
    }

    void BumpIfChanged() {
        std::lock_guard lock(mutex_);
        if (std::exchange(changed_, false)) {
            ++revision_;
        }
    }

    Config config_;
    fs::path dir_;
    Artwork artwork_;
    std::shared_ptr<Gate> gate_ = std::make_shared<Gate>();

    mutable std::mutex mutex_;
    Saved saved_;
    std::vector<Entry> entries_;
    SystemTable systems_;
    JobPtr scan_job_;
    bool scan_active_ = false;
    bool rescan_ = false;
    bool changed_ = false;
    bool constructed_ = false;
    std::unique_ptr<Hacks> hacks_;
    std::atomic<std::uint64_t> revision_{1};
};

} // namespace

std::unique_ptr<Library> CreateLibrary(const Config& config) {
    auto library = std::make_unique<RealLibrary>(config);
    library->MarkConstructed();
    return library;
}

} // namespace den::library
