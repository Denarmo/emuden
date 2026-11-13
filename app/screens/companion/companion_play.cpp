// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen while a game runs on the main one: quick settings by touch. The game
// comes first, a card with its cover, its play time and the controls used most (pause,
// rewind, fast-forward, exit); everything else is one step in: the states with their
// pictures, the screen size, the picture preset and "Swap screens" (the game on this display,
// these settings on the other), the core options and buttons pages, the apps, the volumes.
// With an app open beside the game and these settings pulled over it, the first row goes
// back to that app or closes it.
// Route "companion.play" on the companion stage.

#include <algorithm>
#include <cmath>
#include <format>

#include "../apps/app_grid.h"
#include "../apps/apps_catalog.h"
#include "../apps/beside.h"
#include "../home/game_widgets.h"
#include "../home/games.h"
#include "../player/picture_choice.h"
#include "../player/player_screen.h"
#include "../player/volume_rows.h"
#include "companion_cells.h"
#include "companion_page.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/app/pages.h"
#include "den/base/files.h"
#include "den/platform/audio.h"
#include "second_screen.h"

namespace den::app {

namespace {

constexpr float BUTTON_HEIGHT = 56;
constexpr float COVER_W = 112, COVER_H = 150;
constexpr float ROW_HEIGHT = 64;
constexpr float GAP = 12;
constexpr int SLOT_COLUMNS = 4, PICTURE_COLUMNS = 4, APP_COLUMNS = 3;
constexpr std::size_t RECENT_APPS = 6;

bool Playing(const player::Snapshot& snapshot) {
    return snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
}

// The views of the page: the main one, and the sections it opens.
enum class View { Main, States, Screen, Controls, Apps };

class CompanionPlay final : public CompanionPage {
public:
    void Build() override {
        Services& svc = Svc();
        const player::Snapshot snapshot = svc.player->GetSnapshot();
        if (snapshot.game_id != game_id_) {
            view_ = View::Main; // another game starts on the main view
        }
        key_ = Key(snapshot);
        game_id_ = snapshot.game_id;
        const auto game = svc.library->Find(snapshot.game_id);
        system_ = game ? game->system : std::string();

        // One list, so a display too low for every row scrolls; a rebuilt page keeps every
        // scrolling node with an id where it was (ui::Page).
        // The main view scrolls when the display is too low for every row; a section fills it.
        Root().SetPadding(24, 16);
        ui::Node& root = *Root().Add(view_ == View::Main ? ui::MakeList(10) : ui::MakeBox(ui::Axis::Column, 12));
        root.id = view_ == View::Main ? "companion.page" : "companion.section";
        root.grow = 1;

        if (const system::AppBeside& beside = Beside();
            beside.GetStage() == system::AppBeside::Stage::SettingsOver) {
            // These settings lie over an app the user opened beside the game: the way back
            // to it comes first.
            ui::Node* over = root.Add(ui::MakeBox(ui::Axis::Row, 10));
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

        // The game's own volume, and the system's while an app plays beside the game.
        other_volume_ = Beside().GetStage() != system::AppBeside::Stage::None;
        switch (view_) {
        case View::Main:
            BuildMain(root, snapshot, game ? &*game : nullptr);
            break;
        case View::States:
            Heading(root, "States");
            BuildStates(root);
            break;
        case View::Screen:
            Heading(root, "Screen and picture");
            BuildScreen(root, snapshot);
            break;
        case View::Controls:
            Heading(root, "Controls and core");
            BuildControls(root);
            break;
        case View::Apps:
            Heading(root, "Apps");
            BuildApps(root);
            break;
        }
    }

    void Update(ui::Context&) override {
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
        if (Key(snapshot) != key_) {
            Rebuild();
        }
    }

private:
    void Open(View view) {
        view_ = view;
        Rebuild();
    }

    // A section's title with the way back to the main view.
    void Heading(ui::Node& root, const char* title) {
        ui::Node* line = root.Add(ui::MakeBox(ui::Axis::Row, 12));
        line->align = ui::Align::Center;
        auto* back = line->Emplace<ui::Button>("Back", [this] { Open(View::Main); });
        back->id = "companion.section_back";
        back->height = BUTTON_HEIGHT;
        back->SetPadding(22, 0);
        auto* label = line->Emplace<ui::Label>(title, Svc().theme->text_heading, gfx::FONT_BOLD);
        label->grow = 1;
    }

    void BuildMain(ui::Node& root, const player::Snapshot& snapshot, const library::Entry* game) {
        Services& svc = Svc();
        // The card: the cover, what is playing, and its controls.
        ui::Node* card = root.Add(ui::MakeBox(ui::Axis::Row, 18));
        card->pad_bottom = 6;
        auto* cover = card->Emplace<CoverView>(game ? game->name : std::string("Game"),
                                               game ? game->cover : std::string(), COVER_W, COVER_H);
        cover->overlay_title = game && game->placeholder_cover;
        ui::Node* info = card->Add(ui::MakeBox(ui::Axis::Column, 6));
        info->grow = 1;
        info->justify = ui::Align::Center;
        info->Emplace<ui::Label>(game ? game->name : std::string("Game"), svc.theme->text_heading,
                                 gfx::FONT_BOLD);
        if (game) {
            std::string line = SystemName(game->system);
            if (game->play_seconds > 0) {
                line += " · " + PlayTimeText(game->play_seconds) + " played";
            }
            info->Emplace<ui::Label>(line)->tone = ui::Tone::Dim;
        }
        info->Emplace<ui::Label>(snapshot.state == player::State::Paused ? "Paused"
                                 : snapshot.rewinding                    ? "Rewinding"
                                 : snapshot.fast_forward                 ? "Fast-forward"
                                                                         : "Playing",
                                 svc.theme->text_small, gfx::FONT_BOLD)
            ->tone = ui::Tone::Dim;

        ui::Node* controls = info->Add(ui::MakeBox(ui::Axis::Row, 10));
        controls->pad_top = 6;
        const auto control = [&](const char* id, const std::string& text, bool selected,
                                 std::function<void()> fn) {
            auto* button = controls->Emplace<ui::Button>(text, std::move(fn));
            button->id = id;
            button->height = BUTTON_HEIGHT;
            button->grow = 1;
            button->selected = selected;
        };
        control("companion.menu", snapshot.state == player::State::Paused ? "Resume" : "Pause", false,
                [] { Svc().input->Inject(input::Event::MakeNav(input::Nav::Menu)); });
        if (snapshot.can_rewind) {
            control("companion.rewind", "Rewind", snapshot.rewinding, [this] {
                rewind_ = !Svc().player->GetSnapshot().rewinding;
                fast_ = false;
                Svc().player->SetFastForward(false);
                Svc().player->SetRewind(rewind_);
            });
        }
        control("companion.fast", "Fast-forward", snapshot.fast_forward, [this] {
            fast_ = !Svc().player->GetSnapshot().fast_forward;
            rewind_ = false;
            Svc().player->SetRewind(false);
            Svc().player->SetFastForward(fast_);
        });
        control("companion.exit", "Exit", false, [] {
            // The player screen saves the game and books its play time.
            if (PlayerScreen* screen = PlayerScreen::Current()) {
                screen->Exit();
            }
        });

        // The sections, each one step in, with what it holds now at the right.
        std::vector<ui::RowSpec> rows;
        const PlayerScreen* screen = PlayerScreen::Current();
        if (snapshot.can_save_states) {
            int used = 0;
            for (int slot = 0; slot < player::STATE_SLOTS; ++slot) {
                used += svc.player->HasState(slot) ? 1 : 0;
            }
            rows.push_back(ValueRow(
                "companion.section.states", "States",
                [used] { return used ? std::format("{} saved", used) : std::string("None saved"); },
                [this] { Open(View::States); }));
        }
        std::string picture;
        for (const player::PicturePreset& preset : player::PicturePresets()) {
            picture = preset.id == snapshot.picture ? preset.name : picture;
        }
        std::string size;
        if (screen) {
            const auto& names = ScreenSizeNames();
            const auto index = static_cast<std::size_t>(std::max(0, screen->ScreenSizeIndex()));
            size = index < names.size() ? names[index] : std::string();
        }
        const std::string screen_value = size.empty() || picture.empty() ? size + picture : size + " · " + picture;
        rows.push_back(ValueRow("companion.section.screen", "Screen and picture",
                                [screen_value] { return screen_value; }, [this] { Open(View::Screen); }));
        if (screen) {
            rows.push_back(ValueRow("companion.section.controls", "Controls and core",
                                    [] { return std::string("Core options, buttons"); },
                                    [this] { Open(View::Controls); }));
            if (AppsAvailable()) {
                const std::string beside = other_volume_ ? Beside().App().label : std::string();
                rows.push_back(ValueRow("companion.section.apps", "Apps", [beside] { return beside; }, [this] {
                    // Without apps opened before, the section would only hold "All apps".
                    if (Apps().Recent(RECENT_APPS).empty()) {
                        GetStage().Push("companion.apps");
                    } else {
                        Open(View::Apps);
                    }
                }));
            }
        }
        for (ui::RowSpec& row : VolumeRows("companion", other_volume_)) {
            rows.push_back(std::move(row));
        }
        AddRows(&root, std::move(rows), ROW_HEIGHT);
    }

    void BuildStates(ui::Node& root) {
        Services& svc = Svc();
        ui::Node* modes = root.Add(ui::MakeBox(ui::Axis::Row, GAP));
        modes->height = BUTTON_HEIGHT;
        const auto mode = [&](const char* id, const std::string& text, bool save) {
            auto* card = modes->Emplace<CompanionCard>(text, [this, save] {
                save_mode_ = save;
                Rebuild();
            });
            card->id = id;
            card->grow = 1;
            card->width = 0;
            card->selected = save_mode_ == save;
        };
        mode("companion.load_mode", "Load state", false);
        mode("companion.save_mode", "Save state", true);

        CompanionGrid(root, player::STATE_SLOTS, SLOT_COLUMNS, GAP, [&](ui::Node& row, int slot) -> ui::Node* {
            const bool used = svc.player->HasState(slot);
            auto* cell = row.Emplace<CompanionSlot>(
                std::format("Slot {}", slot + 1),
                used ? PathToUtf8(svc.player->StateThumbnail(slot)) : std::string());
            cell->id = std::format("companion.slot{}", slot);
            cell->on_activate = [this, slot] {
                if (save_mode_) {
                    Save(slot);
                } else {
                    Load(slot);
                }
            };
            return cell;
        });
    }

    static void Caption(ui::Node& root, const char* text) {
        auto* label = root.Emplace<ui::Label>(text, Svc().theme->text_small, gfx::FONT_BOLD);
        label->tone = ui::Tone::Dim;
    }

    void BuildScreen(ui::Node& root, const player::Snapshot& snapshot) {
        PlayerScreen* screen = PlayerScreen::Current();
        if (screen) {
            Caption(root, "Screen size");
            const auto& names = ScreenSizeNames();
            const int count = static_cast<int>(names.size());
            const int current = screen->ScreenSizeIndex();
            CompanionGrid(root, count, count, GAP, [&](ui::Node& row, int index) -> ui::Node* {
                auto* card = row.Emplace<CompanionCard>(names[static_cast<std::size_t>(index)], [index] {
                    if (PlayerScreen* now = PlayerScreen::Current()) {
                        now->SetScreenSize(index);
                    }
                });
                card->id = std::format("companion.size{}", index);
                card->selected = index == current;
                return card;
            });
        }
        const auto& presets = player::PicturePresets();
        if (presets.size() > 1) {
            Caption(root, "Picture");
            CompanionGrid(root, static_cast<int>(presets.size()), PICTURE_COLUMNS, GAP,
                          [&](ui::Node& row, int index) -> ui::Node* {
                const player::PicturePreset& preset = presets[static_cast<std::size_t>(index)];
                auto* card = row.Emplace<CompanionCard>(preset.name, [this, id = preset.id] {
                    // Kept where the pause menu keeps it, for the same games.
                    if (const auto game = Svc().library->Find(game_id_)) {
                        SavePicture(*game, PictureScopeOf(*game), id);
                    }
                    Svc().player->SetPicture(id);
                });
                card->id = "companion.picture." + preset.id;
                card->selected = preset.id == snapshot.picture;
                return card;
            });
        }
        if (screen) {
            auto* swap = root.Emplace<CompanionCard>(
                "Swap screens",
                [] {
                    if (PlayerScreen* now = PlayerScreen::Current()) {
                        now->SwapDisplays();
                    }
                },
                "The game on this screen, these settings on the other");
            swap->id = "companion.swap";
            swap->grow = 1;
        }
    }

    void BuildControls(ui::Node& root) {
        const auto page = [](const char* name) {
            return [name] {
                if (PlayerScreen* now = PlayerScreen::Current()) {
                    now->OpenPage(name);
                }
            };
        };
        ui::Node* row = root.Add(ui::MakeBox(ui::Axis::Row, GAP));
        row->grow = 1;
        auto* options = row->Emplace<CompanionCard>("Core options", page("options"),
                                                    "How the system is emulated, for this game");
        options->id = "companion.options";
        options->grow = 1;
        options->width = 0;
        auto* buttons = row->Emplace<CompanionCard>("Buttons", page("buttons"), "Which button does what");
        buttons->id = "companion.buttons";
        buttons->grow = 1;
        buttons->width = 0;
    }

    void BuildApps(ui::Node& root) {
        // The apps opened last, to open one again beside the game with one tap.
        const std::vector<system::AppItem> recent = Apps().Recent(RECENT_APPS);
        if (!recent.empty()) {
            Caption(root, "Opened last");
            CompanionGrid(root, static_cast<int>(recent.size()), APP_COLUMNS, GAP,
                          [&](ui::Node& row, int index) -> ui::Node* {
                const system::AppItem& app = recent[static_cast<std::size_t>(index)];
                auto* card = row.Emplace<CompanionCard>(
                    app.label, [this, id = app.id] { OpenApp(id, true, GetStage()); }, std::string(), app.icon);
                card->id = "companion.recent." + app.id;
                return card;
            });
        }
        auto* all = root.Emplace<CompanionCard>("All apps", [this] { GetStage().Push("companion.apps"); },
                                                "Another app on this screen while the game goes on");
        all->id = "companion.apps";
        all->grow = 1;
    }

    // What the page shows; it is built again when this changes.
    std::string Key(const player::Snapshot& snapshot) const {
        int slots = 0;
        for (int slot = 0; slot < player::STATE_SLOTS; ++slot) {
            slots |= Svc().player->HasState(slot) ? 1 << slot : 0;
        }
        const PlayerScreen* screen = PlayerScreen::Current();
        // The volumes are not in here: their sliders show the value as it changes, and a
        // page built again under a finger would end the drag.
        const system::AppBeside& beside = Beside();
        return std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", snapshot.game_id,
                           static_cast<int>(snapshot.state), snapshot.fast_forward, snapshot.rewinding,
                           snapshot.can_rewind, snapshot.can_save_states, snapshot.picture, slots,
                           screen ? screen->ScreenSizeIndex() : -1, static_cast<int>(beside.GetStage()),
                           beside.App().id, static_cast<int>(beside.GameScreen()), AppsRevision(),
                           static_cast<int>(view_));
    }

    void Save(int slot) {
        const auto save = [this, slot] {
            const Status saved = Svc().player->SaveState(slot);
            if (!saved) {
                GetStage().Toast(saved.error.empty() ? "The state could not be saved" : saved.error, 5.0f);
                return;
            }
            // The picture of the slot changed under the same file name.
            Svc().textures->Forget(PathToUtf8(Svc().player->StateThumbnail(slot)));
            GetStage().Toast(std::format("Saved to slot {}", slot + 1));
            Rebuild();
        };
        if (Svc().player->HasState(slot)) {
            GetStage().Confirm(std::format("Replace slot {}?", slot + 1), "The state saved there is overwritten.",
                               "Replace", save);
        } else {
            save();
        }
    }

    void Load(int slot) {
        if (!Svc().player->HasState(slot)) {
            GetStage().Toast(std::format("Slot {} is empty", slot + 1));
            return;
        }
        const Status loaded = Svc().player->LoadState(slot);
        GetStage().Toast(loaded ? std::format("Loaded slot {}", slot + 1)
                         : loaded.error.empty() ? std::string("The state could not be loaded")
                                                : loaded.error);
    }

    std::string key_, game_id_, system_;
    View view_ = View::Main;
    bool save_mode_ = false;
    bool fast_ = false, rewind_ = false;
    bool other_volume_ = false;
};

} // namespace

DEN_REGISTER_SCREEN("companion.play", CompanionPlay);

} // namespace den::app
