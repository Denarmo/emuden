// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The library: library.cpp keeps its folders and scans, library_records.cpp what is played
// and shown, library_scan.cpp the scan's work and the covers.

#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>

#include "den/library/artwork.h"
#include "den/library/library.h"
#include "state.h"

namespace den::library {

std::string FolderKey(const std::string& path);
bool IsUnder(const std::string& file, const std::string& folder_key);
void SortByName(std::vector<Entry>& entries);

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
    explicit RealLibrary(const Config& config);
    ~RealLibrary() override;
    std::vector<Folder> Folders() const override;
    Status AddFolder(const std::string& path, const std::string& system) override;
    Status RemoveFolder(const std::string& path) override;
    JobPtr Scan() override;
    std::uint64_t Revision() const override;
    std::vector<Entry> Entries() const override;
    std::optional<Entry> Find(const std::string& id) const override;
    std::vector<SystemInfo> Systems() const override;
    std::vector<Entry> RecentlyPlayed(int limit) const override;
    std::vector<Entry> Favourites() const override;
    std::vector<Entry> RecentlyAdded(int limit) const override;
    std::vector<Session> Sessions(std::int64_t since_unix) const override;
    void SetFavourite(const std::string& id, bool favourite) override;
    void RecordPlay(const std::string& id, std::int64_t started_unix, std::int64_t seconds) override;
    std::map<std::string, PlayTotals> OwnPlay() const override;
    void SetOtherPlay(const std::map<std::string, PlayTotals>& others) override;
    std::vector<CoverChoice> CoverChoices(const std::string& id,
                                          const std::function<bool()>& stopping) override;
    GameFaces Faces(const std::string& id, const std::function<bool()>& stopping) override;
    GameDetails Details(const std::string& id, const std::function<bool()>& stopping) override;
    std::string Device(const std::string& id, const std::string& url,
                       const std::function<bool()>& stopping) override;
    Status SetCover(const std::string& id, const std::string& image_path) override;
    void ResetCover(const std::string& id) override;
    void SetSystems(std::vector<SystemDef> systems) override;
    Status ImportFolders(const std::filesystem::path& old_profile) override;
    void MarkConstructed();
    Hacks* HackStore() override;

private:
    // A game whose console is gone (its plugin was removed) stays in the cache, hidden, until
    // the next scan drops it; reinstalling the plugin brings it back.
    bool Shown(const Entry& entry) const;
    SystemTable Consoles() const override;
    SystemTable SystemsCopy() const;
    void SetDisplayName(const std::string& id, const std::string& name) override;
    void SetHackOf(const std::string& id, const std::string& base_id) override;
    void RetryCovers() override;
    void MoveRecord(const std::string& old_id, const std::string& new_id) override;

    // A game made by the app itself (a hack) is listed at once, without a scan.
    void AdoptEntry(Entry entry);
    void DropEntry(const std::string& id);
    bool AddFolderLocked(const std::string& path, const std::string& system);
    void ApplyRecord(Entry& entry) const;
    void RecordChanged(const std::string& id);

    // Whether the scan has to run once more; if not, the next Scan() starts a new job.
    bool TakeRescan(const Job& job);
    void RunScan(Job& job);
    void BumpIfChanged();

    Config config_;
    std::filesystem::path dir_;
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

} // namespace den::library
