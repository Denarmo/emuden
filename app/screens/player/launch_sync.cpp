// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Before a game starts, its saves are checked against the sync provider (docs/sync.md,
// section 3.3): a newer online save comes down without a question, a save changed on both
// devices asks which one to keep. Offline, slow or skipped, the game starts as it is.

#include "../sync/sync_glue.h"
#include "den/app/pages.h"
#include "den/base/log.h"
#include "glyphs.h"
#include "player_screen.h"

namespace den::app {

namespace {

constexpr double CHECK_SECONDS = 3.0;
constexpr float ROW_HEIGHT = 52;

} // namespace

struct PlayerScreen::SaveCheck {
    sync::CheckGate gate;
    sync::GameCheck result;
    JobPtr job;
    double started = 0;
    bool resolving = false;
    Status resolved;
};

bool PlayerScreen::StartSaveCheck() {
    sync::Engine* engine = SyncSvc();
    // An engine app keeps its own saves; only the player's go through sync.
    if (engine == nullptr || core_.runner == plugins::Runner::Engine || !engine->SyncsSaves()) {
        return false;
    }
    auto check = std::make_shared<SaveCheck>();
    check->started = pal::NowSeconds();
    check->job = Svc().jobs->Run("check saves", [engine, check, id = game_.id](Job&) {
        check->result = engine->CheckGame(id, check->gate, CHECK_SECONDS);
        return Status();
    });
    check_ = check;
    phase_ = Phase::Checking; // Build, which called Open, shows it
    return true;
}

void PlayerScreen::TickSaveCheck() {
    const std::shared_ptr<SaveCheck> check = check_;
    if (!check) {
        return;
    }
    const bool done = check->job && check->job->Finished();
    if (check->resolving) {
        if (done) {
            if (!check->resolved.ok) {
                GetStage().Toast(check->resolved.error, 5.0f);
            }
            check_ = nullptr;
            Proceed();
        }
        return;
    }
    if (phase_ != Phase::Checking) {
        return;
    }
    if (!done) {
        if (pal::NowSeconds() - check->started >= CHECK_SECONDS) {
            check->gate.Abandon(); // offline or slow: the game starts with what it has
            log::Info("player", "save check for {} took too long; starting", game_.name);
            check_ = nullptr;
            Proceed();
        }
        return;
    }
    const sync::GameCheck& result = check->result;
    if (!result.error.empty()) {
        log::Info("player", "save check for {}: {}", game_.name, result.error);
    }
    if (!result.conflicts.empty()) {
        phase_ = Phase::Choosing;
        focus_after_build_ = "sync.online";
        Rebuild();
        return;
    }
    if (result.downloaded > 0) {
        GetStage().Toast("Using your newer save from online");
    }
    check_ = nullptr;
    Proceed();
}

void PlayerScreen::SkipSaveCheck() {
    if (check_ && phase_ == Phase::Checking) {
        check_->gate.Abandon();
        check_ = nullptr;
        Proceed();
    }
}

void PlayerScreen::AbandonSaveCheck() {
    if (check_) {
        check_->gate.Abandon();
        check_ = nullptr;
    }
}

void PlayerScreen::Choose(bool online) {
    const std::shared_ptr<SaveCheck> check = check_;
    sync::Engine* engine = SyncSvc();
    if (!check || phase_ != Phase::Choosing || engine == nullptr) {
        return;
    }
    check->resolving = true;
    phase_ = Phase::Checking;
    check->job = Svc().jobs->Run("choose save", [engine, check, id = game_.id, online](Job&) {
        check->resolved = engine->ResolveGame(id, online);
        return Status();
    });
    Rebuild();
}

void PlayerScreen::BuildSaveCheck() {
    const ui::Theme& theme = *Svc().theme;
    ui::Node& root = Root();
    root.axis = ui::Axis::Column;
    root.justify = ui::Align::Center;
    root.align = ui::Align::Center;
    ui::Node* panel = root.Add(ui::MakeBox(ui::Axis::Column, 10));
    panel->width = 620;
    panel->background = theme.dialog;
    panel->SetPadding(28, 28);
    const bool choosing = phase_ == Phase::Choosing && check_;
    const auto line = [&](const std::string& text, bool dim) {
        auto* label = panel->Emplace<ui::Label>(text, theme.text_body);
        if (dim) {
            label->tone = ui::Tone::Dim;
        }
    };
    if (!choosing) {
        const bool resolving = check_ && check_->resolving;
        panel->Emplace<ui::Label>(game_.name, theme.text_heading, gfx::FONT_BOLD);
        line(resolving ? "Getting your save ready\xE2\x80\xA6" : "Checking for newer saves\xE2\x80\xA6", true);
        if (!resolving) {
            ui::Node* list = panel->Add(ui::MakeList(6));
            AddRows(list, {ActionRow("sync.skip", "Skip", [this] { SkipSaveCheck(); })}, ROW_HEIGHT);
        }
        return;
    }
    const sync::GameCheck& result = check_->result;
    const std::string device = result.online_device.empty() ? "another device" : result.online_device;
    panel->Emplace<ui::Label>("Which save do you want?", theme.text_heading, gfx::FONT_BOLD);
    line(game_.name + " was played on " + device + " and on this device", true);
    line("since they last synced.", true);
    line("Online: " + device + ", " + SyncTimeText(result.online_time), false);
    line("This device: " + SyncTimeText(result.local_time), false);
    auto* note = panel->Emplace<ui::Label>("The other one is kept under Settings > Sync > Conflicts.",
                                           theme.text_small);
    note->tone = ui::Tone::Dim;
    note->pad_bottom = 6;
    ui::Node* list = panel->Add(ui::MakeList(6));
    AddRows(list,
            {ActionRow("sync.online", "Use the online save", [this] { Choose(true); }),
             ActionRow("sync.local", "Keep this device's save", [this] { Choose(false); })},
            ROW_HEIGHT);
    if (!Svc().input->TouchOnly()) {
        auto* hints = panel->Emplace<HintLine>(
            Svc().input->ActiveFamily(),
            std::vector<HintLine::Item>{{input::Nav::Confirm, "Select"}, {input::Nav::Back, "Don't play"}});
        hints->pad_top = 6;
    }
}

} // namespace den::app
