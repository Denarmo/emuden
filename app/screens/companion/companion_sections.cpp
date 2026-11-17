// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The sections of the quick settings, the same for every layout: the state slots with their
// pictures, the screen and picture (size, preset carousel, swap, frame rate, widescreen), the
// picture's sliders, the controls and core pages, the volumes.

#include <algorithm>
#include <cmath>
#include <format>

#include "companion_cells.h"
#include "companion_pictures.h"
#include "companion_play.h"
#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/picture_choice.h"
#include "den/app/volume_rows.h"
#include "den/base/files.h"

namespace den::app {

namespace {

constexpr float BUTTON_HEIGHT = 56;
constexpr float ROW_HEIGHT = 64;
constexpr float GAP = 12;
constexpr int SLOT_COLUMNS = 4;
constexpr float PICTURE_W = 300; // a card of the picture carousel
constexpr const char* FPS_NAMES[] = {"FPS: Off", "FPS: On", "FPS: Graph"};

void Caption(ui::Node& root, const char* text) {
    auto* label = root.Emplace<ui::Label>(text, Svc().theme->text_small, gfx::FONT_BOLD);
    label->tone = ui::Tone::Dim;
}

} // namespace

void CompanionPlay::BuildStates(ui::Node& root, int columns) {
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

    CompanionGrid(root, player::STATE_SLOTS, columns > 0 ? columns : SLOT_COLUMNS, GAP, [&](ui::Node& row, int slot) -> ui::Node* {
        const bool used = svc.player->HasState(slot);
        auto* cell = row.Emplace<CompanionSlot>(
            TrF("Slot {}", slot + 1),
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

void CompanionPlay::BuildScreen(ui::Node& root, const player::Snapshot& snapshot) {
    GamePlayer* screen = CurrentPlayer();
    if (screen) {
        Caption(root, "Screen size");
        const auto& names = ScreenSizeNames();
        const int count = static_cast<int>(names.size());
        const int current = screen->ScreenSizeIndex();
        CompanionGrid(root, count, count, GAP, [&](ui::Node& row, int index) -> ui::Node* {
            auto* card = row.Emplace<CompanionCard>(names[static_cast<std::size_t>(index)], [index] {
                if (GamePlayer* now = CurrentPlayer()) {
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
                        Open(QuickView::Picture);
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
            if (GamePlayer* now = CurrentPlayer()) {
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
            if (GamePlayer* now = CurrentPlayer()) {
                now->SetFpsMode((fps + 1) % GamePlayer::FPS_MODES);
            }
        });
        counter->id = "companion.fps";
        counter->grow = 1;
        counter->width = 0;
        counter->selected = fps != 0;
        if (screen->CanWidescreen()) {
            const bool wide = screen->Widescreen();
            auto* card = row->Emplace<CompanionCard>("Widescreen", [this, wide] {
                GamePlayer* now = CurrentPlayer();
                if (now && !now->WidescreenRestarts()) {
                    now->SetWidescreen(!wide);
                } else if (now) {
                    GetStage().Confirm(WIDESCREEN_TITLE, WIDESCREEN_TEXT, "Start again", [wide] {
                        if (GamePlayer* still = CurrentPlayer()) {
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
void CompanionPlay::BuildPicture(ui::Node& root) {
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

void CompanionPlay::BuildControls(ui::Node& root) {
    const auto page = [](const char* name) {
        return [name] {
            if (GamePlayer* now = CurrentPlayer()) {
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
void CompanionPlay::BuildVolume(ui::Node& root) {
    ui::Node* list = root.Add(ui::MakeList(10));
    list->id = "companion.volumes";
    list->grow = 1;
    AddRows(list, VolumeRows("companion", other_volume_), ROW_HEIGHT);
}

void CompanionPlay::Save(int slot) {
    const auto save = [this, slot] {
        const Status saved = Svc().player->SaveState(slot);
        if (!saved) {
            GetStage().Toast(saved.error.empty() ? "The state could not be saved" : saved.error, 5.0f);
            return;
        }
        // The picture of the slot changed under the same file name.
        Svc().textures->Forget(PathToUtf8(Svc().player->StateThumbnail(slot)));
        GetStage().Toast(TrF("Saved to slot {}", slot + 1));
        Rebuild();
    };
    if (Svc().player->HasState(slot)) {
        GetStage().Confirm(TrF("Replace slot {}?", slot + 1), "The state saved there is overwritten.",
                           "Replace", save);
    } else {
        save();
    }
}

void CompanionPlay::Load(int slot) {
    if (!Svc().player->HasState(slot)) {
        GetStage().Toast(TrF("Slot {} is empty", slot + 1));
        return;
    }
    const Status loaded = Svc().player->LoadState(slot);
    GetStage().Toast(loaded ? TrF("Loaded slot {}", slot + 1)
                     : loaded.error.empty() ? std::string("The state could not be loaded")
                                            : loaded.error);
}

} // namespace den::app
