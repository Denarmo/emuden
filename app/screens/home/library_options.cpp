// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the library offers besides its games: layout and order, the user's collections, the
// hidden games and the statistics. Routes "library_options", "hidden_games", "statistics";
// the same rows are the Games section of Settings.

#include <algorithm>
#include <ctime>
#include <map>

#include "den/app/collections.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/app/games.h"
#include "den/app/strings.h"

namespace den::app {

namespace {

std::vector<ui::RowSpec> OptionRows() {
    std::vector<ui::RowSpec> rows;
    rows.push_back(ChoiceRow(
        "library.view", Tr("options.view"), {Tr("options.view_grid"), Tr("options.view_list")},
        [] { return LibraryListView() ? 1 : 0; }, [](int index) { SetLibraryListView(index == 1); }));
    rows.push_back(ChoiceRow(
        "library.sort", Tr("options.sort"), SortNames(), [] { return static_cast<int>(LibrarySort()); },
        [](int index) { SetLibrarySort(static_cast<Sort>(index)); }));

    int index = 0;
    for (const std::string& name : Collections()) {
        ui::RowSpec row = ValueRow(
            "library.collection." + std::to_string(index++), name,
            [name] { return GameCount(CollectionIds(name).size()); },
            [name] {
                Svc().stage->Confirm(TrF("collections.delete_title", name), Tr("collections.delete_message"),
                                     Tr("collections.delete"), [name] {
                                         DeleteCollection(name);
                                         RebuildTop();
                                     });
            });
        row.description = Tr("collections.row_help");
        rows.push_back(std::move(row));
    }
    rows.push_back(ActionRow(
        "library.new_collection", Tr("collections.new"),
        [] {
            Svc().stage->Toast(TrF("collections.created", CreateCollection()));
            RebuildTop();
        },
        Tr("collections.new_help")));

    rows.push_back(ValueRow(
        "library.hidden", Tr("options.hidden"), [] { return std::to_string(HiddenCount()); },
        [] { Svc().stage->Push("hidden_games"); }));
    rows.push_back(ActionRow("library.statistics", Tr("options.statistics"),
                             [] { Svc().stage->Push("statistics"); }));
    return rows;
}

class LibraryOptionsScreen final : public ui::Page {
public:
    void Build() override {
        revision_ = GamesRevision();
        ui::Node* content = BuildPlainFrame(*this, Tr("options.title"));
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "options.list";
        list->pad_bottom = 24;
        AddRows(list, OptionRows());
    }
    void Update(ui::Context&) override {
        if (GamesRevision() != revision_) {
            Rebuild();
        }
    }

private:
    std::uint64_t revision_ = 0;
};

class HiddenGamesScreen final : public ui::Page {
public:
    void Build() override {
        revision_ = GamesRevision();
        ui::Node* content = BuildPlainFrame(*this, Tr("options.hidden"));
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "hidden.list";
        list->pad_bottom = 24;
        std::vector<ui::RowSpec> rows;
        for (const library::Entry& game : Svc().library->Entries()) {
            if (IsHidden(game.id)) {
                ui::RowSpec row = ValueRow("hidden." + game.id, game.name, [] { return Tr("hidden.show"); },
                                           [id = game.id] { SetHidden(id, false); });
                row.description = SystemName(game.system);
                rows.push_back(std::move(row));
            }
        }
        if (rows.empty()) {
            list->Emplace<ui::Label>(Tr("hidden.none"))->tone = ui::Tone::Dim;
            auto* back = list->Emplace<ui::Button>(Tr("common.back"), [] { Svc().stage->Pop(); });
            back->id = "hidden.back";
            back->width = 200;
            return;
        }
        rows.push_back(ActionRow("hidden.all", Tr("hidden.show_all"), [] { ShowAllHidden(); }));
        AddRows(list, std::move(rows));
    }
    void Update(ui::Context&) override {
        if (GamesRevision() != revision_) {
            Rebuild();
        }
    }

private:
    std::uint64_t revision_ = 0;
};

// Totals over the play records, the most played games and the time per console.
class StatisticsScreen final : public ui::Page {
public:
    void Build() override {
        ui::Node* content = BuildPlainFrame(*this, Tr("options.statistics"));
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "statistics.list";
        list->pad_bottom = 24;

        std::vector<library::Entry> games = Svc().library->Entries();
        std::int64_t total = 0;
        int played = 0, sessions = 0;
        std::map<std::string, std::int64_t> by_system;
        const library::Entry* last = nullptr;
        for (const library::Entry& game : games) {
            total += game.play_seconds;
            sessions += game.play_count;
            played += game.play_count > 0 || game.last_played > 0 ? 1 : 0;
            by_system[game.system] += game.play_seconds;
            if (game.last_played > 0 && (!last || game.last_played > last->last_played)) {
                last = &game;
            }
        }
        const auto text = [](std::string value) { return [value = std::move(value)] { return value; }; };
        std::vector<ui::RowSpec> rows;
        rows.push_back(ValueRow("statistics.total", Tr("statistics.total"), text(PlayTimeText(total))));
        rows.push_back(ValueRow("statistics.played", Tr("statistics.played"),
                                text(TrF("statistics.of", played, games.size()))));
        rows.push_back(ValueRow("statistics.sessions", Tr("statistics.sessions"), text(std::to_string(sessions))));
        // From the session log: what was played in the last seven days.
        const std::int64_t week_start = static_cast<std::int64_t>(std::time(nullptr)) - 7 * 24 * 3600;
        std::int64_t week_seconds = 0;
        std::size_t week_sessions = 0;
        for (const library::Session& session : Svc().library->Sessions(week_start)) {
            week_seconds += session.seconds;
            ++week_sessions;
        }
        rows.push_back(ValueRow(
            "statistics.week", Tr("statistics.week"),
            text(TrF("statistics.week_value", PlayTimeText(week_seconds),
                     week_sessions == 1 ? Tr("statistics.sessions_one")
                                        : TrF("statistics.sessions_count", week_sessions)))));
        if (last) {
            rows.push_back(ValueRow("statistics.last", Tr("statistics.last"),
                                    text(last->name + "  \xC2\xB7  " + LastPlayedText(last->last_played)),
                                    [id = last->id] { Svc().stage->Push("details", {{"id", id}}); }));
        }
        AddRows(list, std::move(rows));

        std::erase_if(games, [](const library::Entry& game) { return game.play_seconds <= 0; });
        SortGames(games, Sort::PlayTime);
        if (games.empty()) {
            return;
        }
        Heading(list, Tr("statistics.most_played"));
        std::vector<ui::RowSpec> most;
        for (std::size_t i = 0; i < games.size() && i < 5; ++i) {
            most.push_back(ValueRow("statistics.game." + games[i].id, games[i].name,
                                    text(PlayTimeText(games[i].play_seconds)),
                                    [id = games[i].id] { Svc().stage->Push("details", {{"id", id}}); }));
        }
        AddRows(list, std::move(most));

        Heading(list, Tr("statistics.by_console"));
        std::vector<std::pair<std::string, std::int64_t>> systems(by_system.begin(), by_system.end());
        std::sort(systems.begin(), systems.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::vector<ui::RowSpec> consoles;
        for (const auto& [system, seconds] : systems) {
            if (seconds > 0) {
                consoles.push_back(ValueRow("statistics.system." + system, SystemName(system),
                                            text(PlayTimeText(seconds))));
            }
        }
        AddRows(list, std::move(consoles));
    }

private:
    static void Heading(ui::Node* list, const std::string& title) {
        auto heading = ui::MakeHeading(title);
        heading->pad_top = 14;
        heading->pad_bottom = 4;
        list->Add(std::move(heading));
    }
};

} // namespace

DEN_REGISTER_SCREEN("library_options", LibraryOptionsScreen);
DEN_REGISTER_SCREEN("hidden_games", HiddenGamesScreen);
DEN_REGISTER_SCREEN("statistics", StatisticsScreen);
DEN_REGISTER_SETTINGS(40, "Games", OptionRows);

} // namespace den::app
