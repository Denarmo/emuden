// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Collections in the settings: named and renamed with the on-screen text entry.

#include <algorithm>
#include <cctype>
#include <format>

#include "den/app/strings.h"
#include "den/app/collections.h"
#include "den/app/games.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"

namespace den::app {

namespace {

constexpr const char* NAMES_KEY = "library.collections";

// The key a collection's games are kept under; the same plain form of the name as in
// home/collections.cpp, which has no rename of its own yet.
std::string GamesKey(const std::string& name) {
    std::string slug;
    for (const char c : name) {
        slug.push_back(std::isalnum(static_cast<unsigned char>(c))
                           ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                           : '_');
    }
    return "library.collection_games." + slug;
}

// Why `name` cannot be given to a collection (`current` is the one being renamed), or empty.
std::string Refusal(const std::string& name, const std::string& current) {
    for (const std::string& other : Collections()) {
        if (other != current && GamesKey(other) == GamesKey(name)) {
            return TrF("There is a collection called {} already", other);
        }
    }
    return {};
}

void Rename(const std::string& from, const std::string& to) {
    if (to == from) {
        return;
    }
    store::Settings& settings = *Svc().settings;
    if (const std::string refusal = Refusal(to, from); !refusal.empty()) {
        Svc().stage->Toast(refusal);
        return;
    }
    std::vector<std::string> names = Collections();
    std::replace(names.begin(), names.end(), from, to);
    const std::vector<std::string> games = CollectionIds(from);
    if (GamesKey(from) != GamesKey(to)) {
        settings.Remove(GamesKey(from));
    }
    settings.SetList(GamesKey(to), games);
    settings.SetList(NAMES_KEY, names);
    TouchGames();
    Svc().stage->Toast(std::format("{} is now {}", from, to));
    RebuildTop();
}

void Create(const std::string& name) {
    if (const std::string refusal = Refusal(name, {}); !refusal.empty()) {
        Svc().stage->Toast(refusal);
        return;
    }
    std::vector<std::string> names = Collections();
    names.push_back(name);
    Svc().settings->SetList(NAMES_KEY, names);
    Svc().settings->Remove(GamesKey(name));
    TouchGames();
    Svc().stage->Toast(std::format("{} created", name));
    RebuildTop();
}

std::vector<ui::RowSpec> CollectionRows() {
    std::vector<ui::RowSpec> rows;
    int index = 0;
    for (const std::string& name : Collections()) {
        ui::RowSpec row = ValueRow(
            std::format("collections.rename.{}", index++), name,
            [name] {
                const std::size_t count = CollectionIds(name).size();
                return count == 1 ? std::string("1 game") : std::format("{} games", count);
            },
            [name] {
                ui::TextRequest request;
                request.title = TrF("Rename {}", name);
                request.text = name;
                Svc().stage->AskText(std::move(request), [name](const std::string& text) { Rename(name, text); });
            });
        row.description = "Select to rename";
        rows.push_back(std::move(row));
    }
    rows.push_back(ActionRow(
        "collections.new", "New collection",
        [] {
            ui::TextRequest request;
            request.title = "New collection";
            request.placeholder = "Name";
            Svc().stage->AskText(std::move(request), Create);
        },
        "Collections are shelves of your own on the home screen"));
    return rows;
}

} // namespace

DEN_REGISTER_SETTINGS(42, "Collections", CollectionRows);

} // namespace den::app
