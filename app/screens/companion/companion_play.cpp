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
#include "companion_pictures.h"
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
constexpr int SLOT_COLUMNS = 4;
constexpr float PICTURE_W = 300; // a card of the picture carousel
constexpr const char* FPS_NAMES[] = {"FPS: Off", "FPS: On", "FPS: Graph"};

bool Playing(const player::Snapshot& snapshot) {
    return snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
}

// The views of the page: the main one, and the sections it opens.
enum class View { Main, States, Screen, Picture, Controls };

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
        KeepPicturePreviews(game_id_);
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
        case View::Picture:
            Heading(root, PictureName(snapshot.picture, system_), View::Screen);
            BuildPicture(root);
            break;
        case View::Controls:
            Heading(root, "Controls and core");
            BuildControls(root);
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
            return;
        }
        if (ui::Node* shelf = reveal_index_ >= 0 ? Root().FindById("companion.pictures") : nullptr;
            shelf && shelf->rect.w > 0) {
            const float start = static_cast<float>(reveal_index_) * (PICTURE_W + GAP);
            shelf->scroller.Jump(std::clamp(start - (shelf->rect.w - PICTURE_W) * 0.5f, 0.0f, shelf->scroller.Max()));
            reveal_index_ = -1;
        }
    }

private:
    void Open(View view) {
        if (view_ == View::Picture) {
            // Its sliders may have moved.
            ForgetPicturePreview(Svc().player->GetSnapshot().picture);
        }
        reveal_picture_ = view == View::Screen && view_ != View::Screen;
        if (view == View::Screen && view_ == View::Main) {
            // The previews show the game as it is now.
            ResetPicturePreviews();
        }
        view_ = view;
        Rebuild();
    }

    // A section's title with the way back to the main view.
    void Heading(ui::Node& root, const std::string& title, View back_to = View::Main) {
        ui::Node* line = root.Add(ui::MakeBox(ui::Axis::Row, 12));
        line->align = ui::Align::Center;
        auto* back = line->Emplace<ui::Button>("Back", [this, back_to] { Open(back_to); });
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
        if (snapshot.can_fast_forward) {
            control("companion.fast", "Fast-forward", snapshot.fast_forward, [this] {
                fast_ = !Svc().player->GetSnapshot().fast_forward;
                rewind_ = false;
                Svc().player->SetRewind(false);
                Svc().player->SetFastForward(fast_);
            });
        }
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
        // An engine app draws the game and reads the pads itself.
        if (snapshot.can_picture) {
            rows.push_back(ValueRow("companion.section.screen", "Screen and picture",
                                    [screen_value] { return screen_value; }, [this] { Open(View::Screen); }));
        }
        if (screen) {
            if (!snapshot.external) {
                rows.push_back(ActionRow("companion.section.controls", "Controls and core",
                                         [this] { Open(View::Controls); }));
            }
            if (AppsAvailable()) {
                const std::string beside = other_volume_ ? Beside().App().label : std::string();
                rows.push_back(ValueRow("companion.section.apps", "Apps", [beside] { return beside; }, [this] {
                    // The list has the apps opened last at its top.
                    GetStage().Push("companion.apps");
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
            // The carousel below takes the room.
            root.Children().back()->grow = 0;
            root.Children().back()->height = BUTTON_HEIGHT;
        }
        const auto& presets = player::PicturePresets();
        if (presets.size() > 1) {
            Caption(root, "Picture");
            // A carousel: the game through each preset, swiped sideways.
            ui::Node* shelf = root.Add(ui::MakeShelf(GAP));
            shelf->id = "companion.pictures";
            shelf->grow = 1;
            shelf->align = ui::Align::Stretch;
            int in_use = -1;
            for (const player::PicturePreset& preset : presets) {
                const bool current = preset.id == snapshot.picture;
                auto* card = shelf->Emplace<PictureCard>(preset.id, preset.name, current, [this, id = preset.id] {
                    // The picture shown now opens its settings.
                    if (Svc().player->GetSnapshot().picture == id) {
                        if (!Svc().player->PictureParams().empty()) {
                            Open(View::Picture);
                        }
                        return;
                    }
                    // Kept where the pause menu keeps it, for the same games.
                    if (const auto game = Svc().library->Find(game_id_)) {
                        SavePicture(*game, PictureScopeOf(*game), id);
                    }
                    Svc().player->SetPicture(id);
                });
                card->id = "companion.picture." + preset.id;
                card->width = PICTURE_W;
                in_use = current ? static_cast<int>(&preset - presets.data()) : in_use;
            }
            // Opened from the main view: the carousel starts at the picture in use, once it
            // is laid out (Update).
            reveal_index_ = reveal_picture_ ? in_use : -1;
            reveal_picture_ = false;
        }
        if (screen) {
            ui::Node* row = root.Add(ui::MakeBox(ui::Axis::Row, GAP));
            row->height = BUTTON_HEIGHT;
            auto* swap = row->Emplace<CompanionCard>("Swap screens", [] {
                if (PlayerScreen* now = PlayerScreen::Current()) {
                    now->SwapDisplays();
                }
            });
            swap->id = "companion.swap";
            swap->grow = 1;
            swap->width = 0;
            auto* frame = row->Emplace<CompanionCard>("New preview frame", [] { ResetPicturePreviews(); });
            frame->id = "companion.preview_frame";
            frame->grow = 1;
            frame->width = 0;
            // Off, the frame rate, the frame rate with a graph of the frame times.
            const int fps = screen->FpsMode();
            auto* counter = row->Emplace<CompanionCard>(FPS_NAMES[fps], [fps] {
                if (PlayerScreen* now = PlayerScreen::Current()) {
                    now->SetFpsMode((fps + 1) % PlayerScreen::FPS_MODES);
                }
            });
            counter->id = "companion.fps";
            counter->grow = 1;
            counter->width = 0;
            counter->selected = fps != 0;
            if (screen->CanWidescreen()) {
                const bool wide = screen->Widescreen();
                auto* card = row->Emplace<CompanionCard>("Widescreen", [this, wide] {
                    PlayerScreen* now = PlayerScreen::Current();
                    if (now && !now->WidescreenRestarts()) {
                        now->SetWidescreen(!wide);
                    } else if (now) {
                        GetStage().Confirm(WIDESCREEN_TITLE, WIDESCREEN_TEXT, "Start again", [wide] {
                            if (PlayerScreen* still = PlayerScreen::Current()) {
                                still->SetWidescreen(!wide);
                            }
                        });
                    }
                });
                card->id = "companion.widescreen";
                card->grow = 1;
                card->width = 0;
                card->selected = wide;
            }
        }
    }

    // The sliders of the picture shown now, as the pause menu has them.
    void BuildPicture(ui::Node& root) {
        if (const auto& looks = PictureLooks(); !looks.empty()) {
            ui::Node* row = root.Add(ui::MakeBox(ui::Axis::Row, GAP));
            row->height = BUTTON_HEIGHT;
            const int current = CurrentLook();
            for (std::size_t i = 0; i < looks.size(); ++i) {
                auto* card = row->Emplace<CompanionCard>(looks[i].name, [this, i] {
                    ApplyLook(static_cast<int>(i));
                    Rebuild();
                });
                card->id = std::format("companion.look{}", i);
                card->grow = 1;
                card->width = 0;
                card->selected = current == static_cast<int>(i);
            }
        }
        ui::Node* list = root.Add(ui::MakeList(10));
        list->id = "companion.picture_settings";
        list->grow = 1;
        std::vector<ui::RowSpec> rows;
        for (const player::PictureParam& param : Svc().player->PictureParams()) {
            const std::string id = param.id;
            rows.push_back(SliderRow(
                "companion.adjust." + id, param.name, param.min, param.max, param.step,
                [id] {
                    for (const player::PictureParam& now : Svc().player->PictureParams()) {
                        if (now.id == id) {
                            return now.value;
                        }
                    }
                    return 0.0f;
                },
                [id](float value) { Svc().player->SetPictureParam(id, value); },
                [labels = param.labels, min = param.min, step = param.step](float value) {
                    const auto index = static_cast<std::size_t>(std::max(0.0f, std::round((value - min) / step)));
                    if (index < labels.size()) {
                        return labels[index];
                    }
                    return step >= 1.0f ? std::format("{:.0f}", value)
                                        : std::format("{:.0f}%", std::round(value * 100.0f));
                }));
        }
        rows.push_back(ActionRow("companion.adjust.reset", "Reset", [this] {
            Svc().player->ResetPictureParams();
            Rebuild();
        }));
        AddRows(list, std::move(rows), ROW_HEIGHT);
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
        auto* options = row->Emplace<CompanionCard>("Core options", page("options"));
        options->id = "companion.options";
        options->grow = 1;
        options->width = 0;
        auto* buttons = row->Emplace<CompanionCard>("Buttons", page("buttons"));
        buttons->id = "companion.buttons";
        buttons->grow = 1;
        buttons->width = 0;
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
        return std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", snapshot.game_id,
                           static_cast<int>(snapshot.state), snapshot.fast_forward, snapshot.rewinding,
                           snapshot.can_rewind, snapshot.can_save_states, snapshot.picture, slots,
                           screen ? screen->ScreenSizeIndex() : -1, static_cast<int>(beside.GetStage()),
                           beside.App().id, static_cast<int>(beside.GameScreen()), AppsRevision(),
                           static_cast<int>(view_), screen ? screen->FpsMode() : -1,
                           screen && screen->Widescreen());
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
    bool reveal_picture_ = false; // the Screen view was just opened
    int reveal_index_ = -1;       // the carousel card to bring into view
    bool fast_ = false, rewind_ = false;
    bool other_volume_ = false;
};

} // namespace

DEN_REGISTER_SCREEN("companion.play", CompanionPlay);

} // namespace den::app
