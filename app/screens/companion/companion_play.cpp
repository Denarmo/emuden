// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen while a game runs on the main one: quick settings by touch. Save and
// load states with their pictures, fast-forward, rewind, the screen size, the picture
// preset, the volumes, the pause menu with its core options and buttons pages, the way out,
// and "Swap screens", which puts the game on this display and these settings on the other.
// With an app open beside the game and these settings pulled over it, the first row goes
// back to that app or closes it; the apps opened last are one tap away.
// Route "companion.play" on the companion stage.

#include <algorithm>
#include <cmath>
#include <format>

#include "../apps/app_grid.h"
#include "../apps/apps_catalog.h"
#include "../apps/beside.h"
#include "../home/games.h"
#include "../player/picture_choice.h"
#include "../player/player_screen.h"
#include "../player/volume_rows.h"
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
constexpr float SLOT_W = 150, SLOT_H = 112;
constexpr float CHOICE_HEIGHT = 48, CHOICE_LABEL = 104;
constexpr float VOLUME_HEIGHT = 64;
constexpr std::size_t RECENT_APPS = 3;

bool Playing(const player::Snapshot& snapshot) {
    return snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
}

class CompanionPlay final : public CompanionPage {
public:
    void Build() override {
        Services& svc = Svc();
        const player::Snapshot snapshot = svc.player->GetSnapshot();
        key_ = Key(snapshot);
        game_id_ = snapshot.game_id;
        const auto game = svc.library->Find(snapshot.game_id);
        system_ = game ? game->system : std::string();

        // One list, so a display too low for every row scrolls; a rebuilt page keeps every
        // scrolling node with an id where it was (ui::Page).
        Root().SetPadding(24, 16);
        ui::Node& root = *Root().Add(ui::MakeList(10));
        root.id = "companion.page";

        ui::Node* header = root.Add(ui::MakeBox(ui::Axis::Row, 12));
        header->align = ui::Align::Center;
        auto* name = header->Emplace<ui::Label>(game ? game->name : std::string("Game"),
                                                svc.theme->text_heading, gfx::FONT_BOLD);
        name->grow = 1;
        auto* status = header->Emplace<ui::Label>(
            snapshot.state == player::State::Paused ? "Paused"
            : snapshot.rewinding                    ? "Rewinding"
            : snapshot.fast_forward                 ? "Fast-forward"
                                                    : "Playing");
        status->tone = ui::Tone::Dim;

        if (const system::AppBeside& beside = Beside();
            beside.GetStage() == system::AppBeside::Stage::SettingsOver) {
            // These settings lie over an app the user opened beside the game: the way back
            // to it comes first.
            ui::Node* over = root.Add(ui::MakeBox(ui::Axis::Row, 10));
            auto* back = over->Emplace<AppChip>("Back to " + beside.App().label, beside.App().icon);
            back->id = "companion.back_to_app";
            back->strong = true;
            back->grow = 1;
            back->height = 64;
            back->on_activate = [] { BesideBackToApp(); };
            auto* close = over->Emplace<ui::Button>("Close " + beside.App().label, [] { BesideClose(); });
            close->id = "companion.close_app";
            close->height = 64;
            close->SetPadding(18, 0);
        }

        ui::Node* controls = root.Add(ui::MakeBox(ui::Axis::Row, 10));
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
        control("companion.fast", "Fast-forward", snapshot.fast_forward, [this] {
            fast_ = !Svc().player->GetSnapshot().fast_forward;
            rewind_ = false;
            Svc().player->SetRewind(false);
            Svc().player->SetFastForward(fast_);
        });
        if (snapshot.can_rewind) {
            control("companion.rewind", "Rewind", snapshot.rewinding, [this] {
                rewind_ = !Svc().player->GetSnapshot().rewinding;
                fast_ = false;
                Svc().player->SetFastForward(false);
                Svc().player->SetRewind(rewind_);
            });
        }
        control("companion.exit", "Exit", false, [] {
            // The player screen saves the game and books its play time.
            if (PlayerScreen* screen = PlayerScreen::Current()) {
                screen->Exit();
            }
        });

        PlayerScreen* screen = PlayerScreen::Current();
        if (screen) {
            ui::Node* tools = root.Add(ui::MakeBox(ui::Axis::Row, 10));
            const auto tool = [&](const char* id, const std::string& text, std::function<void()> fn) {
                auto* button = tools->Emplace<ui::Button>(text, std::move(fn));
                button->id = id;
                button->height = CHOICE_HEIGHT;
                button->grow = 1;
            };
            tool("companion.swap", "Swap screens", [] {
                if (PlayerScreen* now = PlayerScreen::Current()) {
                    now->SwapDisplays();
                }
            });
            tool("companion.options", "Core options", [] {
                if (PlayerScreen* now = PlayerScreen::Current()) {
                    now->OpenPage("options");
                }
            });
            tool("companion.buttons", "Buttons", [] {
                if (PlayerScreen* now = PlayerScreen::Current()) {
                    now->OpenPage("buttons");
                }
            });
            if (AppsAvailable()) {
                // Another app on this display while the game goes on on the other.
                tool("companion.apps", "Apps", [this] { GetStage().Push("companion.apps"); });
            }
        }

        if (snapshot.can_save_states) {
            ui::Node* modes = root.Add(ui::MakeBox(ui::Axis::Row, 10));
            const auto mode = [&](const char* id, const std::string& text, bool save) {
                auto* button = modes->Emplace<ui::Button>(text, [this, save] {
                    save_mode_ = save;
                    Rebuild();
                });
                button->id = id;
                button->grow = 1;
                button->selected = save_mode_ == save;
            };
            mode("companion.load_mode", "Load state", false);
            mode("companion.save_mode", "Save state", true);

            ui::Node* slots = root.Add(ui::MakeShelf(12));
            slots->height = SLOT_H + 36;
            slots->id = "companion.slots";
            for (int slot = 0; slot < player::STATE_SLOTS; ++slot) {
                const bool used = svc.player->HasState(slot);
                const std::string image = used ? PathToUtf8(svc.player->StateThumbnail(slot)) : std::string();
                auto* tile = slots->Emplace<ui::Tile>(
                    std::format("Slot {}", slot + 1), image, SLOT_W, SLOT_H);
                tile->id = std::format("companion.slot{}", slot);
                tile->on_activate = [this, slot] {
                    if (save_mode_) {
                        Save(slot);
                    } else {
                        Load(slot);
                    }
                };
            }
        }

        // A row of choices beside its name.
        const auto choices = [&](const char* id, const char* title) {
            ui::Node* line = root.Add(ui::MakeBox(ui::Axis::Row, 10));
            line->align = ui::Align::Center;
            auto* label = line->Emplace<ui::Label>(title, svc.theme->text_small, gfx::FONT_BOLD);
            label->tone = ui::Tone::Dim;
            label->width = CHOICE_LABEL;
            ui::Node* shelf = line->Add(ui::MakeShelf(10));
            shelf->id = id;
            shelf->height = CHOICE_HEIGHT;
            shelf->grow = 1;
            return shelf;
        };
        if (screen) {
            ui::Node* sizes = choices("companion.sizes", "Screen size");
            const auto& names = ScreenSizeNames();
            for (int index = 0; index < static_cast<int>(names.size()); ++index) {
                auto* button = sizes->Emplace<ui::Button>(names[static_cast<std::size_t>(index)], [index] {
                    if (PlayerScreen* now = PlayerScreen::Current()) {
                        now->SetScreenSize(index);
                    }
                });
                button->id = std::format("companion.size{}", index);
                button->height = CHOICE_HEIGHT;
                button->selected = index == screen->ScreenSizeIndex();
            }
        }
        const auto& presets = player::PicturePresets();
        if (presets.size() > 1) {
            ui::Node* pictures = choices("companion.pictures", "Picture");
            for (const player::PicturePreset& preset : presets) {
                auto* button = pictures->Emplace<ui::Button>(preset.name, [this, id = preset.id] {
                    // Kept where the pause menu keeps it, for the same games.
                    if (const auto game = Svc().library->Find(game_id_)) {
                        SavePicture(*game, PictureScopeOf(*game), id);
                    }
                    Svc().player->SetPicture(id);
                });
                button->id = "companion.picture." + preset.id;
                button->height = CHOICE_HEIGHT;
                button->selected = preset.id == snapshot.picture;
            }
        }

        if (screen && AppsAvailable()) {
            // The apps opened last, to open one again beside the game with one tap.
            const std::vector<system::AppItem> recent = Apps().Recent(RECENT_APPS);
            if (!recent.empty()) {
                ui::Node* apps = choices("companion.recent_apps", "Recent apps");
                for (const system::AppItem& app : recent) {
                    auto* chip = apps->Emplace<AppChip>(app.label, app.icon);
                    chip->id = "companion.recent." + app.id;
                    chip->height = CHOICE_HEIGHT;
                    chip->on_activate = [this, id = app.id] { OpenApp(id, true, GetStage()); };
                }
            }
        }

        // The game's own volume, and the system's while an app plays beside the game.
        other_volume_ = Beside().GetStage() != system::AppBeside::Stage::None;
        AddRows(&root, VolumeRows("companion", other_volume_), VOLUME_HEIGHT);
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
        return std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", snapshot.game_id,
                           static_cast<int>(snapshot.state), snapshot.fast_forward, snapshot.rewinding,
                           snapshot.can_rewind, snapshot.can_save_states, snapshot.picture, slots,
                           screen ? screen->ScreenSizeIndex() : -1, static_cast<int>(beside.GetStage()),
                           beside.App().id, static_cast<int>(beside.GameScreen()), AppsRevision());
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
    bool save_mode_ = false;
    bool fast_ = false, rewind_ = false;
    bool other_volume_ = false;
};

} // namespace

DEN_REGISTER_SCREEN("companion.play", CompanionPlay);

} // namespace den::app
