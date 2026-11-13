// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen while a game runs on the main one: quick settings by touch. Save and
// load states with their pictures, fast-forward, rewind, the picture preset, the volume,
// the pause menu and the way out. Route "companion.play" on the companion stage.

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>

#include "../home/games.h"
#include "../player/picture_choice.h"
#include "companion_page.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "second_screen.h"

namespace den::app {

namespace {

constexpr float BUTTON_HEIGHT = 56;
constexpr float SLOT_W = 150, SLOT_H = 112;
constexpr const char* VOLUME_KEY = "player.volume";
constexpr const char* SLOTS_ID = "companion.slots";
constexpr const char* PICTURES_ID = "companion.pictures";
constexpr double EXIT_WAIT = 0.8; // seconds given to the pause menu to show its exit row

bool Playing(const player::Snapshot& snapshot) {
    return snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
}

class CompanionPlay final : public CompanionPage {
public:
    CompanionPlay() : started_unix_(static_cast<std::int64_t>(std::time(nullptr))), started_(pal::NowSeconds()) {}

    void Build() override {
        Services& svc = Svc();
        const player::Snapshot snapshot = svc.player->GetSnapshot();
        key_ = Key(snapshot);
        game_id_ = snapshot.game_id;
        const auto game = svc.library->Find(snapshot.game_id);
        system_ = game ? game->system : std::string();

        ui::Node& root = Root();
        root.axis = ui::Axis::Column;
        root.gap = 12;
        root.SetPadding(24, 20);

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
        control("companion.exit", "Exit", false, [this] { Exit(); });

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
            slots->id = SLOTS_ID;
            slots->scroller.Jump(slots_at_);
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

        const auto& presets = player::PicturePresets();
        if (presets.size() > 1) {
            auto* heading = root.Emplace<ui::Label>("Picture", svc.theme->text_small, gfx::FONT_BOLD);
            heading->tone = ui::Tone::Dim;
            ui::Node* pictures = root.Add(ui::MakeShelf(10));
            pictures->height = 48;
            pictures->id = PICTURES_ID;
            pictures->scroller.Jump(pictures_at_);
            for (const player::PicturePreset& preset : presets) {
                auto* button = pictures->Emplace<ui::Button>(preset.name, [this, id = preset.id] {
                    // Kept where the pause menu keeps it, for the same games.
                    if (const auto game = Svc().library->Find(game_id_)) {
                        SavePicture(*game, PictureScopeOf(*game), id);
                    }
                    Svc().player->SetPicture(id);
                });
                button->id = "companion.picture." + preset.id;
                button->selected = preset.id == snapshot.picture;
            }
        }

        ui::Node* volume = root.Add(ui::MakeBox(ui::Axis::Row, 10));
        volume->align = ui::Align::Center;
        auto* text = volume->Emplace<ui::Label>(std::format("Volume {}%", VolumePercent()));
        text->grow = 1;
        const auto step = [&](const char* id, const char* sign, int delta) {
            auto* button = volume->Emplace<ui::Button>(sign, [delta] { SetVolume(VolumePercent() + delta); });
            button->id = id;
            button->width = 96;
        };
        step("companion.volume_down", "-", -10);
        step("companion.volume_up", "+", 10);
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
        if (exit_until_ > 0) {
            TryExit();
        }
        // A rebuilt row starts where the finger left it, not at its first entry.
        if (ui::Node* row = Root().FindById(SLOTS_ID)) {
            slots_at_ = row->scroller.Position();
        }
        if (ui::Node* row = Root().FindById(PICTURES_ID)) {
            pictures_at_ = row->scroller.Position();
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
        return std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}", snapshot.game_id, static_cast<int>(snapshot.state),
                           snapshot.fast_forward, snapshot.rewinding, snapshot.can_rewind,
                           snapshot.can_save_states, snapshot.picture, slots, VolumePercent());
    }

    static int VolumePercent() {
        const double volume = Svc().settings->GetNumber(VOLUME_KEY, 1.0);
        return static_cast<int>(std::lround(std::clamp(volume, 0.0, 1.0) * 100.0));
    }

    static void SetVolume(int percent) {
        const double volume = std::clamp(percent, 0, 100) / 100.0;
        Svc().settings->SetNumber(VOLUME_KEY, volume);
        Svc().player->SetVolume(static_cast<float>(volume));
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

    // Leaving goes through the pause menu's own "Exit to home" row, so the game is saved and
    // its play time booked by the player screen itself.
    void Exit() {
        exit_until_ = pal::NowSeconds() + EXIT_WAIT;
        if (!ExitRow()) {
            Svc().input->Inject(input::Event::MakeNav(input::Nav::Menu));
        }
        TryExit();
    }

    static ui::Node* ExitRow() {
        ui::Page* top = Svc().stage->Top();
        return top && top->Route() == "player" ? top->Root().FindById("menu.exit") : nullptr;
    }

    void TryExit() {
        Services& svc = Svc();
        if (ui::Node* row = ExitRow()) {
            exit_until_ = 0;
            if (!row->OnNav(input::Nav::Confirm) && row->on_activate) {
                row->on_activate();
            }
            return;
        }
        if (pal::NowSeconds() < exit_until_) {
            return;
        }
        exit_until_ = 0;
        // The menu is on another of its pages: close the game from here.
        const ui::Page* top = svc.stage->Top();
        if (top && top->Route() == "player" && !game_id_.empty()) {
            svc.library->RecordPlay(game_id_, started_unix_,
                                    static_cast<std::int64_t>(pal::NowSeconds() - started_));
            svc.stage->Pop();
        }
    }

    std::string key_, game_id_, system_;
    float slots_at_ = 0, pictures_at_ = 0;
    bool save_mode_ = false;
    bool fast_ = false, rewind_ = false;
    double exit_until_ = 0;
    std::int64_t started_unix_ = 0;
    double started_ = 0;
};

} // namespace

DEN_REGISTER_SCREEN("companion.play", CompanionPlay);

} // namespace den::app
