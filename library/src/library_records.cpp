// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What is played and shown: the lists screens ask for, play time and per-game records.

#include <algorithm>

#include "den/base/files.h"
#include "cover_files.h"
#include "library_impl.h"

namespace den::library {

namespace {

constexpr std::size_t SESSION_LOG_LIMIT = 5000;

} // namespace

std::uint64_t RealLibrary::Revision() const {
    return revision_.load();
}

std::vector<Entry> RealLibrary::Entries() const {
    std::lock_guard lock(mutex_);
    std::vector<Entry> out;
    for (const Entry& entry : entries_) {
        if (Shown(entry)) {
            out.push_back(entry);
        }
    }
    return out;
}

std::optional<Entry> RealLibrary::Find(const std::string& id) const {
    std::lock_guard lock(mutex_);
    for (const Entry& entry : entries_) {
        if (entry.id == id) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<SystemInfo> RealLibrary::Systems() const {
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

std::vector<Entry> RealLibrary::RecentlyPlayed(int limit) const {
    std::lock_guard lock(mutex_);
    std::vector<Entry> out;
    for (const Entry& entry : entries_) {
        if (entry.last_played > 0 && Shown(entry)) {
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

std::vector<Entry> RealLibrary::Favourites() const {
    std::lock_guard lock(mutex_);
    std::vector<Entry> out;
    for (const Entry& entry : entries_) {
        if (entry.favourite && Shown(entry)) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<Entry> RealLibrary::RecentlyAdded(int limit) const {
    std::lock_guard lock(mutex_);
    std::vector<Entry> out;
    for (const Entry& entry : entries_) {
        if (entry.added > 0 && Shown(entry)) {
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

std::vector<Session> RealLibrary::Sessions(std::int64_t since_unix) const {
    std::lock_guard lock(mutex_);
    std::vector<Session> out;
    for (const Session& session : saved_.sessions) {
        if (session.started + session.seconds > since_unix) {
            out.push_back(session);
        }
    }
    return out;
}

void RealLibrary::SetFavourite(const std::string& id, bool favourite) {
    std::lock_guard lock(mutex_);
    saved_.records[id].favourite = favourite;
    RecordChanged(id);
}

void RealLibrary::RecordPlay(const std::string& id, std::int64_t started_unix, std::int64_t seconds) {
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

std::map<std::string, PlayTotals> RealLibrary::OwnPlay() const {
    std::lock_guard lock(mutex_);
    std::map<std::string, PlayTotals> out;
    for (const auto& [id, record] : saved_.records) {
        if (record.play_count > 0 || record.play_seconds > 0) {
            out[id] = {record.play_seconds, record.last_played, record.play_count};
        }
    }
    return out;
}

void RealLibrary::SetOtherPlay(const std::map<std::string, PlayTotals>& others) {
    std::lock_guard lock(mutex_);
    bool changed = false;
    for (auto& [id, record] : saved_.records) {
        if (!others.contains(id) && (record.other.seconds != 0 || record.other.count != 0)) {
            record.other = {};
            changed = true;
        }
    }
    for (const auto& [id, play] : others) {
        PlayTotals& other = saved_.records[id].other;
        if (other.seconds != play.seconds || other.count != play.count || other.last_played != play.last_played) {
            other = play;
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    for (Entry& entry : entries_) {
        ApplyRecord(entry);
    }
    SaveSaved(dir_ / "library.json", saved_);
    ++revision_;
}

void RealLibrary::SetDisplayName(const std::string& id, const std::string& name) {
    std::lock_guard lock(mutex_);
    saved_.records[id].name = name;
    RecordChanged(id);
}

void RealLibrary::SetHackOf(const std::string& id, const std::string& base_id) {
    std::lock_guard lock(mutex_);
    saved_.records[id].hack_of = base_id;
    for (Entry& entry : entries_) {
        if (entry.id == id) {
            entry.hack_of = base_id;
        }
    }
    RecordChanged(id);
}

void RealLibrary::MoveRecord(const std::string& old_id, const std::string& new_id) {
    std::lock_guard lock(mutex_);
    if (const auto found = saved_.records.find(old_id); found != saved_.records.end()) {
        saved_.records[new_id] = found->second;
        saved_.records.erase(found);
    }
    for (Session& session : saved_.sessions) {
        if (session.id == old_id) {
            session.id = new_id;
        }
    }
    SaveSaved(dir_ / "library.json", saved_);
    ++revision_;
}

// A game made by the app itself (a hack) is listed at once, without a scan.
void RealLibrary::AdoptEntry(Entry entry) {
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

void RealLibrary::DropEntry(const std::string& id) {
    std::lock_guard lock(mutex_);
    std::erase_if(entries_, [&](const Entry& other) { return other.id == id; });
    SaveEntryCache(dir_ / "cache.json", entries_);
    ++revision_;
}

void RealLibrary::ApplyRecord(Entry& entry) const {
    const auto found = saved_.records.find(entry.id);
    const Record record = found != saved_.records.end() ? found->second : Record{};
    entry.favourite = record.favourite;
    entry.last_played = std::max(record.last_played, record.other.last_played);
    entry.play_seconds = record.play_seconds + record.other.seconds;
    entry.play_count = record.play_count + record.other.count;
    entry.added = record.added;
    if (!record.name.empty()) {
        entry.name = record.name;
    }
    if (!record.hack_of.empty() && entry.hack_of.empty()) {
        entry.hack_of = record.hack_of;
    }
}

void RealLibrary::RecordChanged(const std::string& id) {
    for (Entry& entry : entries_) {
        if (entry.id == id) {
            ApplyRecord(entry);
        }
    }
    SaveSaved(dir_ / "library.json", saved_);
    ++revision_;
}

} // namespace den::library
