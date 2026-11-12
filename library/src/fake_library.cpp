// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A stand-in library with a few made-up games, so the app links and the screens have
// something to show. Delete this file when the real library defines CreateLibrary.

#include <algorithm>
#include <mutex>

#include "den/library/library.h"

namespace den::library {

namespace {

class FakeLibrary final : public Library {
public:
    explicit FakeLibrary(const Config& config) : config_(config) {
        const char* const games[][2] = {
            {"Aurora Quest", "gba"},   {"Brick Garden", "gb"},     {"Comet Racers", "psx"},
            {"Dune Drifters", "nds"},  {"Echo Valley", "gbc"},     {"Fable Harbor", "psp"},
            {"Glimmer Kart", "switch2"}, {"Hollow Peaks", "gba"},  {"Iron Lanterns", "psx"},
            {"Jade Circuit", "nds"},   {"Kite Islands", "switch2"}, {"Lumen Tactics", "gbc"},
        };
        int index = 0;
        for (const auto& game : games) {
            Entry entry;
            entry.id = "fake-" + std::to_string(index);
            entry.name = game[0];
            entry.system = game[1];
            entry.path = "fake/" + entry.name;
            entry.last_played = index < 4 ? 1760000000 - index * 86400 : 0;
            entry.favourite = index % 5 == 0;
            entries_.push_back(std::move(entry));
            ++index;
        }
    }

    std::vector<Folder> Folders() const override {
        std::lock_guard lock(mutex_);
        return folders_;
    }
    Status AddFolder(const std::string& path, const std::string& system) override {
        std::lock_guard lock(mutex_);
        folders_.push_back({path, system});
        ++revision_;
        return {};
    }
    Status RemoveFolder(const std::string& path) override {
        std::lock_guard lock(mutex_);
        std::erase_if(folders_, [&](const Folder& f) { return f.path == path; });
        ++revision_;
        return {};
    }
    JobPtr Scan() override {
        return config_.jobs->Run("scan library", [](Job& job) {
            job.SetProgress(1.0f);
            return Status{};
        });
    }
    std::uint64_t Revision() const override {
        std::lock_guard lock(mutex_);
        return revision_;
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
        std::vector<SystemInfo> systems;
        for (const Entry& entry : entries_) {
            auto it = std::find_if(systems.begin(), systems.end(),
                                   [&](const SystemInfo& s) { return s.id == entry.system; });
            if (it == systems.end()) {
                systems.push_back({entry.system, entry.system, entry.system, 1});
            } else {
                ++it->game_count;
            }
        }
        return systems;
    }
    std::vector<Entry> RecentlyPlayed(int limit) const override {
        std::lock_guard lock(mutex_);
        std::vector<Entry> out;
        for (const Entry& entry : entries_) {
            if (entry.last_played > 0) {
                out.push_back(entry);
            }
        }
        std::sort(out.begin(), out.end(),
                  [](const Entry& a, const Entry& b) { return a.last_played > b.last_played; });
        if (static_cast<int>(out.size()) > limit) {
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
    void SetFavourite(const std::string& id, bool favourite) override {
        std::lock_guard lock(mutex_);
        for (Entry& entry : entries_) {
            if (entry.id == id) {
                entry.favourite = favourite;
                ++revision_;
            }
        }
    }
    void RecordPlay(const std::string& id, std::int64_t started_unix, std::int64_t seconds) override {
        std::lock_guard lock(mutex_);
        for (Entry& entry : entries_) {
            if (entry.id == id) {
                entry.last_played = started_unix;
                entry.play_seconds += seconds;
                ++entry.play_count;
                ++revision_;
            }
        }
    }

private:
    Config config_;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::vector<Folder> folders_;
    std::uint64_t revision_ = 1;
};

} // namespace

std::unique_ptr<Library> CreateLibrary(const Config& config) {
    return std::make_unique<FakeLibrary>(config);
}

} // namespace den::library
