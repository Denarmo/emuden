// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pause menu's picture pages: the presets, shown on the game behind the menu as the
// selection moves over them, and the sliders of the one in use.

#include <cmath>
#include <format>

#include "den/app/pages.h"
#include "picture_choice.h"
#include "player_screen.h"

namespace den::app {

namespace {

constexpr float ROW_HEIGHT = 44;

system::Scope ScopeFor(const library::Entry& game, PictureScope scope) {
    return scope == PictureScope::Game      ? system::Scope::Game(game.system, game.id)
           : scope == PictureScope::Console ? system::Scope::Console(game.system)
                                            : system::Scope::Global();
}

} // namespace

const system::Key<std::string> PICTURE_KEY{
    "player.picture", player::PICTURE_AUTO,
    {.label = "Picture", .section = "Player", .per_console = true, .per_game = true}};

std::string PictureFor(const library::Entry& game) {
    return system::HasStore()
               ? system::Store().Get(PICTURE_KEY, system::Scope::Game(game.system, game.id))
               : Svc().settings->GetString(PICTURE_KEY.name(), player::PICTURE_AUTO);
}

PictureScope PictureScopeOf(const library::Entry& game) {
    if (!system::HasStore()) {
        return PictureScope::Console;
    }
    const system::Layer layer =
        system::Store().Source(PICTURE_KEY.name(), system::Scope::Game(game.system, game.id));
    // Nothing chosen yet, or only for every console: a choice made now is the console's.
    return layer == system::Layer::Game ? PictureScope::Game : PictureScope::Console;
}

void SavePicture(const library::Entry& game, PictureScope scope, const std::string& id) {
    if (!system::HasStore()) {
        Svc().settings->SetString(PICTURE_KEY.name(), id);
        return;
    }
    system::SettingsStore& store = system::Store();
    if (scope != PictureScope::Game) {
        store.Clear(PICTURE_KEY.name(), ScopeFor(game, PictureScope::Game));
    }
    if (scope == PictureScope::Everything) {
        store.Clear(PICTURE_KEY.name(), ScopeFor(game, PictureScope::Console));
    }
    store.Set(PICTURE_KEY, id, ScopeFor(game, scope));
}

std::vector<std::string> PictureIds() {
    std::vector<std::string> ids{player::PICTURE_AUTO};
    for (const player::PicturePreset& preset : player::PicturePresets()) {
        ids.push_back(preset.id);
    }
    return ids;
}

std::string PictureName(const std::string& id, const std::string& system) {
    const auto name = [](const std::string& wanted) {
        for (const player::PicturePreset& preset : player::PicturePresets()) {
            if (preset.id == wanted) {
                return preset.name;
            }
        }
        return std::string("Plain");
    };
    if (id != player::PICTURE_AUTO) {
        return name(id);
    }
    if (system.empty()) {
        return "Auto: by console";
    }
    // Short enough for a row: "Dot matrix (green)" is "Auto: Dot matrix".
    const std::string preset = name(player::AutoPicture(system));
    return "Auto: " + preset.substr(0, preset.find(" ("));
}

void PlayerScreen::LoadPicture() {
    picture_ = PictureFor(game_);
    picture_scope_ = static_cast<int>(PictureScopeOf(game_));
    Svc().player->SetPicture(picture_);
}

void PlayerScreen::ChoosePicture(const std::string& id) {
    picture_ = id;
    SavePicture(game_, static_cast<PictureScope>(picture_scope_), id);
    Svc().player->SetPicture(id);
    Show(View::Picture, "picture." + id);
}

void PlayerScreen::BuildPicture(ui::Node* list) {
    const std::string console = console_.short_name.empty() ? console_.name : console_.short_name;
    std::vector<ui::RowSpec> top;
    top.push_back(ChoiceRow(
        "picture.scope", "Apply to", {"This game", "All " + console + " games", "Every console"},
        [this] { return picture_scope_; },
        [this](int index) {
            picture_scope_ = index;
            SavePicture(game_, static_cast<PictureScope>(index), picture_);
        }));
    if (!Svc().player->PictureParams().empty()) {
        top.push_back(ActionRow("picture.adjust", "Adjust " + PictureName(Svc().player->GetSnapshot().picture),
                                [this] { Show(View::PictureAdjust); }));
    }
    AddRows(list, std::move(top), ROW_HEIGHT);

    // One row per choice. Moving over a row shows it on the game; Confirm keeps it.
    std::string group;
    const auto add = [&](const std::string& id) {
        std::vector<ui::RowSpec> row;
        row.push_back(ValueRow(
            "picture." + id, PictureName(id, game_.system),
            [this, id] { return std::string(picture_ == id ? "In use" : ""); },
            [this, id] { ChoosePicture(id); }));
        AddRows(list, std::move(row), ROW_HEIGHT);
        if (ui::Node* node = list->FindById("picture." + id)) {
            node->on_focus = [id] { Svc().player->SetPicture(id); };
        }
    };
    add(player::PICTURE_AUTO);
    for (const player::PicturePreset& preset : player::PicturePresets()) {
        if (preset.group != group) {
            group = preset.group;
            list->Add(ui::MakeHeading(group));
        }
        add(preset.id);
    }
    // The rows above the choices show the kept picture again.
    for (const char* id : {"picture.scope", "picture.adjust"}) {
        if (ui::Node* node = list->FindById(id)) {
            node->on_focus = [this] { Svc().player->SetPicture(picture_); };
        }
    }
}

void PlayerScreen::BuildPictureAdjust(ui::Node* list) {
    std::vector<ui::RowSpec> rows;
    for (const player::PictureParam& param : Svc().player->PictureParams()) {
        const std::string id = param.id;
        rows.push_back(SliderRow(
            "adjust." + id, param.name, param.min, param.max, param.step,
            [id] {
                for (const player::PictureParam& now : Svc().player->PictureParams()) {
                    if (now.id == id) {
                        return now.value;
                    }
                }
                return 0.0f;
            },
            [id](float value) { Svc().player->SetPictureParam(id, value); },
            [whole = param.step >= 1.0f, mask = id == "mask_type"](float value) {
                if (mask) {
                    return std::string(value < 0.5f ? "Grille" : value < 1.5f ? "Slots" : "Dots");
                }
                return whole ? std::format("{:.0f}", value) : std::format("{:.0f}%", std::round(value * 100.0f));
            }));
    }
    rows.push_back(ActionRow("adjust.reset", "Reset", [this] {
        Svc().player->ResetPictureParams();
        Rebuild();
    }));
    AddRows(list, std::move(rows), ROW_HEIGHT);
}

} // namespace den::app
