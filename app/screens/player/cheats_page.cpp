// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The cheats of the running game (den/app/cheats.h). One page in two forms: rows for the pad
// on the main stage (Confirm turns a cheat on or off, Option edits it), and on the second
// screen the same rows with buttons for the finger. A cheat opens its own view: name, code,
// on or off, delete. Changes reach the game at once (Session::SetCheats).

#include <format>

#include "den/app/app.h"
#include "den/app/cheats.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/app/second_screen.h"
#include "den/base/files.h"
#include "den/player/cheats.h"

namespace den::app {

namespace {

constexpr float ROW_HEIGHT = 64;
constexpr float BUTTON_HEIGHT = 56;
constexpr int NAME_LENGTH = 64;
constexpr int CODE_LENGTH = 512;

// Grows when an import changes the cheats behind a page's back (it finishes on another page).
int g_revision = 0;

bool Playing(const player::Snapshot& snapshot) {
    return snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
}

// The new list for the game; a refusal (hardcore, playing together, an empty code) is told.
bool Commit(ui::Stage& stage, std::vector<player::Cheat> cheats) {
    const Status set = Svc().player->SetCheats(std::move(cheats));
    if (!set) {
        stage.Toast(set.error.empty() ? "The cheats could not be changed" : set.error, 5.0f);
    }
    ++g_revision;
    return set.ok;
}

void ImportCht(ui::Stage& stage) {
    PickOptions options;
    options.title = "Choose a .cht file";
    options.files = true;
    options.extensions = {"cht"};
    if (const auto game = Svc().library->Find(Svc().player->GetSnapshot().game_id)) {
        options.start = PathToUtf8(PathFromUtf8(game->path).parent_path());
    }
    Pick(std::move(options), [&stage](const std::string& path) {
        auto imported = player::ReadChtFile(PathFromUtf8(path));
        if (!imported) {
            stage.Toast(imported.error(), 5.0f);
            return;
        }
        std::vector<player::Cheat> cheats = Svc().player->Cheats();
        const int added = player::MergeCheats(cheats, imported.value());
        if (added == 0) {
            stage.Toast("This game has those cheats already");
        } else if (Commit(stage, std::move(cheats))) {
            stage.Toast(added == 1 ? std::string("1 cheat added") : std::format("{} cheats added", added));
        }
    });
}

// TOUCH: the second screen's form, which, like its other pages, has no hint bar and no
// selection unless the pad was handed over to it.
template <bool TOUCH>
class CheatsPage final : public ui::Page {
public:
    bool Immersive() const override {
        return TOUCH;
    }

    void Draw(ui::Context& ctx, const gfx::Rect& area) override {
        if constexpr (TOUCH) {
            if (!CompanionPad()) {
                SetFocus(nullptr, false);
            }
        }
        ui::Page::Draw(ctx, area);
    }

    void Build() override {
        Services& svc = Svc();
        const player::Snapshot snapshot = svc.player->GetSnapshot();
        cheats_ = svc.player->Cheats();
        revision_ = g_revision;
        if (editing_ >= static_cast<int>(cheats_.size())) {
            editing_ = -1;
        }
        const auto game = svc.library->Find(snapshot.game_id);
        const std::string title = editing_ >= 0 ? cheats_[static_cast<std::size_t>(editing_)].name : "Cheats";

        ui::Node* content = &Root();
        if constexpr (TOUCH) {
            content->axis = ui::Axis::Column;
            content->gap = 12;
            content->SetPadding(24, 16);
            ui::Node* bar = content->Add(ui::MakeBox(ui::Axis::Row, 12));
            bar->align = ui::Align::Center;
            Button(*bar, "cheats.back", "Back", [this] { Back(); });
            bar->Emplace<ui::Label>(title, svc.theme->text_heading, gfx::FONT_BOLD)->grow = 1;
            if (editing_ < 0) {
                Button(*bar, "cheats.add", "Add", [this] { Add(); });
                Button(*bar, "cheats.import", "Import .cht", [this] { ImportCht(GetStage()); });
            }
        } else {
            content = BuildPlainFrame(*this, title);
        }
        if (game) {
            content->Emplace<ui::Label>(game->name)->tone = ui::Tone::Dim;
        }
        if (!snapshot.cheats_off.empty()) {
            content->Emplace<ui::Label>(snapshot.cheats_off, svc.theme->text_small, gfx::FONT_BOLD);
        }
        ui::Node* list = content->Add(ui::MakeList(TOUCH ? 10 : 6));
        list->id = "cheats.list";
        list->grow = 1;
        if (editing_ >= 0) {
            BuildEdit(list, static_cast<std::size_t>(editing_));
        } else {
            BuildList(list);
        }
    }

    void Update(ui::Context&) override {
        if (!Playing(Svc().player->GetSnapshot())) {
            GetStage().Pop(); // the game ended
        } else if (revision_ != g_revision) {
            Rebuild();
        }
        // Back from a cheat's view: to its row, once the list is built.
        if (!refocus_.empty()) {
            if (ui::Node* row = Root().FindById(refocus_)) {
                SetFocus(row);
                refocus_.clear();
            } else if (++refocus_tries_ > 2) {
                refocus_.clear();
            }
        }
    }

    bool OnNav(input::Nav nav) override {
        if (nav == input::Nav::Back && editing_ >= 0) {
            Back();
            return true;
        }
        const ui::Node* focus = Focus();
        if (nav == input::Nav::Option && editing_ < 0 && focus && focus->id.starts_with("cheats.item.")) {
            Edit(std::stoi(focus->id.substr(12)));
            return true;
        }
        return false;
    }

    std::vector<ui::Hint> Hints() const override {
        if constexpr (TOUCH) {
            return {};
        }
        if (editing_ >= 0) {
            return {{input::Nav::Confirm, "Select"}, {input::Nav::Back, "Back"}};
        }
        return {{input::Nav::Confirm, "On / off"}, {input::Nav::Option, "Edit"}, {input::Nav::Back, "Back"}};
    }

private:
    void Button(ui::Node& parent, const char* id, const std::string& text, std::function<void()> fn) {
        auto* button = parent.Emplace<ui::Button>(text, std::move(fn));
        button->id = id;
        button->height = BUTTON_HEIGHT;
        button->SetPadding(20, 0);
    }

    void BuildList(ui::Node* list) {
        std::vector<ui::RowSpec> rows;
        if constexpr (!TOUCH) {
            rows.push_back(ActionRow("cheats.add", "Add a cheat", [this] { Add(); }));
            rows.push_back(ActionRow("cheats.import", "Import a .cht file", [this] { ImportCht(GetStage()); }));
        }
        if (cheats_.empty()) {
            rows.push_back(ValueRow("cheats.none", "No cheats for this game yet", [] { return std::string(); }));
        }
        AddRows(list, std::move(rows), ROW_HEIGHT);
        for (std::size_t i = 0; i < cheats_.size(); ++i) {
            const int index = static_cast<int>(i);
            ui::RowSpec row = ToggleRow(
                std::format("cheats.item.{}", i), cheats_[i].name,
                [this, i] { return i < cheats_.size() && cheats_[i].enabled; },
                [this, i](bool on) {
                    if (i < cheats_.size()) {
                        std::vector<player::Cheat> next = cheats_;
                        next[i].enabled = on;
                        Commit(GetStage(), std::move(next));
                    }
                });
            if (cheats_[i].name != cheats_[i].code) {
                row.description = cheats_[i].code;
            }
            if constexpr (!TOUCH) {
                AddRows(list, {std::move(row)}, ROW_HEIGHT);
                list->Children().back()->on_hold = [this, index] { Edit(index); }; // the mouse
                continue;
            }
            ui::Node* line = list->Add(ui::MakeBox(ui::Axis::Row, 10));
            line->align = ui::Align::Center;
            ui::Node* holder = line->Add(ui::MakeBox(ui::Axis::Column));
            holder->grow = 1;
            AddRows(holder, {std::move(row)}, ROW_HEIGHT);
            Button(*line, "", "Edit", [this, index] { Edit(index); });
            line->Children().back()->id = std::format("cheats.edit.{}", i);
        }
    }

    void BuildEdit(ui::Node* list, std::size_t i) {
        std::vector<ui::RowSpec> rows;
        rows.push_back(ToggleRow(
            "cheat.on", "On", [this, i] { return i < cheats_.size() && cheats_[i].enabled; },
            [this, i](bool on) { Change(i, [on](player::Cheat& cheat) { cheat.enabled = on; }); }));
        rows.push_back(ValueRow("cheat.name", "Name", [this, i] { return i < cheats_.size() ? cheats_[i].name : ""; },
                                [this, i] { AskName(i); }));
        rows.push_back(ValueRow("cheat.code", "Code", [this, i] { return i < cheats_.size() ? cheats_[i].code : ""; },
                                [this, i] { AskCode(i); }));
        rows.push_back(ActionRow("cheat.delete", "Delete", [this, i] {
            GetStage().Confirm("Delete this cheat?", cheats_[i].name, "Delete", [this, i] {
                std::vector<player::Cheat> next = cheats_;
                if (i < next.size()) {
                    next.erase(next.begin() + static_cast<std::ptrdiff_t>(i));
                    if (Commit(GetStage(), std::move(next))) {
                        Back();
                    }
                }
            });
        }));
        AddRows(list, std::move(rows), ROW_HEIGHT);
    }

    void Change(std::size_t i, const std::function<void(player::Cheat&)>& change) {
        if (i < cheats_.size()) {
            std::vector<player::Cheat> next = cheats_;
            change(next[i]);
            Commit(GetStage(), std::move(next));
        }
    }

    void AskName(std::size_t i) {
        ui::TextRequest request;
        request.title = "Name of the cheat";
        request.text = cheats_[i].name;
        request.max_length = NAME_LENGTH;
        GetStage().AskText(std::move(request), [this, i](const std::string& name) {
            Change(i, [name](player::Cheat& cheat) { cheat.name = name; });
        });
    }

    void AskCode(std::size_t i) {
        GetStage().AskText(CodeRequest(cheats_[i].code), [this, i](const std::string& code) {
            Change(i, [code](player::Cheat& cheat) { cheat.code = code; });
        });
    }

    static ui::TextRequest CodeRequest(std::string text) {
        ui::TextRequest request;
        request.title = "Code (several joined with +)";
        request.text = std::move(text);
        request.placeholder = "Game Genie, Action Replay, GameShark or raw";
        request.max_length = CODE_LENGTH;
        return request;
    }

    // A name, then the code; the new cheat is on.
    void Add() {
        ui::TextRequest request;
        request.title = "Name of the new cheat";
        request.placeholder = "Infinite lives";
        request.max_length = NAME_LENGTH;
        request.allow_empty = true;
        GetStage().AskText(std::move(request), [this](const std::string& name) {
            GetStage().AskText(CodeRequest({}), [this, name](const std::string& code) {
                const player::Cheat cheat{name, code, true};
                if (const std::string problem = player::CheatProblem(cheat); !problem.empty()) {
                    GetStage().Toast(problem);
                    return;
                }
                std::vector<player::Cheat> next = cheats_;
                next.push_back(cheat);
                Commit(GetStage(), std::move(next));
            });
        });
    }

    void Edit(int index) {
        editing_ = index;
        Rebuild(false);
    }

    void Back() {
        if (editing_ < 0) {
            GetStage().Pop();
            return;
        }
        refocus_ = std::format("cheats.item.{}", editing_);
        refocus_tries_ = 0;
        editing_ = -1;
        Rebuild(false);
    }

    int editing_ = -1; // the cheat whose own view is shown
    int revision_ = 0;
    std::string refocus_;
    int refocus_tries_ = 0;
    std::vector<player::Cheat> cheats_;
};

} // namespace

void OpenCheats(ui::Stage& stage) {
    // The second screen's stage has only the companion routes.
    stage.Push(stage.HasRoute("player.cheats") ? "player.cheats" : "companion.cheats");
}

void OpenCheats() {
    OpenCheats(CompanionStage());
}

std::string CheatsSummary() {
    const player::Snapshot snapshot = Svc().player->GetSnapshot();
    if (!snapshot.cheats_off.empty()) {
        return snapshot.hardcore ? "Off in hardcore mode" : "Off while playing together";
    }
    return snapshot.cheats_active > 0 ? std::format("{} on", snapshot.cheats_active) : std::string("None on");
}

using PadCheats = CheatsPage<false>;
using TouchCheats = CheatsPage<true>;
DEN_REGISTER_SCREEN("player.cheats", PadCheats);
DEN_REGISTER_SCREEN("companion.cheats", TouchCheats);

} // namespace den::app
