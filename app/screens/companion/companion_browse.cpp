// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen while browsing: the game the main screen has in focus, with its cover
// (Shelf's object of it when Shelf is the Home interface), what the LaunchBox Games Database
// says about it, its records and the actions one tap away. Route "companion.browse" on the
// companion stage.

#include <cctype>
#include <format>

#include "den/app/game_prefs.h"
#include "den/app/launch.h"
#include "den/app/game_widgets.h"
#include "den/app/games.h"
#include "den/app/shelf_parts.h"
#include "den/app/model_widgets.h"
#include "companion_page.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/base/jobs.h"
#include "den/app/second_screen.h"

namespace den::app {

namespace {

constexpr float COVER_W = 186, COVER_H = 248;
constexpr float MODEL_W = 250; // a cartridge before its box is wider than a cover
constexpr float ACTION_HEIGHT = 54;
constexpr float PAD_X = 28, PAD_Y = 22, GAP = 14;

// "2003 · Action, Platform", "Konami / Nintendo", "Up to 4 players · rated 4.6 of 5".
std::vector<std::string> DetailLines(const library::GameDetails& d) {
    std::vector<std::string> out;
    const auto join = [](const std::vector<std::string>& parts, const char* between) {
        std::string line;
        for (const std::string& part : parts) {
            if (!part.empty()) {
                line += (line.empty() ? "" : between) + part;
            }
        }
        return line;
    };
    std::vector<std::string> genres(d.genres.begin(), d.genres.begin() + std::min<std::size_t>(d.genres.size(), 3));
    out.push_back(join({d.year > 0 ? std::to_string(d.year) : std::string(), join(genres, ", ")}, " · "));
    out.push_back(join({d.developer, d.publisher != d.developer ? d.publisher : std::string()}, " / "));
    std::string rating;
    if (d.rating > 0 && d.votes > 0) {
        rating = std::format("rated {:.1f} of 5", d.rating);
    }
    const std::string players = d.players > 1 ? std::format("Up to {} players", d.players)
                                : d.players == 1 ? std::string("1 player")
                                                 : std::string();
    std::string last = join({players, rating}, " · ");
    if (!last.empty()) {
        last[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(last[0])));
    }
    out.push_back(std::move(last));
    std::erase_if(out, [](const std::string& line) { return line.empty(); });
    return out;
}

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
    ~CompanionBrowse() override {
        *alive_ = false;
        if (job_) {
            job_->Cancel();
        }
    }

    void Build() override {
        Services& svc = Svc();
        game_ = CompanionGame();
        revision_ = GamesRevision();
        overview_ = nullptr;
        ui::Node& root = Root();
        root.SetPadding(PAD_X, PAD_Y);
        const auto found = game_.empty() ? std::nullopt : svc.library->Find(game_);
        if (!found) {
            root.axis = ui::Axis::Row;
            root.align = ui::Align::Center;
            root.justify = ui::Align::Center;
            backdrop_.Set({});
            auto* hint = root.Emplace<ui::Label>("Choose a game on the main screen", svc.theme->text_heading);
            hint->tone = ui::Tone::Dim;
            return;
        }
        root.axis = ui::Axis::Column;
        root.gap = GAP;
        const library::Entry game = *found;
        const std::string id = game.id;
        backdrop_.Set(game.cover);
        AskDetails(id);
        const library::GameDetails* details = details_id_ == id ? &details_ : nullptr;

        ui::Node* top = root.Add(ui::MakeBox(ui::Axis::Row, 24));
        top->height = COVER_H;
        top->align = ui::Align::Center;
        if (std::unique_ptr<ui::Node> model = MakeShelfPreview(game, MODEL_W, COVER_H)) {
            top->Add(std::move(model));
        } else {
            auto* view = top->Emplace<CoverView>(game.name, game.cover, COVER_W, COVER_H);
            view->overlay_title = game.placeholder_cover;
            view->hack = IsHackGame(game);
        }
        ui::Node* side = top->Add(ui::MakeBox(ui::Axis::Column, 4));
        side->grow = 1;
        side->Emplace<ui::Label>(game.name, svc.theme->text_title, gfx::FONT_BOLD);
        auto* console = side->Emplace<ui::Label>(SystemName(game.system), svc.theme->text_heading, gfx::FONT_BOLD);
        console->tone = ui::Tone::Dim;
        console->pad_bottom = 6;
        if (details != nullptr) {
            for (const std::string& line : DetailLines(*details)) {
                side->Emplace<ui::Label>(line);
            }
        }
        const auto fact = [&](const std::string& text) {
            auto* label = side->Emplace<ui::Label>(text);
            label->tone = ui::Tone::Dim;
            return label;
        };
        fact("Played " + (game.play_seconds > 0 ? PlayTimeText(game.play_seconds) : std::string("not yet")))->pad_top = 6;
        if (game.last_played > 0) {
            fact("Last played " + LastPlayedText(game.last_played));
        }
        if (const auto core = CoreForGame(game)) {
            const std::string name = core->name.empty() ? core->id : core->name;
            fact(core->installed ? "Runs with " + name : name + " is downloaded on Play");
        }

        // The overview takes what the picture and the buttons leave.
        if (details != nullptr && !details->overview.empty()) {
            std::string text = details->overview;
            for (std::size_t at; (at = text.find("\n\n")) != std::string::npos;) {
                text.erase(at, 1); // no blank lines: the room is short
            }
            auto* overview = root.Emplace<Paragraph>(std::move(text), 400.0f);
            overview->clip = true;
            overview->tone = ui::Tone::Dim;
            overview->height = COMPANION_HEIGHT - PAD_Y * 2 - COVER_H - ACTION_HEIGHT - GAP * 2;
            overview_ = overview;
        } else {
            root.Add(ui::MakeBox(ui::Axis::Column, 0))->grow = 1;
        }

        ui::Node* actions = root.Add(ui::MakeBox(ui::Axis::Row, 10));
        actions->height = ACTION_HEIGHT;
        const auto action = [&](const char* node_id, const std::string& text, std::function<void()> fn) {
            auto* button = actions->Emplace<ui::Button>(text, std::move(fn));
            button->id = node_id;
            button->grow = 1;
            button->height = ACTION_HEIGHT;
            return button;
        };
        action("companion.play", game.last_played > 0 ? "Continue" : "Play", [id] { StartGame(id); });
        action("companion.favourite", "Favourite", [id, now = game.favourite] {
            Svc().library->SetFavourite(id, !now);
        })->selected = game.favourite;
        action("companion.details", "Details", [id] { OpenOnMain("details", id); });
        action("companion.settings", "Settings", [id] { OpenOnMain("game_settings", id); });
    }

    void Update(ui::Context&) override {
        if (CompanionGame() != game_ || GamesRevision() != revision_) {
            Rebuild();
        }
    }

    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        if (overview_ != nullptr) {
            overview_->wrap_width = std::max(200.0f, area.w - PAD_X * 2);
        }
        backdrop_.Draw(ctx, gfx::Rect{0, 0, ctx.canvas->Width(), ctx.canvas->Height()});
        CompanionPage::Draw(ctx, area);
    }

private:
    // The database's details of the game, fetched once in the background.
    void AskDetails(const std::string& id) {
        if (details_id_ == id || asked_ == id) {
            return;
        }
        asked_ = id;
        auto result = std::make_shared<library::GameDetails>();
        job_ = Svc().jobs->Run(
            "Game details",
            [id, result](Job& job) {
                *result = Svc().library->Details(id, [&job] { return job.CancelRequested(); });
                return Status{};
            },
            [this, id, result, alive = alive_](Job&) {
                if (!*alive || asked_ != id) {
                    return;
                }
                details_ = *result;
                details_id_ = id;
                if (result->found) {
                    Rebuild();
                }
            });
    }

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    JobPtr job_;
    std::string asked_, details_id_;
    library::GameDetails details_;
    Paragraph* overview_ = nullptr;
    Backdrop backdrop_;
    std::string game_;
    std::uint64_t revision_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("companion.browse", CompanionBrowse);

} // namespace den::app
