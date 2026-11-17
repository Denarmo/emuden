// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Tidy up the library" (route "library.tidy", from Settings > Library): the plan of
// den/library/tidy.h first, as a list the user confirms, then its progress, then what was
// done, with Undo. The work runs on a thread of its own; the page rebuilds when it moves on.
// Param "undo" = "1" takes the last tidy-up back instead.

#include <atomic>
#include <format>
#include <mutex>
#include <thread>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/frame.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "den/library/tidy.h"
#include "den/system/settings.h"

namespace den::app {

namespace {

namespace tidy = library::tidy;

enum class Step { Looking, Showing, Working, Done };

constexpr std::size_t SHOWN_LINES = 300;

std::string Count(int n, const char* one, const char* many) {
    return std::format("{} {}", n, n == 1 ? one : many);
}

// A game's settings follow it to its new id (the store is the main thread's).
void MoveSettings(const std::vector<tidy::Moved>& moved) {
    if (!system::HasStore()) {
        return;
    }
    system::SettingsStore& store = system::Store();
    for (const tidy::Moved& m : moved) {
        const system::Scope from = system::Scope::Game(m.system, m.old_id);
        const JsonValue values = store.Overrides(from);
        if (!values.members.empty()) {
            store.SetOverrides(system::Scope::Game(m.system, m.new_id), values);
            store.ClearScope(from);
        }
    }
}

std::string Line(const tidy::Change& change) {
    switch (change.kind) {
    case tidy::ChangeKind::Rename:
        return std::format("{}  \xE2\x86\x92  {}", PathToUtf8(change.from.filename()), PathToUtf8(change.to.filename()));
    case tidy::ChangeKind::ShowName:
        return TrF("{}  shown as  {}  ({})", PathToUtf8(change.from.filename()), change.name, change.why);
    case tidy::ChangeKind::RemoveJunk:
        return TrF("Delete {}  (a Mac's leftover file)", PathToUtf8(change.from));
    case tidy::ChangeKind::RemoveFolder:
        return TrF("Delete the empty folder {}", PathToUtf8(change.from));
    case tidy::ChangeKind::LinkHack:
        return TrF("{}  is a hack of  {}", PathToUtf8(change.from.filename()), change.name);
    }
    return {};
}

class TidyPage final : public ui::Page {
public:
    ~TidyPage() override {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void Build() override {
        ui::Node* content = BuildPlainFrame(*this, "Tidy up the library");
        ui::Node* list = content->Add(ui::MakeList(8));
        list->id = "tidy.list";
        list->pad_bottom = 64;
        const Step step = step_.load();
        if (!started_) {
            started_ = true;
            Start(Param("undo") == "1");
        }
        std::lock_guard lock(mutex_);
        std::vector<ui::RowSpec> rows;
        if (step == Step::Looking) {
            list->Emplace<ui::Label>(undo_ ? "Taking the last tidy-up back..." : "Looking at your games...");
            return;
        }
        if (step == Step::Working) {
            list->Emplace<ui::Label>(TrF("Tidying up... {}%", static_cast<int>(progress_.load() * 100)));
            return;
        }
        if (step == Step::Done) {
            list->Emplace<ui::Label>(summary_);
            for (const std::string& error : done_.errors) {
                list->Emplace<ui::Label>(error)->tone = ui::Tone::Dim;
            }
            if (!undo_ && tidy::CanUndo(Svc().data_dir)) {
                rows.push_back(ActionRow("tidy.undo", "Undo this tidy-up", [this] {
                    GetStage().Replace("library.tidy", {{"undo", "1"}});
                }));
            }
            rows.push_back(ActionRow("tidy.close", "Close", [this] { GetStage().Pop(); }));
            AddRows(list, std::move(rows));
            return;
        }
        int renames = 0, names = 0, junk = 0, hacks = 0;
        for (const tidy::Change& change : plan_.changes) {
            (change.kind == tidy::ChangeKind::Rename     ? renames
             : change.kind == tidy::ChangeKind::ShowName ? names
             : change.kind == tidy::ChangeKind::LinkHack ? hacks
                                                         : junk)++;
        }
        if (plan_.changes.empty()) {
            if (!retried_) {
                retried_ = true; // nothing to rename: box art is still looked up again
                Svc().library->RetryCovers();
                Svc().library->Scan();
            }
            list->Emplace<ui::Label>(TrF("All {} games have clean names already. Games without box art "
                                                 "look for it again.",
                                                 plan_.games));
            rows.push_back(ActionRow("tidy.close", "Close", [this] { GetStage().Pop(); }));
            AddRows(list, std::move(rows));
            return;
        }
        list->Emplace<ui::Label>(std::format("{} a clean name, {} a clean name shown (the file stays), {} go.",
                                             Count(renames, "file gets", "files get"),
                                             Count(names, "game gets", "games get"),
                                             Count(junk, "leftover file or empty folder", "leftover files and empty folders")));
        if (hacks > 0) {
            list->Emplace<ui::Label>(std::format("{} with the game {} made from (the files stay where they are).",
                                                 Count(hacks, "hack goes", "hacks go"), hacks == 1 ? "it was" : "they were"));
        }
        list->Emplace<ui::Label>("Saves, states, play time and game settings go with the new names. Games without "
                                 "box art look for it again; a hack finds the box of the game it was made from by "
                                 "its header and gets the HACK sticker.")
            ->tone = ui::Tone::Dim;
        rows.push_back(ActionRow("tidy.go", "Tidy up", [this] { Apply(); }));
        rows.push_back(ActionRow("tidy.cancel", "Cancel", [this] { GetStage().Pop(); }));
        AddRows(list, std::move(rows));
        for (std::size_t i = 0; i < plan_.changes.size() && i < SHOWN_LINES; ++i) {
            auto* line = list->Emplace<ui::Label>(Line(plan_.changes[i]));
            line->id = std::format("tidy.line.{}", i);
            line->focusable = true; // a pad reaches the lines below the fold
        }
        if (plan_.changes.size() > SHOWN_LINES) {
            list->Emplace<ui::Label>(std::format("and {} more", plan_.changes.size() - SHOWN_LINES))->tone = ui::Tone::Dim;
        }
    }

    void Update(ui::Context&) override {
        const Step step = step_.load();
        if (step != shown_ || (step == Step::Working && progress_.load() - shown_progress_ >= 0.05f)) {
            if (step == Step::Done && shown_ != Step::Done) {
                std::lock_guard lock(mutex_);
                MoveSettings(moved_);
                Svc().library->Scan(); // renamed games are listed and their box art looked up
            }
            shown_ = step;
            shown_progress_ = progress_.load();
            Rebuild();
        }
    }

    bool OnNav(input::Nav nav) override {
        if (nav == input::Nav::Back && step_.load() != Step::Working) {
            GetStage().Pop();
            return true;
        }
        return false;
    }

    std::vector<ui::Hint> Hints() const override {
        return {{input::Nav::Confirm, "Select"}, {input::Nav::Back, "Back"}};
    }

private:
    void Start(bool undo) {
        undo_ = undo;
        library::Library* library = Svc().library;
        const std::filesystem::path data = Svc().data_dir;
        worker_ = std::thread([this, library, data, undo] {
            if (undo) {
                const Status undone = tidy::Undo(*library, data, [this](const tidy::Moved& m) {
                    std::lock_guard lock(mutex_);
                    moved_.push_back(m);
                });
                std::lock_guard lock(mutex_);
                summary_ = undone ? "The last tidy-up was taken back." : undone.error;
                step_ = Step::Done;
                return;
            }
            tidy::Plan plan = tidy::MakePlan(*library, data);
            std::lock_guard lock(mutex_);
            plan_ = std::move(plan);
            step_ = Step::Showing;
        });
    }

    void Apply() {
        if (worker_.joinable()) {
            worker_.join();
        }
        step_ = Step::Working;
        library::Library* library = Svc().library;
        const std::filesystem::path data = Svc().data_dir;
        worker_ = std::thread([this, library, data] {
            tidy::Plan plan;
            {
                std::lock_guard lock(mutex_);
                plan = plan_;
            }
            tidy::Done done = tidy::Apply(
                *library, plan, data,
                [this](const tidy::Moved& m) {
                    std::lock_guard lock(mutex_);
                    moved_.push_back(m);
                },
                [this](float fraction) { progress_ = fraction; });
            std::lock_guard lock(mutex_);
            summary_ = TrF("Done: {} renamed, {} shown clean, {} deleted{}.", Count(done.renamed, "file", "files"),
                                   Count(done.named, "name", "names"), Count(done.removed, "leftover", "leftovers"),
                                   done.linked > 0 ? ", " + Count(done.linked, "hack", "hacks") + (done.linked == 1 ? " linked to its game" : " linked to their games") : "");
            done_ = std::move(done);
            step_ = Step::Done;
        });
    }

    std::thread worker_;
    std::mutex mutex_; // plan_, done_, moved_, summary_
    std::atomic<Step> step_{Step::Looking};
    std::atomic<float> progress_{0};
    Step shown_ = Step::Looking;
    float shown_progress_ = 0;
    bool started_ = false, undo_ = false, retried_ = false;
    tidy::Plan plan_;
    tidy::Done done_;
    std::vector<tidy::Moved> moved_;
    std::string summary_;
};

} // namespace

DEN_REGISTER_SCREEN("library.tidy", TidyPage);

} // namespace den::app
