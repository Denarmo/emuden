// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A game's page: cover, name, console, records and the actions. Route "details", param id.

#include "../home/game_widgets.h"
#include "../home/games.h"
#include "../home/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "launch.h"

namespace den::app {

namespace {

constexpr float COVER_SIDE_W = 300, COVER_SIDE_H = 400;

class DetailsScreen final : public ui::Page {
public:
    void Build() override {
        Services& svc = Svc();
        revision_ = GamesRevision();
        const std::string id = Param("id");
        const auto found = svc.library->Find(id);
        ui::Node& root = Root();
        root.axis = ui::Axis::Row;
        root.gap = 56;
        root.align = ui::Align::Center;
        root.justify = ui::Align::Center;
        root.SetPadding(svc.theme->margin * 2, 28);
        if (!found) {
            root.Emplace<ui::Label>(Tr("launch.gone"));
            return;
        }
        const library::Entry game = *found;
        backdrop_.Set(game.cover);

        root.Emplace<CoverView>(game.name, game.cover, COVER_SIDE_W, COVER_SIDE_H)->overlay_title =
            game.placeholder_cover;

        ui::Node* side = root.Add(ui::MakeBox(ui::Axis::Column, 6));
        side->width = 520;
        side->Emplace<ui::Label>(game.name, svc.theme->text_title, gfx::FONT_BOLD);
        auto* console = side->Emplace<ui::Label>(SystemName(game.system), svc.theme->text_heading, gfx::FONT_BOLD);
        console->tone = ui::Tone::Dim;
        console->pad_bottom = 6;
        const auto fact = [&](const std::string& text) {
            side->Emplace<ui::Label>(text)->tone = ui::Tone::Dim;
        };
        fact(TrF("details.play_time", game.play_seconds > 0 ? PlayTimeText(game.play_seconds)
                                                            : Tr("records.not_played")));
        fact(TrF("details.last_played", LastPlayedText(game.last_played)));
        if (game.play_count > 0) {
            fact(TrF("details.times_played", game.play_count));
        }
        if (!game.path.empty()) {
            fact(TrF("details.file", PathToUtf8(PathFromUtf8(game.path).filename())));
        }
        if (game.size_bytes > 0) {
            fact(TrF("details.size", SizeText(game.size_bytes)));
        }
        if (const auto core = svc.plugins->CoreFor(game.system)) {
            const std::string name = core->name.empty() ? core->id : core->name;
            fact(TrF("details.core", core->installed ? name : TrF("details.core_missing", name)));
        }

        ui::Node* actions = side->Add(ui::MakeBox(ui::Axis::Column, 8));
        actions->pad_top = 18;
        const auto action = [&](const char* node_id, const std::string& text, std::function<void()> fn) {
            auto* button = actions->Emplace<ui::Button>(text, std::move(fn));
            button->id = node_id;
            button->centered = false;
        };
        action("details.play", game.last_played > 0 ? Tr("details.continue") : Tr("details.play"),
               [id] { StartGame(id); });
        const auto console_info = svc.plugins->FindConsole(game.system);
        if (game.last_played > 0 && console_info && console_info->runner == plugins::Runner::Core) {
            action("details.restart", Tr("details.restart"), [id] { StartGame(id, true); });
        }
        action("details.favourite", game.favourite ? Tr("details.unfavourite") : Tr("details.favourite"),
               [id, now = game.favourite] { Svc().library->SetFavourite(id, !now); });
        action("details.settings", Tr("details.settings"),
               [id] { Svc().stage->Push("game_settings", {{"id", id}}); });
        action("details.hide", Tr("details.hide"), [id, name = game.name] {
            Svc().stage->Confirm(Tr("details.hide_title"), TrF("details.hide_message", name), Tr("details.hide"),
                                 [id] {
                                     SetHidden(id, true);
                                     Svc().stage->Pop();
                                 });
        });
    }

    void Update(ui::Context&) override {
        if (GamesRevision() != revision_) {
            Rebuild();
        }
    }

    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        backdrop_.Draw(ctx, gfx::Rect{0, 0, ctx.canvas->Width(), ctx.canvas->Height()});
        ui::Page::Draw(ctx, area);
    }

    bool OnNav(input::Nav nav) override {
        if (nav == input::Nav::Extra) {
            if (const auto game = Svc().library->Find(Param("id"))) {
                Svc().library->SetFavourite(game->id, !game->favourite);
            }
            return true;
        }
        return false;
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, Tr("hint.select")},
                {input::Nav::Extra, Tr("hint.favourite")},
                {input::Nav::Back, Tr("common.back")}};
    }

private:
    Backdrop backdrop_;
    std::uint64_t revision_ = 0;
};

// The per-game settings page: a stub until the player's options land.
class GameSettingsScreen final : public ui::Page {
public:
    void Build() override {
        const auto game = Svc().library->Find(Param("id"));
        ui::Node* content = BuildPlainFrame(*this, game ? TrF("game_settings.title", game->name)
                                                        : Tr("details.settings"));
        content->Emplace<ui::Label>(Tr("game_settings.empty"))->tone = ui::Tone::Dim;
        auto* back = content->Emplace<ui::Button>(Tr("common.back"), [] { Svc().stage->Pop(); });
        back->id = "game_settings.back";
        back->width = 200;
    }
};

} // namespace

DEN_REGISTER_SCREEN("details", DetailsScreen);
DEN_REGISTER_SCREEN("game_settings", GameSettingsScreen);

} // namespace den::app
