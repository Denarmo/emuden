// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>
#include <sstream>

#include "den/base/files.h"
#include "den/system/app_shelf.h"

namespace den::system {

namespace {

// Kept lists never grow without end: ids of apps long gone fall off the back.
constexpr std::size_t MAX_KEPT_RECENT = 48;

std::vector<std::string> Unique(std::vector<std::string> ids) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (std::string& id : ids) {
        if (!id.empty() && seen.insert(id).second) {
            out.push_back(std::move(id));
        }
    }
    return out;
}

std::vector<std::string> Words(const std::string& query) {
    std::istringstream in(ToLower(query));
    std::vector<std::string> words;
    for (std::string word; in >> word;) {
        words.push_back(std::move(word));
    }
    return words;
}

bool Matches(const AppItem& app, const std::vector<std::string>& words) {
    const std::string name = ToLower(app.label);
    return std::all_of(words.begin(), words.end(),
                       [&](const std::string& word) { return name.find(word) != std::string::npos; });
}

bool Contains(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

void AppShelf::SetApps(std::vector<AppItem> apps) {
    std::set<std::string> seen;
    apps_.clear();
    for (AppItem& app : apps) {
        if (!app.id.empty() && !Trim(app.label).empty() && seen.insert(app.id).second) {
            app.label = Trim(app.label);
            apps_.push_back(std::move(app));
        }
    }
    std::sort(apps_.begin(), apps_.end(), [](const AppItem& a, const AppItem& b) {
        const std::string left = ToLower(a.label), right = ToLower(b.label);
        return left != right ? left < right : a.id < b.id;
    });
}

const AppItem* AppShelf::Find(const std::string& id) const {
    const auto it = std::find_if(apps_.begin(), apps_.end(), [&](const AppItem& app) { return app.id == id; });
    return it == apps_.end() ? nullptr : &*it;
}

std::vector<AppItem> AppShelf::Visible(const std::string& query) const {
    const std::vector<std::string> words = Words(query);
    std::vector<AppItem> pinned, others;
    for (const AppItem& app : apps_) {
        if (Matches(app, words)) {
            (Favourite(app.id) ? pinned : others).push_back(app);
        }
    }
    pinned.insert(pinned.end(), std::make_move_iterator(others.begin()), std::make_move_iterator(others.end()));
    return pinned;
}

std::size_t AppShelf::PinnedCount(const std::string& query) const {
    const std::vector<std::string> words = Words(query);
    return static_cast<std::size_t>(std::count_if(apps_.begin(), apps_.end(), [&](const AppItem& app) {
        return Favourite(app.id) && Matches(app, words);
    }));
}

std::vector<AppItem> AppShelf::Recent(std::size_t max) const {
    std::vector<AppItem> out;
    for (const std::string& id : recent_) {
        if (out.size() >= max) {
            break;
        }
        if (const AppItem* app = Find(id)) {
            out.push_back(*app);
        }
    }
    return out;
}

bool AppShelf::Favourite(const std::string& id) const {
    return Contains(favourites_, id);
}

bool AppShelf::ToggleFavourite(const std::string& id) {
    if (Favourite(id)) {
        std::erase(favourites_, id);
        return false;
    }
    if (!Find(id)) {
        return false;
    }
    favourites_.push_back(id);
    return true;
}

void AppShelf::NoteOpened(const std::string& id) {
    if (id.empty()) {
        return;
    }
    std::erase(recent_, id);
    recent_.insert(recent_.begin(), id);
    if (recent_.size() > MAX_KEPT_RECENT) {
        recent_.resize(MAX_KEPT_RECENT);
    }
}

void AppShelf::SetFavouriteIds(std::vector<std::string> ids) {
    favourites_ = Unique(std::move(ids));
}

void AppShelf::SetRecentIds(std::vector<std::string> ids) {
    recent_ = Unique(std::move(ids));
    if (recent_.size() > MAX_KEPT_RECENT) {
        recent_.resize(MAX_KEPT_RECENT);
    }
}

} // namespace den::system
