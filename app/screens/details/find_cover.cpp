// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Find cover" on a game's page: the covers the hack cover sources have for the game, for
// the user to pick one when the automatic match is wrong or missing. Route "find_cover",
// param id. The Covers section of Settings is here too.

#include <memory>

#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"

namespace den::app {

namespace {

constexpr const char* ONLINE_KEY = "library.hack_covers.online";
constexpr const char* RA_KEY = "library.hack_covers.retroachievements_key";
constexpr const char* GRID_KEY = "library.hack_covers.steamgriddb_key";
constexpr float TILE_W = 168, TILE_H = 224;

std::string SourceName(const std::string& source) {
    if (source == "collection") {
        return "Fan cover collection";
    }
    if (source == "libretro") {
        return "libretro thumbnails";
    }
    if (source == "retroachievements") {
        return "RetroAchievements";
    }
    if (source == "steamgriddb") {
        return "SteamGridDB";
    }
    return "Original game";
}

// What the lookup job hands back; the page may be gone by then.
struct Lookup {
    std::vector<library::CoverChoice> choices;
    bool done = false;
};

void UseCover(const std::string& id, const std::string& image) {
    Services& svc = Svc();
    if (const Status set = svc.library->SetCover(id, image); !set.ok) {
        svc.stage->Toast(set.error);
        return;
    }
    // The file may replace an earlier pick under the same name.
    if (const auto game = svc.library->Find(id)) {
        svc.textures->Forget(game->cover);
    }
    svc.stage->Toast("Cover changed");
    svc.stage->Pop();
}

class FindCoverScreen final : public ui::Page {
public:
    ~FindCoverScreen() override {
        if (job_) {
            job_->Cancel();
        }
    }

    void Build() override {
        Services& svc = Svc();
        const std::string id = Param("id");
        const auto game = svc.library->Find(id);
        ui::Node* content = BuildPlainFrame(*this, game ? "Find cover: " + game->name : "Find cover");
        if (!game) {
            return;
        }
        if (!lookup_) {
            lookup_ = std::make_shared<Lookup>();
            job_ = svc.jobs->Run(
                "Looking for covers",
                [lookup = lookup_, id](Job& job) {
                    lookup->choices = Svc().library->CoverChoices(id, [&job] { return job.CancelRequested(); });
                    return Status{};
                },
                [lookup = lookup_](Job&) { lookup->done = true; });
        }
        shown_ = lookup_->done;
        const bool online = svc.settings->GetBool(ONLINE_KEY, true);
        auto* note = content->Emplace<ui::Label>(
            !lookup_->done ? "Looking for covers..."
            : !lookup_->choices.empty() ? "Pick the cover of this game."
            : online ? "No source has a cover under this game's name."
                     : "Online covers for ROM hacks are switched off in Settings > Covers.");
        note->tone = ui::Tone::Dim;

        if (!lookup_->choices.empty()) {
            ui::Node* shelf = content->Add(ui::MakeShelf(18));
            shelf->id = "find_cover.shelf";
            shelf->SetPadding(10, 10);
            int index = 0;
            for (const library::CoverChoice& choice : lookup_->choices) {
                auto* tile = shelf->Emplace<ui::Tile>(choice.title, choice.image, TILE_W, TILE_H);
                tile->id = "find_cover.choice." + std::to_string(index++);
                tile->subtitle = SourceName(choice.source);
                tile->on_activate = [id, image = choice.image] { UseCover(id, image); };
            }
        }

        ui::Node* list = content->Add(ui::MakeList());
        std::vector<ui::RowSpec> rows;
        rows.push_back(ActionRow(
            "find_cover.file", "Choose an image file",
            [id] {
                PickOptions options;
                options.title = "Choose a cover";
                options.files = true;
                options.extensions = {"png", "jpg", "jpeg"};
                Pick(std::move(options), [id](const std::string& file) { UseCover(id, file); });
            },
            "A PNG or JPEG on this device"));
        rows.push_back(ActionRow(
            "find_cover.reset", "Choose automatically",
            [id] {
                Svc().library->ResetCover(id);
                Svc().stage->Toast("The cover is looked up again");
                Svc().stage->Pop();
            },
            "Forgets a cover picked here"));
        AddRows(list, std::move(rows));
    }

    void Update(ui::Context&) override {
        if (lookup_ && lookup_->done && !shown_) {
            Rebuild();
        }
    }

private:
    std::shared_ptr<Lookup> lookup_;
    JobPtr job_;
    bool shown_ = false;
};

ui::RowSpec KeyRow(const char* id, const char* label, const char* key, const char* help) {
    ui::RowSpec row = ValueRow(
        id, label, [key] { return Svc().settings->GetString(key).empty() ? "Not set" : "Set"; },
        [key, label] {
            ui::TextRequest request;
            request.title = label;
            request.placeholder = "Key";
            request.max_length = 80;
            request.allow_empty = true;
            Svc().stage->AskText(std::move(request), [key](const std::string& text) {
                if (text.empty()) {
                    Svc().settings->Remove(key);
                } else {
                    Svc().settings->SetString(key, text);
                }
                RebuildTop();
            });
        });
    row.description = help;
    return row;
}

std::vector<ui::RowSpec> CoverRows() {
    std::vector<ui::RowSpec> rows;
    ui::RowSpec online = ToggleRow(
        "covers.hacks_online", "Online covers for ROM hacks",
        [] { return Svc().settings->GetBool(ONLINE_KEY, true); },
        [](bool on) {
            Svc().settings->SetBool(ONLINE_KEY, on);
            if (on) {
                Svc().library->Scan();
            }
        });
    online.description = "Looks for a hack's own cover by its name; off: the original game's box, tagged";
    rows.push_back(std::move(online));
    rows.push_back(KeyRow("covers.retroachievements", "RetroAchievements web API key", RA_KEY,
                          "Optional: hacks with achievement sets. The key is on your account's settings page"));
    rows.push_back(KeyRow("covers.steamgriddb", "SteamGridDB API key", GRID_KEY,
                          "Optional: fan-made covers. The key is on your account's preferences page"));
    return rows;
}

} // namespace

DEN_REGISTER_SCREEN("find_cover", FindCoverScreen);
DEN_REGISTER_SETTINGS(41, "Covers", CoverRows);

} // namespace den::app
