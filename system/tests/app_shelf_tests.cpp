// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/system/app_shelf.h"

using den::system::AppItem;
using den::system::AppShelf;

namespace {

std::vector<std::string> Labels(const std::vector<AppItem>& apps) {
    std::vector<std::string> out;
    for (const AppItem& app : apps) {
        out.push_back(app.label);
    }
    return out;
}

AppShelf Sample() {
    AppShelf shelf;
    shelf.SetApps({{"com.video/.Main", "YouTube", "y.png"},
                   {"com.files/.Main", "files", ""},
                   {"com.music/.Main", "Music Player", ""},
                   {"com.browser/.Main", "Browser", ""},
                   {"com.browser/.Main", "Browser again", ""}, // the same id twice
                   {"", "No id", ""},
                   {"com.blank/.Main", "   ", ""}});
    return shelf;
}

} // namespace

TEST_CASE("apps are sorted by name without regard to case, and bad entries are dropped") {
    const AppShelf shelf = Sample();
    CHECK(Labels(shelf.Apps()) == std::vector<std::string>{"Browser", "files", "Music Player", "YouTube"});
    REQUIRE(shelf.Find("com.video/.Main"));
    CHECK(shelf.Find("com.video/.Main")->icon == "y.png");
    CHECK_FALSE(shelf.Find("com.blank/.Main"));
}

TEST_CASE("favourite apps come first, each group by name") {
    AppShelf shelf = Sample();
    CHECK(shelf.ToggleFavourite("com.video/.Main"));
    CHECK(shelf.ToggleFavourite("com.files/.Main"));
    CHECK(Labels(shelf.Visible()) == std::vector<std::string>{"files", "YouTube", "Browser", "Music Player"});
    CHECK(shelf.PinnedCount() == 2);
    CHECK(shelf.Favourite("com.files/.Main"));

    // Unpinning puts it back among the others; an unknown app cannot be pinned.
    CHECK_FALSE(shelf.ToggleFavourite("com.files/.Main"));
    CHECK_FALSE(shelf.ToggleFavourite("com.nothing/.Main"));
    CHECK(Labels(shelf.Visible()) == std::vector<std::string>{"YouTube", "Browser", "files", "Music Player"});
    CHECK(shelf.FavouriteIds() == std::vector<std::string>{"com.video/.Main"});
}

TEST_CASE("the search matches every word anywhere in the name") {
    AppShelf shelf = Sample();
    shelf.ToggleFavourite("com.music/.Main");
    CHECK(Labels(shelf.Visible("MUS")) == std::vector<std::string>{"Music Player"});
    CHECK(Labels(shelf.Visible("play mus")) == std::vector<std::string>{"Music Player"});
    CHECK(Labels(shelf.Visible("e")) == std::vector<std::string>{"Music Player", "Browser", "files", "YouTube"});
    CHECK(shelf.PinnedCount("e") == 1);
    CHECK(shelf.PinnedCount("tube") == 0);
    CHECK(shelf.Visible("zzz").empty());
    CHECK(shelf.Visible("  ").size() == 4);
}

TEST_CASE("recent apps: newest first, once each, only while installed") {
    AppShelf shelf = Sample();
    shelf.NoteOpened("com.browser/.Main");
    shelf.NoteOpened("com.video/.Main");
    shelf.NoteOpened("com.gone/.Main");
    shelf.NoteOpened("com.browser/.Main");
    CHECK(Labels(shelf.Recent()) == std::vector<std::string>{"Browser", "YouTube"});
    CHECK(Labels(shelf.Recent(1)) == std::vector<std::string>{"Browser"});
    CHECK(shelf.RecentIds() ==
          std::vector<std::string>{"com.browser/.Main", "com.gone/.Main", "com.video/.Main"});
}

TEST_CASE("the kept lists survive an app that leaves and comes back") {
    AppShelf shelf;
    shelf.SetFavouriteIds({"com.video/.Main", "", "com.video/.Main", "com.later/.Main"});
    shelf.SetRecentIds({"com.later/.Main", "com.video/.Main", "com.later/.Main"});
    CHECK(shelf.FavouriteIds() == std::vector<std::string>{"com.video/.Main", "com.later/.Main"});
    CHECK(shelf.Visible().empty());
    CHECK(shelf.Recent().empty());

    shelf.SetApps({{"com.video/.Main", "YouTube", ""}, {"com.other/.Main", "Atlas", ""}});
    CHECK(Labels(shelf.Visible()) == std::vector<std::string>{"YouTube", "Atlas"});
    CHECK(Labels(shelf.Recent()) == std::vector<std::string>{"YouTube"});

    shelf.SetApps({{"com.later/.Main", "Later", ""}, {"com.other/.Main", "Atlas", ""}});
    CHECK(Labels(shelf.Visible()) == std::vector<std::string>{"Later", "Atlas"});
    CHECK(Labels(shelf.Recent()) == std::vector<std::string>{"Later"});
    // Unpinning an app that is not installed still works.
    CHECK_FALSE(shelf.ToggleFavourite("com.video/.Main"));
    CHECK(shelf.FavouriteIds() == std::vector<std::string>{"com.later/.Main"});
}

TEST_CASE("the recent list does not grow without end") {
    AppShelf shelf;
    for (int i = 0; i < 100; ++i) {
        shelf.NoteOpened("app" + std::to_string(i));
    }
    CHECK(shelf.RecentIds().size() == 48);
    CHECK(shelf.RecentIds().front() == "app99");
}
