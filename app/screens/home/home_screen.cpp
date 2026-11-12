// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The first screen: the rail, a clock, the selected game's name and records, and shelves of
// covers ("Continue playing", favourites, one per console) over the selected cover, blurred.

#include <map>

#include "../details/launch.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "game_widgets.h"
#include "games.h"
#include "strings.h"

namespace den::app {

namespace {

constexpr int RECENT_LIMIT = 12;

class HomeScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = GamesRevision();
        selected_.clear();
        ui::Node* content = BuildRailFrame(*this, "");
        content->gap = 4;

        ui::Node* header = content->Add(ui::MakeBox(ui::Axis::Row, 16));
        header->pad_right = svc.theme->margin;
        header->align = ui::Align::Center;
        name_ = header->Emplace<ui::Label>(Tr("home.title"), svc.theme->text_title, gfx::FONT_BOLD);
        name_->grow = 1;
        clock_ = header->Emplace<ui::Label>(ClockText(), svc.theme->text_heading);
        clock_->tone = ui::Tone::Dim;
        records_ = content->Emplace<ui::Label>(" ");
        records_->tone = ui::Tone::Dim;
        records_->pad_bottom = 10;

        const std::vector<library::Entry> games = WithoutHidden(svc.library->Entries());
        if (games.empty()) {
            BuildEmpty(content);
            return;
        }
        ui::Node* list = content->Add(ui::MakeList(18));
        list->id = "home.shelves";
        list->pad_bottom = 24;

        AddShelf(list, "recent", Tr("home.continue"), WithoutHidden(svc.library->RecentlyPlayed(RECENT_LIMIT)),
                 Tr("home.continue_badge"));
        AddShelf(list, "favourites", Tr("home.favourites"), WithoutHidden(svc.library->Favourites()), "");

        std::map<std::string, std::vector<library::Entry>> by_system;
        for (const library::Entry& game : games) {
            by_system[game.system].push_back(game);
        }
        // Consoles in the plugins' order first, then whatever else the library holds.
        for (const plugins::Console& console : svc.plugins->Consoles()) {
            if (const auto it = by_system.find(console.id); it != by_system.end()) {
                AddShelf(list, "system." + console.id, console.name, it->second, "", false);
                by_system.erase(it);
            }
        }
        for (const auto& [system, entries] : by_system) {
            AddShelf(list, "system." + system, SystemName(system), entries, "", false);
        }
    }

    void Update(ui::Context&) override {
        if (GamesRevision() != revision_) {
            Rebuild();
            return;
        }
        if (clock_) {
            clock_->text = ClockText();
        }
    }

    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        // Behind the content only: the rail keeps its plain tint.
        const float rail = 232;
        backdrop_.Draw(ctx, gfx::Rect{rail, 0, ctx.canvas->Width() - rail, ctx.canvas->Height()});
        ui::Page::Draw(ctx, area);
    }

    bool OnEvent(const input::Event& event) override {
        using Type = input::Event::Type;
        if (event.type == Type::PointerUp) {
            by_pointer_ = true;
        } else if (event.type == Type::Nav) {
            by_pointer_ = false;
        }
        return false;
    }

    bool OnNav(input::Nav nav) override {
        ui::Stage& stage = *Svc().stage;
        switch (nav) {
        case input::Nav::Option:
            if (OnGame()) {
                stage.Push("details", {{"id", selected_}});
            }
            return true;
        case input::Nav::Extra:
            if (const auto game = OnGame() ? Svc().library->Find(selected_) : std::nullopt) {
                Svc().library->SetFavourite(game->id, !game->favourite);
                stage.Toast(TrF(game->favourite ? "home.unfavourited" : "home.favourited", game->name), 2.0f);
            }
            return true;
        case input::Nav::View:
            stage.Replace("search");
            return true;
        case input::Nav::Back:
            return true; // the first page: nowhere to go back to
        default:
            return false;
        }
    }

    std::vector<ui::Hint> Hints() const override {
        if (!OnGame()) {
            return {{input::Nav::Confirm, Tr("hint.select")}, {input::Nav::View, Tr("hint.search")}};
        }
        return {{input::Nav::Confirm, Tr("hint.start")},
                {input::Nav::Option, Tr("hint.details")},
                {input::Nav::Extra, Tr("hint.favourite")},
                {input::Nav::View, Tr("hint.search")}};
    }

private:
    // Whether the focus is on a game (not on the rail or a button).
    bool OnGame() const {
        return !selected_.empty() && Focus() && Focus()->id.ends_with("." + selected_);
    }

    void BuildEmpty(ui::Node* content) {
        name_->text = Tr("home.empty_title");
        records_->text = Tr("home.empty_message");
        ui::Node* row = content->Add(ui::MakeBox(ui::Axis::Row, 12));
        row->pad_top = 12;
        const auto go = [&](const char* id, const std::string& text, const char* route) {
            if (!Svc().stage->HasRoute(route)) {
                return;
            }
            auto* button = row->Emplace<ui::Button>(text, [route] { Svc().stage->Push(route); });
            button->id = id;
            button->width = 240;
        };
        go("home.add_folders", Tr("home.add_folders"), "folders");
        go("home.open_settings", Tr("rail.settings"), "settings");
    }

    void AddShelf(ui::Node* list, const std::string& id, const std::string& title,
                  const std::vector<library::Entry>& games, const std::string& badge, bool show_badge = true) {
        if (games.empty()) {
            return;
        }
        ui::Node* section = list->Add(ui::MakeBox(ui::Axis::Column, 6));
        auto heading = ui::MakeHeading(title);
        heading->size = 20;
        heading->tone = ui::Tone::Dim;
        section->Add(std::move(heading));
        ui::Node* shelf = section->Add(ui::MakeShelf(18));
        shelf->id = "shelf." + id;
        shelf->SetPadding(10, 10);
        shelf->pad_right = 32;
        GameTileOptions options;
        options.id_prefix = id;
        options.badge = badge;
        options.show_badge = show_badge;
        // A tap opens the game's page; Confirm on a pad or the keyboard starts it.
        options.on_activate = [this](const library::Entry& game) {
            if (by_pointer_) {
                Svc().stage->Push("details", {{"id", game.id}});
            } else {
                StartGame(game.id);
            }
        };
        options.on_focus = [this](const library::Entry& game) { Select(game); };
        for (const library::Entry& game : games) {
            AddGameTile(shelf, game, options);
        }
    }

    void Select(const library::Entry& game) {
        selected_ = game.id;
        name_->text = game.name;
        records_->text = RecordsLine(game);
        backdrop_.Set(game.cover);
    }

    Backdrop backdrop_;
    ui::Label* name_ = nullptr;
    ui::Label* records_ = nullptr;
    ui::Label* clock_ = nullptr;
    std::string selected_;
    bool by_pointer_ = false;
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("home", HomeScreen);
DEN_REGISTER_RAIL(0, "Home", "home");

DEN_REGISTER_SETTINGS(40, "Games", [] {
    std::vector<ui::RowSpec> rows;
    ui::RowSpec hidden;
    hidden.kind = ui::RowSpec::Kind::Action;
    hidden.id = "library.show_hidden";
    hidden.label = Tr("settings.show_hidden");
    hidden.description = Tr("settings.show_hidden_help");
    hidden.value = [] { return std::to_string(HiddenCount()); };
    hidden.action = [] {
        if (HiddenCount() > 0) {
            ShowAllHidden();
            Svc().stage->Toast(Tr("settings.hidden_shown"));
        }
    };
    rows.push_back(std::move(hidden));
    return rows;
});

} // namespace den::app
