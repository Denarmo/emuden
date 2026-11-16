// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Before a game starts, its saves are checked against the sync provider (docs/sync.md,
// section 3.3): a newer online save comes down without a question, a save changed on both
// devices asks which one to keep. Offline, slow or skipped, the game starts as it is.

#include <algorithm>
#include <ctime>
#include <format>

#include "../home/game_widgets.h"
#include "../details/game_prefs.h"
#include "../home/games.h"
#include "../sync/sync_glue.h"
#include "den/app/pages.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/states/gba.h"
#include "den/sync/continue.h"
#include "glyphs.h"
#include "player_screen.h"

namespace den::app {

namespace {

constexpr double CHECK_SECONDS = 3.0;
// From Home's Continue the moment it was left on another device is what the user asked for:
// it may take a while to come down (Skip starts the game as it is here).
constexpr double CONTINUE_SECONDS = 30.0;
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

// Whether the running core takes over another GBA core's resume state by converting it
// (docs/gba-states.md): then the game keeps its own core.
bool ResumeConverts(const std::filesystem::path& file, const std::string& wrote, const std::string& own) {
    if (!states::CanConvertGbaState(wrote, own)) {
        return false;
    }
    const auto data = ReadBinaryFile(file);
    const std::size_t header = 9 + wrote.size();
    if (!data || data->size() <= header) {
        return false;
    }
    const std::string why = states::GbaStateNotPortable(wrote, std::span(*data).subspan(header));
    if (!why.empty()) {
        log::Info("player", "the {} resume state does not convert: {}", wrote, why);
    }
    return why.empty();
}

} // namespace

struct PlayerScreen::SaveCheck {
    sync::CheckGate gate;
    sync::GameCheck result;
    JobPtr job;
    double started = 0;
    double seconds = CHECK_SECONDS;
    bool from_continue = false;
    ui::Label* progress = nullptr; // shown while a Continue check brings files down
    ProgressBar* bar = nullptr;
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
    check->from_continue = Param("continue") == "1";
    check->seconds = check->from_continue ? CONTINUE_SECONDS : CHECK_SECONDS;
    check->job = Svc().jobs->Run("check saves", [engine, check, id = game_.id](Job&) {
        check->result = engine->CheckGame(id, check->gate, check->seconds);
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
        ShowCheckProgress(*check);
        if (pal::NowSeconds() - check->started >= check->seconds) {
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
    if (result.downloaded > 0 && !check->from_continue) {
        GetStage().Toast("Using your newer save from online");
    }
    check_ = nullptr;
    Proceed();
}

void PlayerScreen::ShowCheckProgress(SaveCheck& check) {
    sync::Engine* engine = SyncSvc();
    if (check.progress == nullptr || check.bar == nullptr || engine == nullptr) {
        return;
    }
    const sync::GameTransfer transfer = engine->GameProgress(true);
    if (transfer.game_id != game_.id || transfer.total == 0) {
        check.progress->text = "Looking online\xE2\x80\xA6";
        check.bar->fraction = -1;
        return;
    }
    const float part = transfer.fraction >= 0 ? transfer.fraction : 0.0f;
    check.bar->fraction = std::clamp((static_cast<float>(transfer.done) + part) / static_cast<float>(transfer.total),
                                     0.0f, 1.0f);
    check.progress->text = std::format("{} of {}", std::min(transfer.done + 1, transfer.total), transfer.total) +
                           (transfer.size > 0 ? " \xC2\xB7 " + SizeText(transfer.size) : std::string());
}

void PlayerScreen::MatchResumeCore() {
    if (core_.runner != plugins::Runner::Core || Param("resume") == "0" || !Param("core").empty()) {
        return;
    }
    sync::ResumeCoreInput input;
    input.own = core_.name;
    const std::filesystem::path resume = sync::ResumeState(Svc().data_dir, game_.system, game_.id);
    input.wrote = sync::StateCore(resume);
    if (!input.wrote.empty() && !EqualsNoCase(input.wrote, core_.name) && ResumeConverts(resume, input.wrote, core_.name)) {
        log::Info("player", "the resume state of {} is {}'s: {} converts it", game_.name, input.wrote, core_.name);
        return;
    }
    std::optional<plugins::CoreInfo> other;
    if (!input.wrote.empty() && !EqualsNoCase(input.wrote, core_.name)) {
        input.own_for_game = GameHasOwnCore(game_);
        for (const plugins::CoreInfo& installed : Svc().plugins->CoresFor(game_.system)) {
            if (installed.Ready() && EqualsNoCase(installed.name, input.wrote)) {
                other = CoreForGame(game_, installed.id);
                input.wrote_installed = other.has_value();
                break;
            }
        }
    }
    switch (sync::ChooseResumeCore(input)) {
    case sync::ResumeCoreChoice::Same:
        return;
    case sync::ResumeCoreChoice::Switch:
        log::Info("player", "the resume state of {} is {}'s: this time it starts with it", game_.name, input.wrote);
        core_ = *other;
        return;
    case sync::ResumeCoreChoice::Unusable:
        // A core the game asks for (GBA DS and its second screen) is kept: the game starts
        // from its save.
        log::Info("player", "the resume state of {} is {}'s: {} plays it, from its save", game_.name, input.wrote,
                  core_.name);
        GetStage().Toast(std::format("Where you left off was saved with {}, which {} cannot load: starting from "
                                     "the game's save",
                                     input.wrote, core_.name),
                         5.0f);
        return;
    case sync::ResumeCoreChoice::Missing:
        log::Info("player", "the resume state of {} is {}'s, which is not installed here", game_.name, input.wrote);
        return;
    }
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
    if (check_) {
        check_->progress = nullptr; // the nodes of the last build are gone
        check_->bar = nullptr;
    }
    const ui::Theme& theme = *Svc().theme;
    ui::Node& root = Root();
    root.axis = ui::Axis::Column;
    root.justify = ui::Align::Center;
    root.align = ui::Align::Center;
    ui::Node* panel = root.Add(ui::MakeBox(ui::Axis::Column, 10));
    panel->width = 820;
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
        const std::string from = Param("from");
        const bool picking_up = check_ && check_->from_continue && !resolving;
        line(resolving    ? "Getting your save ready\xE2\x80\xA6"
             : picking_up ? "Getting where you left off" + (from.empty() ? std::string() : " on " + from) + "\xE2\x80\xA6"
                          : "Checking for newer saves\xE2\x80\xA6",
             true);
        if (picking_up) {
            check_->progress = panel->Emplace<ui::Label>(" ", theme.text_small);
            check_->progress->tone = ui::Tone::Dim;
            check_->bar = panel->Emplace<ProgressBar>();
            check_->bar->pad_bottom = 6;
        }
        if (!resolving) {
            ui::Node* list = panel->Add(ui::MakeBox(ui::Axis::Column, 6));
            AddRows(list, {ActionRow("sync.skip", "Skip", [this] { SkipSaveCheck(); })}, ROW_HEIGHT);
        }
        return;
    }
    const sync::GameCheck& result = check_->result;
    const std::string device = result.online_device.empty() ? "another device" : result.online_device;
    panel->Emplace<ui::Label>("Which save do you want?", theme.text_heading, gfx::FONT_BOLD);
    line(game_.name + " was played on " + device, true);
    line("and on this device since they last synced.", true);
    line("Online: " + device + ", " + When(result.online_time), false);
    line("This device: " + When(result.local_time), false);
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

} // namespace den::app
