// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Before the game starts: what other features hold it for (den/app/player_hooks.h gates,
// sync's check for newer saves), and the core its resume state asks for. A resume state
// written by another core (another device plays this console with another one) makes this
// launch use that core when it is installed here, or keep its own when it converts the state
// (docs/gba-states.md).

#include <format>

#include "den/app/game_prefs.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/states/gba.h"
#include "den/sync/continue.h"
#include "player_screen.h"

namespace den::app {

namespace {

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


bool PlayerScreen::StartGate() {
    const PlayerStart start{game_, *this};
    for (const PlayerHooks& hooks : AllPlayerHooks()) {
        if (hooks.gate) {
            if (std::unique_ptr<PlayerGate> gate = hooks.gate(start, core_)) {
                gate_ = std::move(gate);
                phase_ = Phase::Gated; // Build, which called Open, shows it
                return true;
            }
        }
    }
    return false;
}

void PlayerScreen::TickGate() {
    if (!gate_ || phase_ != Phase::Gated) {
        return;
    }
    switch (gate_->Tick()) {
    case PlayerGate::Step::Waiting:
        break;
    case PlayerGate::Step::Rebuild:
        Rebuild();
        break;
    case PlayerGate::Step::Done:
        gate_.reset();
        Proceed();
        break;
    }
}

void PlayerScreen::AbandonGate() {
    if (gate_) {
        gate_->Abandon();
        gate_.reset();
    }
}

} // namespace den::app
