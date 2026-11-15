// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The "carousel" and "shelf3d" layouts of interface plugins: consoles as coloured tabs along the top (the
// shoulder buttons step through them), one gliding row of covers, and the selected game's
// name and records under it, over a backdrop in the console's colour. The plugin's spec sets
// the colours, art and motion; the page is drawn with a copy of the app's theme in them.

#include <algorithm>
#include <map>

#include "../details/launch.h"
#include "../home/continue_row.h"
#include "../home/game_widgets.h"
#include "../home/games.h"
#include "../home/strings.h"
#include "cover_carousel.h"
#include "object_shelf.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/platform/pal.h"
#include "interfaces.h"

namespace den::app {

namespace {

constexpr int RECENT_LIMIT = 15;
constexpr int GRADIENT_BANDS = 16;
constexpr double CONTINUE_POLL = 2.0; // seconds between looks at what Continue shows

struct TabData {
    std::string key, label;
    gfx::Color color;
    std::string wallpaper;
    float aspect = 0.75f;
    std::vector<library::Entry> games;
    std::vector<ContinueCard> cards; // the Continue tab: one per game
};

// Where the user was, so a game started and left, or a trip to Settings, comes back to it.
struct Place {
    std::string tab, game;
};
Place& Remembered() {
    static Place place;
    return place;
}

gfx::Color Mix(gfx::Color a, gfx::Color b, float t) {
    const auto mix = [&](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(x + (static_cast<float>(y) - x) * std::clamp(t, 0.0f, 1.0f));
    };
    return gfx::Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

// A console's tab: a pill in its colour when chosen.
class Tab final : public ui::Node {
public:
    Tab(std::string text, gfx::Color color) : text_(std::move(text)), color_(color) {
        focusable = true;
        height = 44;
    }
    bool chosen = false;

    float FocusRadius(const ui::Theme&) const override {
        return rect.h * 0.5f;
    }

protected:
    ui::Size MeasureContent(ui::Context& ctx) override {
        return {ctx.Text().Measure(text_, ctx.theme->text_body, gfx::FONT_BOLD) + 36, 44};
    }
    void DrawContent(ui::Context& ctx) override {
        const ui::Theme& theme = *ctx.theme;
        fill_ = ui::Approach(fill_, chosen ? 1.0f : 0.0f, ctx.dt, 12.0f);
        ctx.canvas->FillRect(rect, Mix(theme.panel, color_, fill_), rect.h * 0.5f);
        const float size = theme.text_body;
        const float w = ctx.Text().Measure(text_, size, gfx::FONT_BOLD);
        ctx.canvas->DrawText(text_, rect.CenterX() - w * 0.5f, rect.CenterY() - size * 0.62f, size,
                             Mix(theme.text_dim, theme.fg, std::max(fill_, focused ? 1.0f : 0.0f)), gfx::FONT_BOLD);
    }

private:
    std::string text_;
    gfx::Color color_;
    float fill_ = 0;
};

class CarouselHome final : public ui::Page {
public:
    explicit CarouselHome(std::shared_ptr<const InterfaceSpec> spec) : spec_(std::move(spec)) {
        theme_ = *Svc().theme;
        theme_.page = spec_->background;
        theme_.fg = spec_->text;
        theme_.text_dim = spec_->dim;
        theme_.panel = spec_->panel;
        theme_.panel_strong = spec_->panel.WithAlpha(std::min(1.0f, spec_->panel.a / 255.0f * 2.2f));
    }

    void Build() override {
        revision_ = GamesRevision();
        BuildTabs();
        ui::Node& root = Root();
        root.axis = ui::Axis::Column;
        root.SetPadding(theme_.margin, 18);
        root.gap = 6;

        ui::Node* bar = root.Add(ui::MakeBox(ui::Axis::Row, 10));
        bar->align = ui::Align::Center;
        bar->chrome = true;
        ui::Node* strip = bar->Add(ui::MakeBox(ui::Axis::Row, 10));
        strip->id = "iface.tabs";
        strip->scrollable = true;
        strip->grow = 1;
        strip->width = 0; // the room the clock leaves, however many tabs there are
        strip->SetPadding(6, 6);
        tab_nodes_.clear();
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            auto* tab = strip->Emplace<Tab>(tabs_[i].label, tabs_[i].color);
            tab->id = "iface.tab." + tabs_[i].key;
            tab->on_focus = [this, i] { ChooseTab(i, false); };
            tab->on_activate = [this] { FocusCarousel(); };
            tab_nodes_.push_back(tab);
        }
        clock_ = bar->Emplace<ui::Label>(ClockText(), theme_.text_heading);
        clock_->tone = ui::Tone::Dim;
        clock_->pad_left = 12;
        clock_->pad_right = 8;
        clock_->width = Svc().canvas->GetText().Measure("00:00", theme_.text_heading) + 28;

        if (tabs_.empty()) {
            BuildEmpty(root);
            AddScreens(root);
            return;
        }
        if (spec_->layout == "shelf3d") {
            carousel_ = root.Emplace<ObjectShelf>(spec_);
        } else {
            carousel_ = root.Emplace<CoverCarousel>(spec_);
        }
        carousel_->on_select = [this](const library::Entry& game) { ShowGame(game); };
        carousel_->on_activate = [this] {
            const library::Entry* game = carousel_->Selected();
            if (game == nullptr || carousel_->Launching()) {
                return;
            }
            if (const ContinueCard* card = SelectedCard()) {
                if (card->item.game_id.empty()) {
                    ChooseContinue(*card); // only another device has it: its download
                } else {
                    carousel_->Launch([card = *card] { ChooseContinue(card); });
                }
                return;
            }
            carousel_->Launch([id = game->id] { StartGame(id); });
        };

        below_ = root.Add(ui::MakeBox(ui::Axis::Column, 6));
        ui::Node* info = below_->Add(ui::MakeBox(ui::Axis::Column, 4));
        info->align = ui::Align::Center;
        info->pad_bottom = 8;
        moment_ = info->Emplace<MomentPicture>(224, 126);
        moment_->visible = false;
        name_ = info->Emplace<ui::Label>(" ", theme_.text_title, gfx::FONT_BOLD);
        name_->text_align = ui::Align::Center;
        records_ = info->Emplace<ui::Label>(" ");
        records_->tone = ui::Tone::Dim;
        records_->text_align = ui::Align::Center;
        position_ = info->Emplace<ui::Label>(" ", theme_.text_small);
        position_->tone = ui::Tone::Dim;
        position_->text_align = ui::Align::Center;
        AddScreens(*below_);

        std::size_t chosen = 0;
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            chosen = tabs_[i].key == Remembered().tab ? i : chosen;
        }
        ChooseTab(chosen, true);
        SetFocus(carousel_, false);
        color_ = tabs_[tab_].color;
    }

    void Update(ui::Context&) override {
        if (GamesRevision() != revision_) {
            Rebuild();
            return;
        }
        if (const double now = pal::NowSeconds(); now - polled_ >= CONTINUE_POLL) {
            polled_ = now;
            if (ContinueSignature(ContinueCards()) != signature_) {
                Rebuild();
                return;
            }
            if (const ContinueCard* card = SelectedCard()) {
                records_->text = ContinueLine(*card); // "5 min ago" moves on
            }
        }
        if (clock_) {
            clock_->text = ClockText();
        }
    }

    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        ui::Context themed = ctx;
        themed.theme = &theme_;
        const TabData* tab = tab_ < tabs_.size() ? &tabs_[tab_] : nullptr;
        color_ = Mix(color_, tab ? tab->color : spec_->accent, std::min(1.0f, ctx.dt * 6));
        DrawBackdrop(themed, area, tab);
        // A launch into a console slides the title and buttons down out of sight, its stage
        // taking their room.
        const float away = carousel_ ? carousel_->LaunchStage() : 0;
        if (below_ != nullptr) {
            // Its room shrinks while what it holds moves down past the bottom.
            below_->height = away > 0 ? below_height_ * (1 - away) : ui::AUTO;
            below_->pad_top = below_height_ * away * 1.5f;
        }
        ui::Page::Draw(themed, area);
        if (below_ != nullptr && away <= 0) {
            below_height_ = below_->rect.h;
        }
        if (carousel_ && carousel_->LaunchCover() > 0) {
            ctx.canvas->FillRect(area, color_.WithAlpha(carousel_->LaunchCover() * 0.92f));
        }
    }

    bool OnNav(input::Nav nav) override {
        ui::Stage& stage = *Svc().stage;
        const library::Entry* game = OnCarousel() ? carousel_->Selected() : nullptr;
        switch (nav) {
        case input::Nav::ShoulderL:
        case input::Nav::ShoulderR:
            if (!tabs_.empty()) {
                const std::size_t count = tabs_.size();
                ChooseTab((tab_ + (nav == input::Nav::ShoulderR ? 1 : count - 1)) % count, false);
                FocusCarousel();
            }
            return true;
        case input::Nav::Option:
            if (game && Svc().library->Find(game->id)) {
                stage.Push("details", {{"id", game->id}});
            }
            return true;
        case input::Nav::Extra:
            if (game && Svc().library->Find(game->id)) {
                Svc().library->SetFavourite(game->id, !game->favourite);
                stage.Toast(TrF(game->favourite ? "home.unfavourited" : "home.favourited", game->name), 2.0f);
            }
            return true;
        case input::Nav::View:
            stage.Replace("search");
            return true;
        case input::Nav::Menu:
            stage.Replace("settings");
            return true;
        case input::Nav::Back:
            if (!OnCarousel() && carousel_) {
                FocusCarousel();
            }
            return true; // the first page: nowhere to go back to
        default:
            return false;
        }
    }

    std::vector<ui::Hint> Hints() const override {
        std::vector<ui::Hint> hints;
        if (carousel_ && carousel_->Launching()) {
            return hints; // the launch has the whole screen
        }
        if (OnCarousel() && carousel_->Selected()) {
            hints = {{input::Nav::Confirm, Tr("hint.start")},
                     {input::Nav::Option, Tr("hint.details")},
                     {input::Nav::Extra, Tr("hint.favourite")}};
        } else {
            hints = {{input::Nav::Confirm, Tr("hint.select")}};
        }
        if (tabs_.size() > 1) {
            hints.push_back({input::Nav::ShoulderR, Tr("hint.next_console")});
        }
        hints.push_back({input::Nav::Menu, Tr("rail.settings")});
        return hints;
    }

private:
    bool OnCarousel() const {
        return carousel_ != nullptr && Focus() == carousel_;
    }

    // The Continue card of the selected game, when the Continue tab is chosen.
    const ContinueCard* SelectedCard() const {
        if (carousel_ == nullptr || tab_ >= tabs_.size() || tabs_[tab_].cards.empty()) {
            return nullptr;
        }
        const std::vector<ContinueCard>& cards = tabs_[tab_].cards;
        return carousel_->Index() < cards.size() ? &cards[carousel_->Index()] : nullptr;
    }

    void FocusCarousel() {
        if (carousel_) {
            SetFocus(carousel_, false);
        }
    }

    void BuildTabs() {
        Services& svc = Svc();
        tabs_.clear();
        std::vector<library::Entry> games = WithoutHidden(svc.library->Entries());
        if (games.empty()) {
            return;
        }
        std::sort(games.begin(), games.end(), [](const library::Entry& a, const library::Entry& b) {
            return ToLower(a.name) < ToLower(b.name);
        });
        const auto add = [&](std::string key, std::string label, const ConsoleLook& look, float aspect,
                             std::vector<library::Entry> list) {
            if (!list.empty()) {
                tabs_.push_back({std::move(key), look.label.empty() ? std::move(label) : look.label, look.color,
                                 look.wallpaper.empty() ? spec_->wallpaper : look.wallpaper, aspect, std::move(list)});
            }
        };
        cards_ = ContinueCards();
        signature_ = ContinueSignature(cards_);
        polled_ = pal::NowSeconds();
        if (spec_->tab_continue && !cards_.empty()) {
            std::vector<library::Entry> list;
            for (const ContinueCard& card : cards_) {
                library::Entry entry = card.game;
                entry.id = ContinueId(card);
                list.push_back(std::move(entry));
            }
            // A plugin without a "continue" look gives it its Recent tab's.
            const ConsoleLook look = spec_->consoles.contains("continue") ? spec_->LookFor("continue", "")
                                                                          : spec_->LookFor("recent", "recent");
            add("continue", Tr("home.tab_continue"), look, 0.75f, std::move(list));
            tabs_.back().cards = cards_;
        }
        if (spec_->tab_recent) {
            add("recent", Tr("home.tab_recent"), spec_->LookFor("recent", "recent"), 0.75f,
                WithoutHidden(svc.library->RecentlyPlayed(RECENT_LIMIT)));
        }
        if (spec_->tab_all) {
            add("all", Tr("home.tab_all"), spec_->LookFor("all", "all"), 0.75f, games);
        }
        std::map<std::string, std::vector<library::Entry>> by_system;
        for (const library::Entry& game : games) {
            by_system[game.system].push_back(game);
        }
        for (const plugins::Console& console : svc.plugins->Consoles()) {
            if (const auto it = by_system.find(console.id); it != by_system.end()) {
                const std::string label = console.short_name.empty() ? console.name : console.short_name;
                add(console.id, label, spec_->LookFor(console.id, console.group), console.cover_aspect,
                    std::move(it->second));
                by_system.erase(it);
            }
        }
        for (auto& [system, list] : by_system) {
            add(system, SystemShortName(system), spec_->LookFor(system, ""), 0.75f, std::move(list));
        }
    }

    void ChooseTab(std::size_t index, bool first) {
        if (index >= tabs_.size() || !carousel_ || (index == tab_ && !first)) {
            return;
        }
        tab_ = index;
        const TabData& tab = tabs_[index];
        std::size_t at = 0;
        for (std::size_t i = 0; i < tab.games.size(); ++i) {
            at = tab.games[i].id == Remembered().game ? i : at;
        }
        carousel_->SetGames(tab.games, at, tab.aspect, tab.color);
        for (std::size_t i = 0; i < tab_nodes_.size(); ++i) {
            tab_nodes_[i]->chosen = i == index;
        }
        Remembered().tab = tab.key;
        if (const library::Entry* game = carousel_->Selected()) {
            ShowGame(*game);
        }
    }

    void ShowGame(const library::Entry& game) {
        Remembered().game = game.id;
        name_->text = game.name;
        position_->text = TrF("home.position", carousel_->Index() + 1, tabs_[tab_].games.size());
        const ContinueCard* card = SelectedCard();
        const std::string moment = card != nullptr ? PathToUtf8(card->item.screenshot) : std::string();
        records_->text = card != nullptr ? ContinueLine(*card) : RecordsLine(game);
        moment_->path = moment;
        moment_->visible = !moment.empty();
        backdrop_.Set(moment.empty() ? game.cover : moment);
    }

    void DrawBackdrop(ui::Context& ctx, const gfx::Rect& area, const TabData* tab) {
        gfx::Canvas& canvas = *ctx.canvas;
        canvas.FillRect(area, spec_->background);
        if (spec_->backdrop == "cover") {
            backdrop_.Draw(ctx, area);
        } else if (spec_->backdrop == "console" && tab != nullptr && !tab->wallpaper.empty()) {
            if (const gfx::Texture wall = ctx.textures->Get(tab->wallpaper, 1920)) {
                canvas.DrawImage(area, wall, gfx::Color{255, 255, 255, 255}, 0,
                                 gfx::CoverUv(wall.width, wall.height, area));
            }
        }
        canvas.FillRect(area, color_.WithAlpha(spec_->tint));
        // Darker towards the bottom, where the name is: layers that each reach the bottom.
        const float band = area.h * 0.5f / GRADIENT_BANDS;
        for (int i = 0; i < GRADIENT_BANDS; ++i) {
            const float top = area.y + area.h * 0.5f + band * i;
            canvas.FillRect(gfx::Rect{area.x, top, area.w, area.Bottom() - top}, spec_->background.WithAlpha(0.045f));
        }
    }

    // The app's other screens (the rail's entries), in a row under the game: Down reaches it.
    static void AddScreens(ui::Node& root) {
        ui::Node* row = root.Add(ui::MakeBox(ui::Axis::Row, 10));
        row->justify = ui::Align::Center;
        row->chrome = true;
        for (const RailEntry& entry : Registry::Get().Rail()) {
            if (entry.route == "home") {
                continue;
            }
            auto* button = row->Emplace<ui::Button>(entry.label, [route = entry.route] {
                Svc().stage->PopToRoot();
                Svc().stage->Replace(route);
            });
            button->id = "iface.go." + entry.route;
            button->height = 40;
        }
    }

    void BuildEmpty(ui::Node& root) {
        ui::Node* box = root.Add(ui::MakeBox(ui::Axis::Column, 12));
        box->grow = 1;
        box->justify = ui::Align::Center;
        box->align = ui::Align::Center;
        box->Emplace<ui::Label>(Tr("home.empty_title"), theme_.text_title, gfx::FONT_BOLD);
        box->Emplace<ui::Label>(Tr("home.empty_message"))->tone = ui::Tone::Dim;
        if (Svc().stage->HasRoute("folders")) {
            auto* button = box->Emplace<ui::Button>(Tr("home.add_folders"), [] { Svc().stage->Push("folders"); });
            button->id = "home.add_folders";
            button->width = 260;
        }
    }

    std::shared_ptr<const InterfaceSpec> spec_;
    ui::Theme theme_;
    std::vector<TabData> tabs_;
    std::vector<Tab*> tab_nodes_;
    std::size_t tab_ = 0;
    CoverCarousel* carousel_ = nullptr;
    ui::Node* below_ = nullptr; // the game's name and the buttons under the carousel
    float below_height_ = 0;
    ui::Label* name_ = nullptr;
    ui::Label* records_ = nullptr;
    ui::Label* position_ = nullptr;
    ui::Label* clock_ = nullptr;
    MomentPicture* moment_ = nullptr;
    Backdrop backdrop_;
    std::vector<ContinueCard> cards_;
    std::string signature_;
    double polled_ = 0;
    gfx::Color color_;
    std::uint64_t revision_ = 0;
};

} // namespace

std::unique_ptr<ui::Page> MakeCarouselHome(std::shared_ptr<const InterfaceSpec> spec) {
    return std::make_unique<CarouselHome>(std::move(spec));
}

} // namespace den::app
