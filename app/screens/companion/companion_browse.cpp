// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen while browsing: the game the main screen has in focus, with its cover,
// records and the actions one tap away. Route "companion.browse" on the companion stage.

#include "../details/game_prefs.h"
#include "../details/launch.h"
#include "../home/game_widgets.h"
#include "../home/games.h"
#include "companion_page.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "second_screen.h"

namespace den::app {

namespace {

constexpr float COVER_SIDE_W = 228, COVER_SIDE_H = 304;
constexpr float ACTION_HEIGHT = 58;

// Shows a page of the main stage for the game, unless it is the one already there.
void OpenOnMain(const std::string& route, const std::string& id) {
    ui::Stage& stage = *Svc().stage;
    const ui::Page* top = stage.Top();
    if (top && top->Route() == route && top->Param("id") == id) {
        return;
    }
    stage.Push(route, {{"id", id}});
}

class CompanionBrowse final : public CompanionPage {
public:
    void Build() override {
        Services& svc = Svc();
        game_ = CompanionGame();
        revision_ = GamesRevision();
        ui::Node& root = Root();
        root.axis = ui::Axis::Row;
        root.gap = 28;
        root.align = ui::Align::Center;
        root.justify = ui::Align::Center;
        root.SetPadding(28, 24);
        const auto found = game_.empty() ? std::nullopt : svc.library->Find(game_);
        if (!found) {
            backdrop_.Set({});
            auto* hint = root.Emplace<ui::Label>("Choose a game on the main screen", svc.theme->text_heading);
            hint->tone = ui::Tone::Dim;
            return;
        }
        const library::Entry game = *found;
        const std::string id = game.id;
        backdrop_.Set(game.cover);

        root.Emplace<CoverView>(game.name, game.cover, COVER_SIDE_W, COVER_SIDE_H)->overlay_title =
            game.placeholder_cover;

        ui::Node* side = root.Add(ui::MakeBox(ui::Axis::Column, 4));
        side->grow = 1;
        side->Emplace<ui::Label>(game.name, svc.theme->text_title, gfx::FONT_BOLD);
        auto* console = side->Emplace<ui::Label>(SystemName(game.system), svc.theme->text_heading, gfx::FONT_BOLD);
        console->tone = ui::Tone::Dim;
        console->pad_bottom = 6;
        const auto fact = [&](const std::string& text) {
            side->Emplace<ui::Label>(text)->tone = ui::Tone::Dim;
        };
        fact("Played " + (game.play_seconds > 0 ? PlayTimeText(game.play_seconds) : std::string("not yet")));
        fact("Last played " + LastPlayedText(game.last_played));
        if (game.play_count > 0) {
            fact(std::to_string(game.play_count) + (game.play_count == 1 ? " session" : " sessions"));
        }
        if (game.added > 0) {
            fact("Added " + LastPlayedText(game.added));
        }
        if (const auto core = CoreForGame(game)) {
            const std::string name = core->name.empty() ? core->id : core->name;
            fact(core->installed ? "Runs with " + name : name + " is downloaded on Play");
        }

        ui::Node* actions = side->Add(ui::MakeBox(ui::Axis::Column, 10));
        actions->pad_top = 16;
        const auto action = [&](const char* node_id, const std::string& text, std::function<void()> fn) {
            auto* button = actions->Emplace<ui::Button>(text, std::move(fn));
            button->id = node_id;
            button->height = ACTION_HEIGHT;
            button->centered = false;
        };
        action("companion.play", game.last_played > 0 ? "Continue" : "Play", [id] { StartGame(id); });
        action("companion.favourite", game.favourite ? "Remove from favourites" : "Add to favourites",
               [id, now = game.favourite] { Svc().library->SetFavourite(id, !now); });
        action("companion.details", "Details", [id] { OpenOnMain("details", id); });
        action("companion.settings", "Game settings", [id] { OpenOnMain("game_settings", id); });
    }

    void Update(ui::Context&) override {
        if (CompanionGame() != game_ || GamesRevision() != revision_) {
            Rebuild();
        }
    }

    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        backdrop_.Draw(ctx, gfx::Rect{0, 0, ctx.canvas->Width(), ctx.canvas->Height()});
        CompanionPage::Draw(ctx, area);
    }

private:
    Backdrop backdrop_;
    std::string game_;
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("companion.browse", CompanionBrowse);

} // namespace den::app
