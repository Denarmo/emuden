// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A game's page: cover, name, console, records and the actions. Route "details", param id.

#include "../home/collections.h"
#include "../home/game_widgets.h"
#include "../home/games.h"
#include "../home/strings.h"
#include "../sync/sync_glue.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "game_prefs.h"
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
        // Centred when it all fits; on a small screen the facts and buttons scroll under the
        // title, above the hint bar.
        root.align = compact_ ? ui::Align::Stretch : ui::Align::Center;
        root.justify = ui::Align::Center;
        side_ = nullptr;
        root.SetPadding(svc.theme->margin * 2, PAD);
        if (compact_) {
            root.pad_bottom = PAD + HINTS;
        }
        if (!found) {
            root.Emplace<ui::Label>(Tr("launch.gone"));
            return;
        }
        const library::Entry game = *found;
        backdrop_.Set(game.cover);

        ui::Node* cover = root.Add(ui::MakeBox(ui::Axis::Column, 0));
        cover->justify = ui::Align::Center;
        cover->Emplace<CoverView>(game.name, game.cover, COVER_SIDE_W, COVER_SIDE_H)->overlay_title =
            game.placeholder_cover;

        ui::Node* side = root.Add(ui::MakeBox(ui::Axis::Column, 6));
        if (compact_) {
            side->grow = 1;
        } else {
            side->width = 520;
        }
        side_ = side;
        side->Emplace<ui::Label>(game.name, svc.theme->text_title, gfx::FONT_BOLD);
        auto* console = side->Emplace<ui::Label>(SystemName(game.system), svc.theme->text_heading, gfx::FONT_BOLD);
        console->tone = ui::Tone::Dim;
        console->pad_bottom = 6;
        ui::Node* body = compact_ ? side->Add(ui::MakeList(6)) : side;
        if (compact_) {
            body->id = "details.body";
        }
        const auto fact = [&](const std::string& text) {
            body->Emplace<ui::Label>(text)->tone = ui::Tone::Dim;
        };
        fact(TrF("details.play_time", game.play_seconds > 0 ? PlayTimeText(game.play_seconds)
                                                            : Tr("records.not_played")));
        fact(TrF("details.last_played", LastPlayedText(game.last_played)));
        if (game.play_count > 0) {
            fact(TrF("details.times_played", game.play_count));
            fact(TrF("details.average", PlayTimeText(game.play_seconds / game.play_count)));
        }
        if (game.added > 0) {
            fact(TrF("details.added", LastPlayedText(game.added)));
        }
        std::string in;
        for (const std::string& name : Collections()) {
            if (InCollection(name, id)) {
                in += (in.empty() ? "" : ", ") + name;
            }
        }
        if (!in.empty()) {
            fact(TrF("details.collections", in));
        }
        if (!game.path.empty()) {
            fact(TrF("details.file", PathToUtf8(PathFromUtf8(game.path).filename())));
        }
        if (game.size_bytes > 0) {
            fact(TrF("details.size", SizeText(game.size_bytes)));
        }
        if (const auto core = CoreForGame(game)) {
            const std::string name = core->name.empty() ? core->id : core->name;
            fact(TrF("details.core", core->installed ? name : TrF("details.core_missing", name)));
        }
        const auto console_of = svc.plugins->FindConsole(game.system);
        if (sync::Engine* engine = SyncSvc(); engine && console_of && console_of->group != "pc") {
            const sync::GameSync saves = engine->GameState(id);
            if (saves.active) {
                fact(saves.conflict  ? std::string("Saves synced, with a conflict: see Settings, Sync")
                     : saves.waiting ? std::string("Saves sync when the game ends")
                     : saves.synced > 0 ? "Saves synced " + SyncTimeText(saves.synced)
                                        : std::string("Saves not synced yet"));
            }
        }

        body->Emplace<ui::Node>()->height = 12;
        ui::Node* actions = compact_ ? body : side->Add(ui::MakeBox(ui::Axis::Column, 8));
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
        if (console_info && console_info->runner == plugins::Runner::Core) {
            action("details.together", "Play together", [id] { Svc().stage->Push("together", {{"id", id}}); });
        }
        action("details.favourite", game.favourite ? Tr("details.unfavourite") : Tr("details.favourite"),
               [id, now = game.favourite] { Svc().library->SetFavourite(id, !now); });
        action("details.collect", Tr("details.collect"), [id] { Svc().stage->Push("collect", {{"id", id}}); });
        if (console_info && console_info->runner == plugins::Runner::Core) {
            action("details.find_cover", "Find cover", [id] { Svc().stage->Push("find_cover", {{"id", id}}); });
        }
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
        if (!compact_ && side_ && side_->rect.h > Root().rect.h - PAD * 2 - HINTS) {
            compact_ = true;
            Rebuild();
        } else if (GamesRevision() != revision_) {
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
    static constexpr float PAD = 28, HINTS = 44;
    std::uint64_t revision_ = 0;
    ui::Node* side_ = nullptr;
    bool compact_ = false;
};

} // namespace

DEN_REGISTER_SCREEN("details", DetailsScreen);

} // namespace den::app
