// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen under the in-app browser. For the browser (any site) it is a panel with
// two tabs: Bookmarks (a tap opens one in the page on the main screen) and Downloads (what
// each download became, with Undo; route "companion.download", param id). Touch works as
// everywhere; View on the pad hands the pad over and back (web_screen.cpp). Under a page
// opened for one site it only says where the page is. Route "companion.web".

#include <atomic>
#include <format>
#include <memory>
#include <thread>

#include "den/app/web_browser.h"
#include "companion_page.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/library/downloads.h"

namespace den::app {

namespace {

namespace downloads = library::downloads;

constexpr float ROW_HEIGHT = 56;

std::shared_ptr<downloads::Setup> Setups() {
    return downloads::SharedSetup(*Svc().library, Svc().data_dir);
}

// What the list shows changes when this does.
std::string Signature(const std::vector<downloads::Record>& history) {
    std::string out = std::to_string(history.size());
    for (std::size_t i = 0; i < history.size() && i < 3; ++i) {
        out += history[i].id + (history[i].undone ? "u" : "");
    }
    return out;
}

ui::Button* AddButton(ui::Node* list, const std::string& id, const std::string& text, std::function<void()> action) {
    auto* button = list->Emplace<ui::Button>(text, std::move(action));
    button->id = id;
    button->centered = false;
    button->height = ROW_HEIGHT;
    return button;
}

class CompanionWeb final : public CompanionPage {
public:
    void Build() override {
        Services& svc = Svc();
        browser_ = web::CurrentState().browser;
        pad_ = CompanionPad();
        Root().SetPadding(24, 16);
        Root().gap = 10;
        if (!browser_) {
            Root().justify = ui::Align::Center;
            Root().Emplace<ui::Label>("Browser", svc.theme->text_title, gfx::FONT_BOLD);
            Root().Emplace<ui::Label>("The page is on the other screen.")->tone = ui::Tone::Dim;
            return;
        }
        ui::Node* bar = Root().Add(ui::MakeBox(ui::Axis::Row, 10));
        bar->align = ui::Align::Center;
        for (const auto& [tab, text] : {std::pair{0, "Bookmarks"}, std::pair{1, "Downloads"}}) {
            auto* button = bar->Emplace<ui::Button>(text, [this, tab = tab] {
                tab_ = tab;
                Rebuild(false);
            });
            button->id = std::format("companion.web.tab{}", tab);
            button->selected = tab_ == tab;
            button->width = 200;
            button->height = 52;
        }
        auto* hint = bar->Emplace<ui::Label>(CompanionPad() ? "View: back to the page" : "View on the pad: choose here");
        hint->tone = ui::Tone::Dim;
        hint->grow = 1;
        hint->pad_left = 12;

        ui::Node* list = Root().Add(ui::MakeList(6));
        list->id = "companion.web.list";
        list->grow = 1;
        if (tab_ == 0) {
            const std::vector<web::Bookmark> marks = web::LoadBookmarks();
            if (marks.empty()) {
                list->Emplace<ui::Label>("No bookmarks yet: Menu > Bookmark this page on the other screen.")->tone =
                    ui::Tone::Dim;
            }
            for (std::size_t i = 0; i < marks.size(); ++i) {
                AddButton(list, std::format("companion.web.mark{}", i), marks[i].title.empty() ? marks[i].url : marks[i].title,
                          [url = marks[i].url] {
                              web::GoTo(url);
                              SetCompanionPad(false);
                          });
            }
            return;
        }
        const std::vector<downloads::Record> history = Setups()->History();
        signature_ = Signature(history);
        if (history.empty()) {
            list->Emplace<ui::Label>("Nothing downloaded yet.")->tone = ui::Tone::Dim;
        }
        for (const downloads::Record& record : history) {
            AddButton(list, "companion.web.dl." + record.id,
                      (record.undone ? "Undone: " : "") + record.message,
                      [this, id = record.id] { GetStage().Push("companion.download", {{"id", id}}); });
        }
    }

    void Update(ui::Context&) override {
        if (web::CurrentState().browser != browser_ || CompanionPad() != pad_) {
            Rebuild(false);
        } else if (browser_ && tab_ == 1 && Signature(Setups()->History()) != signature_) {
            Rebuild();
        }
    }

private:
    int tab_ = 0;
    bool browser_ = false, pad_ = false;
    std::string signature_;
};

class CompanionDownload final : public CompanionPage {
public:
    ~CompanionDownload() override {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void Build() override {
        Services& svc = Svc();
        Root().SetPadding(24, 16);
        Root().gap = 10;
        ui::Node* bar = Root().Add(ui::MakeBox(ui::Axis::Row, 10));
        bar->align = ui::Align::Center;
        auto* back = bar->Emplace<ui::Button>("Back", [this] { GetStage().Pop(); });
        back->id = "companion.download.back";
        back->width = 110;
        back->height = 52;
        downloads::Record record;
        for (const downloads::Record& kept : Setups()->History()) {
            if (kept.id == Param("id")) {
                record = kept;
            }
        }
        bar->Emplace<ui::Label>(record.download, svc.theme->text_heading, gfx::FONT_BOLD)->grow = 1;
        if (record.id.empty()) {
            Root().Emplace<ui::Label>("That download is not in the list any more.");
            return;
        }
        Root().Emplace<ui::Label>(record.message);
        for (const downloads::AddedGame& game : record.games) {
            const auto entry = svc.library->Find(game.id);
            Root().Emplace<ui::Label>(entry ? std::format("In the library: {} ({})", entry->name, record.system_name)
                                            : std::format("{}: not in the library now", game.name))
                ->tone = ui::Tone::Dim;
        }
        if (record.games.empty() || record.undone) {
            Root().Emplace<ui::Label>("The file is in EmuDEN's Downloads folder.")->tone = ui::Tone::Dim;
        }
        const bool can_undo = !record.undone && (record.outcome == downloads::Outcome::Added ||
                                                 record.outcome == downloads::Outcome::HackAdded);
        if (can_undo || busy_) {
            auto* undo = Root().Emplace<ui::Button>(busy_ ? "Taking it back..." : "Undo", [this, id = record.id] {
                Undo(id);
            });
            undo->id = "companion.download.undo";
            undo->height = 56;
        }
        if (!result_.empty()) {
            Root().Emplace<ui::Label>(result_)->tone = ui::Tone::Dim;
        }
    }

    void Update(ui::Context&) override {
        if (finished_.exchange(false)) {
            busy_ = false;
            Rebuild();
        }
    }

private:
    void Undo(const std::string& id) {
        if (busy_) {
            return;
        }
        busy_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
        worker_ = std::thread([this, id, setup = Setups()] {
            const Status undone = setup->Undo(id);
            result_ = undone ? "Taken back: the files are in EmuDEN's Downloads folder again." : undone.error;
            finished_ = true;
        });
        Rebuild();
    }

    std::thread worker_;
    std::atomic<bool> finished_{false};
    bool busy_ = false;
    std::string result_; // written by the worker before finished_
};

} // namespace

DEN_REGISTER_SCREEN("companion.web", CompanionWeb);
DEN_REGISTER_SCREEN("companion.download", CompanionDownload);

} // namespace den::app
