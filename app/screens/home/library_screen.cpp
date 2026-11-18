// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Library entry of the rail: every game as a grid of covers or a list, filtered with the
// chips above it (or the shoulder buttons): all, favourites, the user's collections, one per
// console. Route "library", optional params system or collection.

#include <algorithm>

#include "den/app/launch.h"
#include "den/app/collections.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "den/app/game_widgets.h"
#include "den/app/games.h"
#include "den/app/strings.h"
#include "page_widths.h"

namespace den::app {

namespace {


class LibraryScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = GamesRevision();
        width_ = CanvasWidth();
        if (!filter_set_) {
            filter_ = !Param("collection").empty() ? "c:" + Param("collection")
                      : !Param("system").empty()   ? "s:" + Param("system")
                                                   : std::string();
            filter_set_ = true;
        }
        std::vector<library::Entry> games = WithoutHidden(svc.library->Entries());

        // The consoles that have games, in the plugins' order.
        systems_.clear();
        for (const plugins::Console& console : svc.plugins->Consoles()) {
            if (HasSystem(games, console.id)) {
                systems_.push_back(console.id);
            }
        }
        for (const library::Entry& game : games) {
            if (std::find(systems_.begin(), systems_.end(), game.system) == systems_.end()) {
                systems_.push_back(game.system);
            }
        }
        // The chips: all, favourites, collections, consoles.
        filters_.clear();
        filters_.push_back({"", Tr("library.all")});
        if (std::any_of(games.begin(), games.end(), [](const library::Entry& game) { return game.favourite; })) {
            filters_.push_back({"fav", Tr("home.favourites")});
        }
        for (const std::string& name : Collections()) {
            filters_.push_back({"c:" + name, name});
        }
        for (const std::string& system : systems_) {
            filters_.push_back({"s:" + system, SystemName(system)});
        }
        if (std::none_of(filters_.begin(), filters_.end(),
                         [&](const auto& filter) { return filter.first == filter_; })) {
            filter_.clear();
        }
        if (filter_ == "fav") {
            std::erase_if(games, [](const library::Entry& game) { return !game.favourite; });
        } else if (filter_.starts_with("c:")) {
            games = CollectionGames(filter_.substr(2));
            std::sort(games.begin(), games.end(),
                      [](const library::Entry& a, const library::Entry& b) { return a.name < b.name; });
        } else if (filter_.starts_with("s:")) {
            std::erase_if(games,
                          [&](const library::Entry& game) { return game.system != filter_.substr(2); });
        }
        SortGames(games, LibrarySort());

        ui::Node* content = BuildRailFrame(*this, "");
        content->gap = 10;
        ui::Node* header = content->Add(ui::MakeBox(ui::Axis::Row, 16));
        header->align = ui::Align::End;
        header->Emplace<ui::Label>(Tr("library.title"), svc.theme->text_title, gfx::FONT_BOLD);
        auto* count = header->Emplace<ui::Label>(GameCount(games.size()));
        count->tone = ui::Tone::Dim;
        count->pad_bottom = 6;
        auto* order = header->Emplace<ui::Label>(TrF("library.sorted_by", SortNames()[static_cast<int>(LibrarySort())]));
        order->tone = ui::Tone::Dim;
        order->pad_bottom = 6;

        ui::Node* chips = content->Add(ui::MakeShelf(8));
        chips->id = "library.chips";
        chips->SetPadding(8, 6);
        for (const auto& [key, text] : filters_) {
            AddChip(chips, key, text);
        }

        if (games.empty()) {
            content->Emplace<ui::Label>(filter_.starts_with("c:") ? Tr("collections.empty") : Tr("home.empty_message"))
                ->tone = ui::Tone::Dim;
            return;
        }
        if (LibraryListView()) {
            BuildList(content, games);
            return;
        }
        GameTileOptions options;
        options.id_prefix = "library";
        options.show_badge = !filter_.starts_with("s:");
        options.on_activate = [](const library::Entry& game) {
            Svc().stage->Push("details", {{"id", game.id}});
        };
        options.on_focus = [this](const library::Entry& game) { selected_ = game.id; };
        const float room = RailContentWidth(width_, PageHeight(), svc.theme->margin) - 10;
        ui::Node* grid = AddGameGrid(content, games, GridColumns(room), options);
        grid->id = "library.grid";
    }

    void Update(ui::Context&) override {
        if (GamesRevision() != revision_ || CanvasWidth() != width_) {
            Rebuild();
            return;
        }
        if (focus_grid_) {
            focus_grid_ = false;
            std::vector<ui::Node*> tiles;
            if (ui::Node* grid = Root().FindById("library.grid")) {
                grid->CollectFocusable(tiles);
            }
            if (!tiles.empty()) {
                SetFocus(tiles.front());
            }
        }
    }

    bool OnNav(input::Nav nav) override {
        switch (nav) {
        case input::Nav::ShoulderL:
        case input::Nav::ShoulderR:
            StepFilter(nav == input::Nav::ShoulderR ? 1 : -1);
            return true;
        case input::Nav::Option:
            if (OnGame()) {
                StartGame(selected_);
            }
            return true;
        case input::Nav::Extra:
            if (const auto game = OnGame() ? Svc().library->Find(selected_) : std::nullopt) {
                Svc().library->SetFavourite(game->id, !game->favourite);
                Svc().stage->Toast(TrF(game->favourite ? "home.unfavourited" : "home.favourited", game->name),
                                   2.0f);
            }
            return true;
        case input::Nav::Menu:
            Svc().stage->Push("library_options");
            return true;
        case input::Nav::View:
            SetLibrarySort(static_cast<Sort>((static_cast<int>(LibrarySort()) + 1) %
                                             static_cast<int>(SortNames().size())));
            return true;
        case input::Nav::Back:
            if (Svc().stage->Depth() > 1) {
                Svc().stage->Pop();
            } else {
                Svc().stage->Replace("home");
            }
            return true;
        default:
            return false;
        }
    }

    std::vector<ui::Hint> Hints() const override {
        std::vector<ui::Hint> hints{{input::Nav::Confirm, OnGame() ? Tr("hint.details") : Tr("hint.select")}};
        if (OnGame()) {
            hints.push_back({input::Nav::Option, Tr("hint.start")});
            hints.push_back({input::Nav::Extra, Tr("hint.favourite")});
        }
        hints.push_back({input::Nav::View, Tr("hint.sort")});
        hints.push_back({input::Nav::Menu, Tr("hint.options")});
        hints.push_back({input::Nav::ShoulderR, Tr("hint.next_filter")});
        hints.push_back({input::Nav::Back, Tr("common.back")});
        return hints;
    }

private:
    static float CanvasWidth() {
        const float width = Svc().canvas->Width();
        return width > 0 ? width : 1280.0f;
    }

    static bool HasSystem(const std::vector<library::Entry>& games, const std::string& system) {
        return std::any_of(games.begin(), games.end(),
                           [&](const library::Entry& game) { return game.system == system; });
    }

    bool OnGame() const {
        return !selected_.empty() && Focus() && Focus()->id == "library." + selected_;
    }

    // The compact layout: one row per game with its console and records.
    void BuildList(ui::Node* content, const std::vector<library::Entry>& games) {
        ui::Node* list = content->Add(ui::MakeList(6));
        list->id = "library.grid";
        list->pad_right = Svc().theme->margin;
        list->pad_bottom = 24;
        for (const library::Entry& game : games) {
            ui::RowSpec spec;
            spec.kind = ui::RowSpec::Kind::Value;
            spec.id = "library." + game.id;
            spec.label = game.name;
            spec.value = [text = RecordsLine(game)] { return text; };
            spec.action = [id = game.id] { Svc().stage->Push("details", {{"id", id}}); };
            auto* row = list->Emplace<ui::Row>(std::move(spec));
            row->id = "library." + game.id;
            row->on_focus = [this, id = game.id] { selected_ = id; };
        }
    }

    void AddChip(ui::Node* chips, const std::string& system, const std::string& text) {
        auto* chip = chips->Emplace<ui::Button>(text, [this, system] { SetFilter(system); });
        chip->id = "library.chip." + system;
        chip->height = 40;
        chip->selected = system == filter_;
    }

    void SetFilter(const std::string& system) {
        if (system != filter_) {
            filter_ = system;
            selected_.clear();
            Rebuild(false);
        }
    }

    void StepFilter(int direction) {
        std::vector<std::string> all;
        for (const auto& filter : filters_) {
            all.push_back(filter.first);
        }
        const auto it = std::find(all.begin(), all.end(), filter_);
        const int count = static_cast<int>(all.size());
        const int index = it == all.end() ? 0 : static_cast<int>(it - all.begin());
        SetFilter(all[static_cast<std::size_t>(((index + direction) % count + count) % count)]);
        focus_grid_ = true;
    }

    std::vector<std::string> systems_;
    std::vector<std::pair<std::string, std::string>> filters_; // key, text
    std::string filter_, selected_;
    bool filter_set_ = false, focus_grid_ = false;
    float width_ = 0;
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("library", LibraryScreen);
DEN_REGISTER_RAIL(10, "Library", "library");

} // namespace den::app
