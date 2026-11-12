// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The first screen: the rail and shelves of games and consoles from the library and plugin
// facades.

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

constexpr float COVER_W = 168, COVER_H = 224;

class HomeScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        library_revision_ = svc.library->Revision();
        plugins_revision_ = svc.plugins->Revision();
        ui::Node* content = BuildRailFrame(*this, "Home");
        ui::Node* list = content->Add(ui::MakeList(22));
        list->id = "home.shelves";
        list->pad_bottom = 24;

        AddGames(list, "recent", "Continue playing", svc.library->RecentlyPlayed(12));
        AddGames(list, "favourites", "Favourites", svc.library->Favourites());
        AddGames(list, "all", "All games", svc.library->Entries());

        const std::vector<plugins::Console> consoles = svc.plugins->Consoles();
        if (!consoles.empty()) {
            ui::Node* shelf = AddShelf(list, "consoles", "Consoles");
            for (const plugins::Console& console : consoles) {
                auto* tile = shelf->Emplace<ui::Tile>(console.name, "", 224.0f, 126.0f);
                tile->id = "console." + console.id;
                tile->badge = console.installed ? "" : "Not installed";
                tile->on_activate = [id = console.id, name = console.name] {
                    Open("console", "system", id, name);
                };
            }
        }
    }

    void Update(ui::Context&) override {
        Services& svc = Svc();
        if (svc.library->Revision() != library_revision_ || svc.plugins->Revision() != plugins_revision_) {
            Rebuild();
        }
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Open"}};
    }

private:
    // Opens a screen another feature registers; says so while it does not exist yet.
    static void Open(const char* route, const char* key, const std::string& value,
                     const std::string& name) {
        ui::Stage& stage = *Svc().stage;
        if (stage.HasRoute(route)) {
            stage.Push(route, {{key, value}});
        } else {
            stage.Toast(name + ": this screen is not built yet");
        }
    }

    ui::Node* AddShelf(ui::Node* list, const std::string& id, const std::string& title) {
        ui::Node* section = list->Add(ui::MakeBox(ui::Axis::Column, 10));
        section->Add(ui::MakeHeading(title));
        ui::Node* shelf = section->Add(ui::MakeShelf(18));
        shelf->id = "shelf." + id;
        shelf->pad_top = 8;
        shelf->pad_bottom = 8;
        shelf->pad_left = 8;
        shelf->pad_right = 32;
        return shelf;
    }

    void AddGames(ui::Node* list, const std::string& id, const std::string& title,
                  const std::vector<library::Entry>& games) {
        if (games.empty()) {
            return;
        }
        ui::Node* shelf = AddShelf(list, id, title);
        for (const library::Entry& game : games) {
            auto* tile = shelf->Emplace<ui::Tile>(game.name, game.cover, COVER_W, COVER_H);
            tile->id = id + "." + game.id;
            if (const auto console = Svc().plugins->FindConsole(game.system)) {
                tile->badge = console->short_name;
            }
            tile->on_activate = [game_id = game.id, name = game.name] {
                Open("details", "id", game_id, name);
            };
        }
    }

    std::uint64_t library_revision_ = 0, plugins_revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("home", HomeScreen);
DEN_REGISTER_RAIL(0, "Home", "home");

} // namespace den::app
