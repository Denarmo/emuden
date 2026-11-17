// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen while a game runs on the main one: quick settings by touch. The main
// view is the chosen layout's (companion_quick.h); EmuDEN's own puts the game first, a card
// with its cover, its play time and the controls used most (pause, rewind, fast-forward,
// exit), and everything else one step in: the states with their pictures, the screen size,
// the picture preset and "Swap screens" (the game on this display, these settings on the
// other), the core options and buttons pages, the apps, the volumes. With an app open beside
// the game and these settings pulled over it, the first row goes back to that app or closes it.
// Route "companion.play" on the companion stage.

#include "companion_play.h"

#include <algorithm>
#include <format>

#include "companion_pictures.h"
#include "den/app/app.h"
#include "den/app/app_grid.h"
#include "den/app/apps_catalog.h"
#include "den/app/beside.h"
#include "den/app/picture_choice.h"
#include "den/app/registry.h"
#include "den/gfx/canvas.h"

namespace den::app {

namespace {

constexpr float BUTTON_HEIGHT = 56;
constexpr float ROW_HEIGHT = 64;
constexpr float GAP = 12;
constexpr float PICTURE_W = 300; // a card of the picture carousel

bool Playing(const player::Snapshot& snapshot) {
    return snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
}

bool Colored(const QuickSpec& spec) {
    return spec.background || spec.text || spec.dim || spec.panel || spec.accent;
}

} // namespace

void CompanionPlay::Build() {
    Services& svc = Svc();
    const player::Snapshot snapshot = svc.player->GetSnapshot();
    if (snapshot.game_id != game_id_) {
        view_ = QuickView::Main; // another game starts on the main view
    }
    spec_ = ChosenQuickLayout();
    const QuickLayout& layout = FindQuickLayout(spec_->layout);
    if (view_ == QuickView::More && layout.more == nullptr) {
        view_ = QuickView::Main;
    }
    key_ = Key(snapshot);
    game_id_ = snapshot.game_id;
    KeepPicturePreviews(game_id_);
    const auto game = svc.library->Find(snapshot.game_id);
    system_ = game ? game->system : std::string();
    Root().SetPadding(24, 16);
    // The game's own volume, and the system's while an app plays beside the game.
    other_volume_ = Beside().GetStage() != system::AppBeside::Stage::None;

    if (view_ == QuickView::Main) {
        // A rebuilt page keeps every scrolling node with an id where it was (ui::Page).
        QuickContext ctx{Root(), snapshot, game ? &*game : nullptr, *spec_};
        ctx.width = width_;
        ctx.height = height_;
        ctx.beside = [this](ui::Node& parent) { BesideRow(parent); };
        ctx.open = [this](QuickView view) { Open(view); };
        ctx.states = [this](ui::Node& parent, int columns) { BuildStates(parent, columns); };
        ctx.actions = Arranged(snapshot);
        layout.main(ctx);
        shape_ = Shape(width_, height_);
        return;
    }

    // A section fills the display.
    ui::Node& root = *Root().Add(ui::MakeBox(ui::Axis::Column, 12));
    root.id = "companion.section";
    root.grow = 1;
    BesideRow(root);
    switch (view_) {
    case QuickView::Main:
        break;
    case QuickView::States:
        Heading(root, "States");
        BuildStates(root);
        break;
    case QuickView::Screen:
        Heading(root, "Screen and picture");
        BuildScreen(root, snapshot);
        break;
    case QuickView::Picture:
        Heading(root, PictureName(snapshot.picture, system_), QuickView::Screen);
        BuildPicture(root);
        break;
    case QuickView::Controls:
        Heading(root, "Controls and core");
        BuildControls(root);
        break;
    case QuickView::Volume:
        Heading(root, "Volume");
        BuildVolume(root);
        break;
    case QuickView::More: {
        Heading(root, "More");
        QuickContext ctx{root, snapshot, game ? &*game : nullptr, *spec_};
        ctx.width = width_;
        ctx.height = height_;
        ctx.open = [this](QuickView view) { Open(view); };
        ctx.actions = Arranged(snapshot);
        layout.more(ctx);
        break;
    }
    }
}

void CompanionPlay::Update(ui::Context&) {
    Services& svc = Svc();
    const player::Snapshot snapshot = svc.player->GetSnapshot();
    if (!Playing(snapshot)) {
        return; // the controller swaps the page
    }
    // The player screen sends its own speed only when that changes: what was chosen
    // here is kept up.
    if (snapshot.state == player::State::Running) {
        if (rewind_ && snapshot.can_rewind && !snapshot.rewinding) {
            svc.player->SetRewind(true);
        } else if (fast_ && !rewind_ && !snapshot.fast_forward && !snapshot.rewinding) {
            svc.player->SetFastForward(true);
        }
    }
    if (Key(snapshot) != key_ || (view_ == QuickView::Main && Shape(width_, height_) != shape_)) {
        Rebuild();
        return;
    }
    if (ui::Node* shelf = reveal_index_ >= 0 ? Root().FindById("companion.pictures") : nullptr;
        shelf && shelf->rect.w > 0) {
        const float start = static_cast<float>(reveal_index_) * (PICTURE_W + GAP);
        shelf->scroller.Jump(std::clamp(start - (shelf->rect.w - PICTURE_W) * 0.5f, 0.0f, shelf->scroller.Max()));
        reveal_index_ = -1;
    }
}

void CompanionPlay::Draw(ui::Context& ctx, const gfx::Rect& area) {
    width_ = area.w;
    height_ = area.h;
    if (!spec_ || !Colored(*spec_)) {
        CompanionPage::Draw(ctx, area);
        return;
    }
    // The layout's colours, for this page only.
    theme_ = *ctx.theme;
    theme_.page = spec_->background.value_or(theme_.page);
    theme_.fg = spec_->text.value_or(theme_.fg);
    theme_.text_dim = spec_->dim.value_or(theme_.text_dim);
    theme_.panel = spec_->panel.value_or(theme_.panel);
    theme_.panel_strong = spec_->accent.value_or(theme_.panel_strong);
    const ui::Theme* before = ctx.theme;
    ctx.theme = &theme_;
    if (spec_->background) {
        ctx.canvas->FillRect(area, *spec_->background);
    }
    CompanionPage::Draw(ctx, area);
    ctx.theme = before;
}

void CompanionPlay::Open(QuickView view) {
    if (view_ == QuickView::Picture) {
        // Its sliders may have moved.
        ForgetPicturePreview(Svc().player->GetSnapshot().picture);
    }
    reveal_picture_ = view == QuickView::Screen && view_ != QuickView::Screen;
    if (view == QuickView::Screen && view_ != QuickView::Picture) {
        // The previews show the game as it is now.
        ResetPicturePreviews();
    }
    view_ = view;
    Rebuild();
}

void CompanionPlay::Heading(ui::Node& root, const std::string& title, QuickView back_to) {
    ui::Node* line = root.Add(ui::MakeBox(ui::Axis::Row, 12));
    line->align = ui::Align::Center;
    auto* back = line->Emplace<ui::Button>("Back", [this, back_to] { Open(back_to); });
    back->id = "companion.section_back";
    back->height = BUTTON_HEIGHT;
    back->SetPadding(22, 0);
    auto* label = line->Emplace<ui::Label>(title, Svc().theme->text_heading, gfx::FONT_BOLD);
    label->grow = 1;
}

void CompanionPlay::BesideRow(ui::Node& parent) {
    const system::AppBeside& beside = Beside();
    if (beside.GetStage() != system::AppBeside::Stage::SettingsOver) {
        return;
    }
    // These settings lie over an app the user opened beside the game: the way back to it
    // comes first.
    ui::Node* over = parent.Add(ui::MakeBox(ui::Axis::Row, 10));
    auto* back = over->Emplace<AppChip>("Back to " + beside.App().label, beside.App().icon);
    back->id = "companion.back_to_app";
    back->strong = true;
    back->grow = 1;
    back->height = ROW_HEIGHT;
    back->on_activate = [] { BesideBackToApp(); };
    auto* close = over->Emplace<ui::Button>("Close " + beside.App().label, [] { BesideClose(); });
    close->id = "companion.close_app";
    close->height = ROW_HEIGHT;
    close->SetPadding(18, 0);
}

std::string CompanionPlay::Key(const player::Snapshot& snapshot) const {
    int slots = 0;
    for (int slot = 0; slot < player::STATE_SLOTS; ++slot) {
        slots |= Svc().player->HasState(slot) ? 1 << slot : 0;
    }
    const GamePlayer* screen = CurrentPlayer();
    // The volumes are not in here: their sliders show the value as it changes, and a
    // page built again under a finger would end the drag.
    const system::AppBeside& beside = Beside();
    const std::shared_ptr<const QuickSpec> spec = ChosenQuickLayout();
    return std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", snapshot.game_id,
                       static_cast<int>(snapshot.state), snapshot.fast_forward, snapshot.rewinding,
                       snapshot.can_rewind, snapshot.can_save_states, snapshot.picture, slots,
                       screen ? screen->ScreenSizeIndex() : -1, static_cast<int>(beside.GetStage()),
                       beside.App().id, static_cast<int>(beside.GameScreen()), AppsRevision(),
                       static_cast<int>(view_), screen ? screen->FpsMode() : -1,
                       screen && screen->Widescreen(), spec->Key());
}

std::vector<QuickAction> CompanionPlay::Arranged(const player::Snapshot& snapshot) {
    std::vector<QuickAction> all = MakeActions(snapshot);
    std::vector<std::string> ids;
    for (const QuickAction& action : all) {
        ids.push_back(action.id);
    }
    std::vector<QuickAction> out;
    for (const std::string& id : ArrangeQuickActions(*spec_, ids)) {
        const auto it = std::find_if(all.begin(), all.end(), [&](const QuickAction& a) { return a.id == id; });
        out.push_back(std::move(*it));
    }
    return out;
}

int CompanionPlay::Shape(float width, float height) const {
    if (!spec_ || spec_->layout == "standard") {
        return 0; // one column whatever the shape
    }
    if (width <= 0 || height <= 0) {
        return -1;
    }
    const float aspect = width / height;
    return aspect < 0.95f ? 1 : aspect < 1.45f ? 2 : 3;
}

DEN_REGISTER_SCREEN("companion.play", CompanionPlay);

} // namespace den::app
