// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The pages under Settings, Sync: the conflicts kept (route "sync.conflicts") and the games
// the provider has, per console, to download (route "sync.games").

#include <format>
#include <map>

#include "../home/games.h"
#include "../home/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "sync_glue.h"

namespace den::app {

namespace {

ui::Node* AddList(ui::Page& page, const std::string& title, const std::string& id) {
    ui::Node* content = BuildPlainFrame(page, title);
    ui::Node* list = content->Add(ui::MakeList(8));
    list->id = id;
    list->pad_bottom = 24;
    return list;
}

class ConflictsScreen final : public ui::Page {
public:
    void Build() override {
        ui::Node* list = AddList(*this, "Conflicts", "sync.conflicts.list");
        revision_ = SyncSvc() ? SyncSvc()->Revision() : 0;
        const std::vector<sync::Conflict> conflicts = SyncSvc() ? SyncSvc()->Conflicts() : std::vector<sync::Conflict>();
        if (conflicts.empty()) {
            list->Emplace<ui::Label>("No conflicts. When a file changed on two devices, the newer one is kept "
                                     "and the other one is listed here.")
                ->tone = ui::Tone::Dim;
            return;
        }
        std::vector<ui::RowSpec> rows;
        for (std::size_t i = 0; i < conflicts.size(); ++i) {
            const sync::Conflict& conflict = conflicts[i];
            std::string name = conflict.path;
            if (const auto game = conflict.game_id.empty() ? std::nullopt : Svc().library->Find(conflict.game_id)) {
                name = game->name + ": " + conflict.path.substr(conflict.path.rfind('/') + 1);
            }
            ui::RowSpec row = ActionRow(std::format("sync.conflict.{}", i), name, [i, name] {
                Svc().stage->Confirm("Use this one instead?",
                                     "The kept copy of " + name + " replaces the current one, which is kept "
                                     "here in its place.",
                                     "Use this one", [i] {
                                         const Status used = SyncSvc()->UseConflict(i);
                                         Svc().stage->Toast(used.ok ? "Done. It syncs next." : used.error, 4.0f);
                                         if (used.ok) {
                                             SyncSvc()->SyncNow();
                                         }
                                         RebuildTop();
                                     });
            });
            row.description = std::format("From {}, {}. Use this one instead", conflict.from,
                                          SyncTimeText(conflict.time));
            rows.push_back(std::move(row));
        }
        AddRows(list, std::move(rows));
    }

    void Update(ui::Context&) override {
        if (SyncSvc() && SyncSvc()->Revision() != revision_ && !SyncSvc()->State().running) {
            Rebuild();
        }
    }

    bool OnNav(input::Nav nav) override {
        // Option drops the kept copy of the focused conflict.
        if (nav != input::Nav::Option || Focus() == nullptr || !Focus()->id.starts_with("sync.conflict.")) {
            return false;
        }
        const std::size_t index = std::stoul(Focus()->id.substr(14));
        Svc().stage->Confirm("Delete the kept copy?", "The current file stays as it is.", "Delete",
                             [index] {
                                 SyncSvc()->DismissConflict(index);
                                 RebuildTop();
                             });
        return true;
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Use this one"}, {input::Nav::Option, "Delete copy"},
                {input::Nav::Back, Tr("common.back")}};
    }

private:
    std::uint64_t revision_ = 0;
};

class RemoteGamesScreen final : public ui::Page {
public:
    void Build() override {
        ui::Node* list = AddList(*this, "Games on your other devices", "sync.games.list");
        sync::Engine* engine = SyncSvc();
        if (engine == nullptr) {
            return;
        }
        if (!job_) {
            job_ = engine->RefreshRemoteGames();
        }
        revision_ = engine->Revision();
        if (!job_->Finished()) {
            list->Emplace<ui::Label>(JobText(job_, "Looking"))->tone = ui::Tone::Dim;
            return;
        }
        if (job_->State() == JobState::Failed) {
            list->Emplace<ui::Label>(job_->Error())->tone = ui::Tone::Dim;
            return;
        }
        const std::vector<sync::RemoteGame> games = engine->RemoteGames();
        if (games.empty()) {
            list->Emplace<ui::Label>("No games yet. Turn on \"Upload my games\" on a device that has them.")
                ->tone = ui::Tone::Dim;
            return;
        }
        std::string system;
        std::vector<ui::RowSpec> rows;
        const auto flush = [&] {
            AddRows(list, std::move(rows));
            rows.clear();
        };
        for (std::size_t i = 0; i < games.size(); ++i) {
            const sync::RemoteGame& game = games[i];
            if (game.system != system) {
                flush();
                system = game.system;
                list->Add(ui::MakeHeading(SystemName(system)));
            }
            const std::string id = std::format("sync.game.{}", i);
            rows.push_back(ValueRow(
                id, game.name,
                [game, this, i] {
                    if (downloads_.contains(i) && !downloads_.at(i)->Finished()) {
                        return JobText(downloads_.at(i), "Downloading");
                    }
                    return game.present ? std::string("On this device") : SizeText(game.size);
                },
                [game, this, i] {
                    if (game.present || (downloads_.contains(i) && !downloads_.at(i)->Finished())) {
                        return;
                    }
                    downloads_[i] = SyncSvc()->StartDownload(game);
                }));
        }
        flush();
    }

    void Update(ui::Context&) override {
        bool finished = false;
        for (auto it = downloads_.begin(); it != downloads_.end();) {
            if (it->second->Finished()) {
                Svc().stage->Toast(it->second->State() == JobState::Done ? "Downloaded. It is in your library."
                                                                         : it->second->Error(),
                                   4.0f);
                it = downloads_.erase(it);
                finished = true;
            } else {
                ++it;
            }
        }
        if (finished || (job_ && job_->Finished() && !shown_)) {
            shown_ = job_ && job_->Finished();
            if (finished) {
                job_.reset(); // list again: what is on this device changed
                shown_ = false;
            }
            Rebuild();
        }
    }

private:
    JobPtr job_;
    bool shown_ = false;
    std::uint64_t revision_ = 0;
    std::map<std::size_t, JobPtr> downloads_;
};

} // namespace

DEN_REGISTER_SCREEN("sync.conflicts", ConflictsScreen);
DEN_REGISTER_SCREEN("sync.games", RemoteGamesScreen);

} // namespace den::app
