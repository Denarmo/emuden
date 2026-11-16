// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <format>

#include "den/app/collections.h"
#include "den/app/app.h"
#include "den/app/games.h"
#include "den/app/strings.h"

namespace den::app {

namespace {

constexpr const char* NAMES_KEY = "library.collections";
constexpr const char* SORT_KEY = "library.sort";
constexpr const char* VIEW_KEY = "library.view";
constexpr int SORT_COUNT = 6;

// Settings keys are dotted, so a collection's games are kept under a plain form of its name.
std::string GamesKey(const std::string& name) {
    std::string slug;
    for (const char c : name) {
        slug.push_back(std::isalnum(static_cast<unsigned char>(c))
                           ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                           : '_');
    }
    return "library.collection_games." + slug;
}

} // namespace

std::vector<std::string> Collections() {
    return Svc().settings->GetList(NAMES_KEY);
}

std::vector<std::string> CollectionIds(const std::string& name) {
    return Svc().settings->GetList(GamesKey(name));
}

std::vector<library::Entry> CollectionGames(const std::string& name) {
    std::vector<library::Entry> games;
    for (const std::string& id : CollectionIds(name)) {
        if (auto game = Svc().library->Find(id); game && !IsHidden(id)) {
            games.push_back(std::move(*game));
        }
    }
    return games;
}

bool InCollection(const std::string& name, const std::string& id) {
    const std::vector<std::string> ids = CollectionIds(name);
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void SetInCollection(const std::string& name, const std::string& id, bool in) {
    std::vector<std::string> ids = CollectionIds(name);
    std::erase(ids, id);
    if (in) {
        ids.push_back(id);
    }
    Svc().settings->SetList(GamesKey(name), ids);
    TouchGames();
}

std::string CreateCollection() {
    std::vector<std::string> names = Collections();
    std::string name;
    for (int number = 1;; ++number) {
        name = TrF("collections.default_name", number);
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            break;
        }
    }
    names.push_back(name);
    Svc().settings->SetList(NAMES_KEY, names);
    Svc().settings->Remove(GamesKey(name));
    TouchGames();
    return name;
}

void DeleteCollection(const std::string& name) {
    std::vector<std::string> names = Collections();
    std::erase(names, name);
    Svc().settings->SetList(NAMES_KEY, names);
    Svc().settings->Remove(GamesKey(name));
    TouchGames();
}

std::vector<std::string> SortNames() {
    return {Tr("sort.name"),         Tr("sort.last_played"), Tr("sort.play_time"),
            Tr("sort.times_played"), Tr("sort.console"),     Tr("sort.size")};
}

Sort LibrarySort() {
    const int value = static_cast<int>(Svc().settings->GetNumber(SORT_KEY, 0));
    return static_cast<Sort>(std::clamp(value, 0, SORT_COUNT - 1));
}

void SetLibrarySort(Sort sort) {
    Svc().settings->SetNumber(SORT_KEY, static_cast<int>(sort));
    TouchGames();
}

void SortGames(std::vector<library::Entry>& games, Sort sort) {
    using library::Entry;
    // The entries arrive sorted by name; a stable sort keeps that order within equal keys.
    switch (sort) {
    case Sort::Name:
        break;
    case Sort::LastPlayed:
        std::stable_sort(games.begin(), games.end(),
                         [](const Entry& a, const Entry& b) { return a.last_played > b.last_played; });
        break;
    case Sort::PlayTime:
        std::stable_sort(games.begin(), games.end(),
                         [](const Entry& a, const Entry& b) { return a.play_seconds > b.play_seconds; });
        break;
    case Sort::TimesPlayed:
        std::stable_sort(games.begin(), games.end(),
                         [](const Entry& a, const Entry& b) { return a.play_count > b.play_count; });
        break;
    case Sort::Console:
        std::stable_sort(games.begin(), games.end(), [](const Entry& a, const Entry& b) {
            return SystemName(a.system) < SystemName(b.system);
        });
        break;
    case Sort::Size:
        std::stable_sort(games.begin(), games.end(),
                         [](const Entry& a, const Entry& b) { return a.size_bytes > b.size_bytes; });
        break;
    }
}

bool LibraryListView() {
    return Svc().settings->GetString(VIEW_KEY, "grid") == "list";
}

void SetLibraryListView(bool list) {
    Svc().settings->SetString(VIEW_KEY, list ? "list" : "grid");
    TouchGames();
}

} // namespace den::app
