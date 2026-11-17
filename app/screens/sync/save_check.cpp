// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Before a game starts, its saves are checked against the sync provider (docs/sync.md,
// section 3.3): a newer online save comes down without a question, a save changed on both
// devices asks which one to keep. Offline, slow or skipped, the game starts as it is. A gate
// of the player (den/app/player_hooks.h). The check begins with the boot screen, as the game is
// asked for, and says there what it waits for; only a question shows its own panel.

#include <algorithm>
#include <ctime>
#include <format>

#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/app/boot.h"
#include "den/app/game_widgets.h"
#include "den/app/games.h"
#include "den/app/glyphs.h"
#include "den/app/pages.h"
#include "den/app/player_hooks.h"
#include "den/app/sync_glue.h"
#include "den/base/log.h"

namespace den::app {

namespace {

constexpr double CHECK_SECONDS = 3.0;
// From Home's Continue the moment it was left on another device is what the user asked for:
// it may take a while to come down (Skip starts the game as it is here).
constexpr double CONTINUE_SECONDS = 30.0;
// A newer save that is coming down is waited for, with its progress.
constexpr double DOWNLOAD_SECONDS = 15.0;
// A check begun with the boot screen is the player's when it opens within this time.
constexpr double EARLY_SECONDS = 5.0;
constexpr float ROW_HEIGHT = 52;

// "Today, 14:05": the day as the library shows it, and the time within the last month.
std::string When(std::int64_t unix_time) {
    std::string text = SyncTimeText(unix_time);
    const std::time_t time = static_cast<std::time_t>(unix_time);
    const std::tm* local = unix_time > 0 ? std::localtime(&time) : nullptr;
    if (local != nullptr && std::time(nullptr) - time < 30 * 86400) {
        text += std::format(", {:02}:{:02}", local->tm_hour, local->tm_min);
    }
    return text;
}

// What the check job and the page share.
struct Check {
    sync::CheckGate gate;
    sync::GameCheck result;
    Status resolved;
};

JobPtr RunCheck(sync::Engine& engine, const std::shared_ptr<Check>& check, const std::string& id, double seconds) {
    const double asked = pal::NowSeconds();
    return Svc().jobs->Run("check saves", [engine = &engine, check, id, seconds, asked](Job&) {
        check->result = engine->CheckGame(id, check->gate, seconds);
        log::Info("sync", "save check of {}: {:.2f} s{}", id, pal::NowSeconds() - asked,
                  check->gate.Abandoned() ? " (given up)" : "");
        return Status();
    });
}

// The check begun as the game was asked for, before the player screen opened.
struct Early {
    std::string game_id;
    std::shared_ptr<Check> check;
    JobPtr job;
    double started = 0;
};

Early& Pending() {
    static Early early;
    return early;
}

void DropEarly() {
    Early& early = Pending();
    if (early.check) {
        early.check->gate.Abandon();
    }
    early = Early{};
}

class SaveCheck final : public PlayerGate {
public:
    SaveCheck(sync::Engine& engine, const library::Entry& game, const PlayerStart& start)
        : engine_(engine), game_(game), from_continue_(start.Param("continue") == "1"),
          from_(start.Param("from")) {
        started_ = pal::NowSeconds();
        seconds_ = from_continue_ ? CONTINUE_SECONDS : CHECK_SECONDS;
        Early& early = Pending();
        if (early.job && early.game_id == game_.id && started_ - early.started < EARLY_SECONDS) {
            check_ = early.check;
            job_ = early.job;
            started_ = early.started; // the time it is given counts from the press
            early = Early{};
        } else {
            DropEarly();
            job_ = RunCheck(engine_, check_, game_.id, from_continue_ ? CONTINUE_SECONDS : DOWNLOAD_SECONDS);
        }
    }

    Step Tick() override {
        if (done_) {
            return Step::Done;
        }
        const bool finished = job_ && job_->Finished();
        if (resolving_) {
            if (finished) {
                if (!check_->resolved.ok) {
                    Svc().stage->Toast(check_->resolved.error, 5.0f);
                }
                return Step::Done;
            }
            return Took();
        }
        if (choosing_) {
            return Took();
        }
        if (!finished) {
            const sync::GameTransfer transfer = engine_.GameProgress(true);
            const bool coming = transfer.game_id == game_.id && transfer.total > 0;
            ShowProgress(transfer);
            TellBoot(transfer, coming);
            if (pal::NowSeconds() - started_ >= (coming && !from_continue_ ? DOWNLOAD_SECONDS : seconds_)) {
                check_->gate.Abandon(); // offline or slow: the game starts with what it has
                log::Info("player", "save check for {} took too long; starting", game_.name);
                return Step::Done;
            }
            return Took();
        }
        const sync::GameCheck& result = check_->result;
        if (!result.error.empty()) {
            log::Info("player", "save check for {}: {}", game_.name, result.error);
        }
        if (!result.conflicts.empty()) {
            BootStepAside(); // the question is the user's: its panel is shown
            choosing_ = true;
            focus_ = "sync.online";
            return Step::Rebuild;
        }
        if (result.downloaded > 0 && !from_continue_) {
            Svc().stage->Toast("Using your newer save from online");
        }
        return Step::Done;
    }

    void Build(ui::Node& root, std::string& focus) override {
        progress_ = nullptr; // the nodes of the last build are gone
        bar_ = nullptr;
        if (!focus_.empty()) {
            focus = std::move(focus_);
            focus_.clear();
        }
        const ui::Theme& theme = *Svc().theme;
        root.axis = ui::Axis::Column;
        root.justify = ui::Align::Center;
        root.align = ui::Align::Center;
        ui::Node* panel = root.Add(ui::MakeBox(ui::Axis::Column, 10));
        panel->width = 820;
        panel->background = theme.dialog;
        panel->SetPadding(28, 28);
        const auto line = [&](const std::string& text, bool dim) {
            auto* label = panel->Emplace<ui::Label>(text, theme.text_body);
            if (dim) {
                label->tone = ui::Tone::Dim;
            }
        };
        if (!choosing_) {
            panel->Emplace<ui::Label>(game_.name, theme.text_heading, gfx::FONT_BOLD);
            const bool picking_up = from_continue_ && !resolving_;
            line(resolving_    ? "Getting your save ready\xE2\x80\xA6"
                 : picking_up ? "Getting where you left off" + (from_.empty() ? std::string() : " on " + from_) +
                                    "\xE2\x80\xA6"
                              : "Checking for newer saves\xE2\x80\xA6",
                 true);
            if (picking_up) {
                progress_ = panel->Emplace<ui::Label>(" ", theme.text_small);
                progress_->tone = ui::Tone::Dim;
                bar_ = panel->Emplace<ProgressBar>();
                bar_->pad_bottom = 6;
            }
            if (!resolving_) {
                ui::Node* list = panel->Add(ui::MakeBox(ui::Axis::Column, 6));
                AddRows(list, {ActionRow("sync.skip", "Skip", [this] { Skip(); })}, ROW_HEIGHT);
            }
            return;
        }
        const sync::GameCheck& result = check_->result;
        const std::string device = result.online_device.empty() ? "another device" : result.online_device;
        panel->Emplace<ui::Label>("Which save do you want?", theme.text_heading, gfx::FONT_BOLD);
        line(TrF("{} was played on {}", game_.name, device), true);
        line("and on this device since they last synced.", true);
        line(TrF("Online: {}, {}", device, When(result.online_time)), false);
        line(TrF("This device: {}", When(result.local_time)), false);
        auto* note = panel->Emplace<ui::Label>("The other one is kept under Settings > Sync > Conflicts.",
                                               theme.text_small);
        note->tone = ui::Tone::Dim;
        note->pad_bottom = 6;
        ui::Node* list = panel->Add(ui::MakeBox(ui::Axis::Column, 6));
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

    void Abandon() override {
        check_->gate.Abandon();
    }

private:
    Step Took() {
        if (rebuild_) {
            rebuild_ = false;
            return Step::Rebuild;
        }
        return Step::Waiting;
    }

    void Skip() {
        check_->gate.Abandon();
        done_ = true;
    }

    void Choose(bool online) {
        if (!choosing_) {
            return;
        }
        choosing_ = false;
        resolving_ = true;
        job_ = Svc().jobs->Run("choose save", [engine = &engine_, check = check_, id = game_.id, online](Job&) {
            check->resolved = engine->ResolveGame(id, online);
            return Status();
        });
        rebuild_ = true;
    }

    // Under the boot screen: what is waited for, there.
    void TellBoot(const sync::GameTransfer& transfer, bool coming) {
        if (!BootCovers()) {
            return;
        }
        BootWait wait;
        if (from_continue_) {
            wait.text = "Getting where you left off" + (from_.empty() ? std::string() : " on " + from_) + "\xE2\x80\xA6";
        } else {
            wait.text = coming ? "Getting your newer save\xE2\x80\xA6" : "Checking for newer saves\xE2\x80\xA6";
        }
        if (coming) {
            const float part = transfer.fraction >= 0 ? transfer.fraction : 0.0f;
            wait.fraction = std::clamp(
                (static_cast<float>(transfer.done) + part) / static_cast<float>(transfer.total), 0.0f, 1.0f);
        }
        wait.skip = [this] { Skip(); };
        SetBootWait(std::move(wait));
    }

    void ShowProgress(const sync::GameTransfer& transfer) {
        if (progress_ == nullptr || bar_ == nullptr) {
            return;
        }
        if (transfer.game_id != game_.id || transfer.total == 0) {
            progress_->text = "Looking online\xE2\x80\xA6";
            bar_->fraction = -1;
            return;
        }
        const float part = transfer.fraction >= 0 ? transfer.fraction : 0.0f;
        bar_->fraction = std::clamp(
            (static_cast<float>(transfer.done) + part) / static_cast<float>(transfer.total), 0.0f, 1.0f);
        progress_->text = std::format("{} of {}", std::min(transfer.done + 1, transfer.total), transfer.total) +
                          (transfer.size > 0 ? " \xC2\xB7 " + SizeText(transfer.size) : std::string());
    }

    sync::Engine& engine_;
    library::Entry game_;
    std::shared_ptr<Check> check_ = std::make_shared<Check>();
    JobPtr job_;
    double started_ = 0;
    double seconds_ = CHECK_SECONDS;
    bool from_continue_ = false;
    std::string from_;
    bool choosing_ = false, resolving_ = false, done_ = false, rebuild_ = false;
    std::string focus_;
    ui::Label* progress_ = nullptr; // shown while a Continue check brings files down
    ProgressBar* bar_ = nullptr;
};

PlayerHooks SyncHooks() {
    PlayerHooks hooks;
    hooks.gate = [](const PlayerStart& start, const plugins::CoreInfo& core) -> std::unique_ptr<PlayerGate> {
        sync::Engine* engine = SyncSvc();
        // An engine app keeps its own saves; only the player's go through sync.
        if (engine == nullptr || core.runner == plugins::Runner::Engine || !engine->SyncsSaves()) {
            return nullptr;
        }
        return std::make_unique<SaveCheck>(*engine, start.game, start);
    };
    hooks.asked = [](const library::Entry& game, const plugins::CoreInfo& core, const ui::Params& params) {
        sync::Engine* engine = SyncSvc();
        if (engine == nullptr || core.runner == plugins::Runner::Engine || !engine->SyncsSaves()) {
            return;
        }
        DropEarly();
        const auto from_continue = params.find("continue");
        const bool picking_up = from_continue != params.end() && from_continue->second == "1";
        Early& early = Pending();
        early.game_id = game.id;
        early.check = std::make_shared<Check>();
        early.started = pal::NowSeconds();
        early.job = RunCheck(*engine, early.check, game.id, picking_up ? CONTINUE_SECONDS : DOWNLOAD_SECONDS);
    };
    return hooks;
}

DEN_REGISTER_PLAYER_HOOKS(SyncHooks());

} // namespace

} // namespace den::app
