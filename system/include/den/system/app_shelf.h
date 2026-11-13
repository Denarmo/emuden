// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The device's other apps as the Apps section shows them: sorted by name, the user's
// favourites first, a search by words, and the apps opened last. Pure data: the platform
// gives the list (pal::apps), the app keeps the two id lists in its settings.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace den::system {

struct AppItem {
    std::string id;    // the platform's id, stable while the app is installed
    std::string label;
    std::string icon;  // UTF-8 path of its picture; may be empty
};

class AppShelf {
public:
    static constexpr std::size_t MAX_RECENT = 12;

    /// The installed apps, in any order. Entries without an id or name and repeated ids are
    /// dropped; the rest is kept sorted by name (case ignored), then by id.
    void SetApps(std::vector<AppItem> apps);
    const std::vector<AppItem>& Apps() const {
        return apps_;
    }
    const AppItem* Find(const std::string& id) const;

    /// What the grid shows: favourites first, then the others, each by name. With a query,
    /// only the apps whose name holds every word of it (case ignored).
    std::vector<AppItem> Visible(const std::string& query = {}) const;
    /// How many entries at the front of Visible(query) are favourites.
    std::size_t PinnedCount(const std::string& query = {}) const;
    /// The apps opened last that are still installed, newest first.
    std::vector<AppItem> Recent(std::size_t max = MAX_RECENT) const;

    bool Favourite(const std::string& id) const;
    /// Pins or unpins; returns the new state. Unknown ids are refused (false).
    bool ToggleFavourite(const std::string& id);
    /// The app was opened: it moves to the front of the recent ones.
    void NoteOpened(const std::string& id);

    // What is kept between starts. Ids of apps that are not installed (any more, or yet) stay
    // in both lists, so an app that comes back is where it was.
    const std::vector<std::string>& FavouriteIds() const {
        return favourites_;
    }
    void SetFavouriteIds(std::vector<std::string> ids);
    const std::vector<std::string>& RecentIds() const {
        return recent_;
    }
    void SetRecentIds(std::vector<std::string> ids);

private:
    std::vector<AppItem> apps_;
    std::vector<std::string> favourites_; // in the order they were pinned
    std::vector<std::string> recent_;     // newest first
};

} // namespace den::system
