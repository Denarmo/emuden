// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Keeps system::AppBeside in step with the device: every frame it is told which display the
// game is on and which of this app's surfaces are shown, takes the platform's events (the
// handle, Back, Home), and does what the state asks for (open, raise, lower, the handle,
// hold the game, write its saves).

#include "../player/player_screen.h"
#include "apps_catalog.h"
#include "beside.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/log.h"
#include "den/platform/apps.h"
#include "den/platform/lifecycle.h"
#include "den/platform/second_display.h"
#include "den/platform/text_input.h"
#include "den/system/device.h"

namespace den::app {

namespace {

using system::BesideAction;
using system::BesideActions;
using Screen = system::BesideScreen;
using Kind = system::BesideAction::Kind;

system::AppBeside& Machine() {
    static system::AppBeside machine;
    return machine;
}

pal::apps::Display DisplayOf(Screen screen) {
    return screen == Screen::Second ? pal::apps::Display::Second : pal::apps::Display::Main;
}

void Run(const BesideActions& actions, ui::Stage* stage = nullptr);

void Open(const BesideAction& action, ui::Stage& stage, BesideActions& then) {
    system::AppBeside& machine = Machine();
    const std::string label = machine.App().id == action.app ? machine.App().label : action.app;
    if (AppsFake()) {
        // The desktop's made-up apps: nothing opens, the quick settings act as pulled over it.
        stage.Toast("Would open " + label);
        then = machine.PullSettings();
        return;
    }
    const bool two = pal::GetSecondDisplay().Present();
    const pal::apps::Display where = two ? DisplayOf(action.screen) : pal::apps::Display::Default;
    const Status opened = pal::apps::Launch(action.app, where);
    if (!opened) {
        stage.Toast(opened.error.empty() ? "The app could not be opened" : opened.error, 5.0f);
        then = machine.LaunchFailed();
        return;
    }
    for (const std::string& note : opened.notes) {
        stage.Toast(note, 5.0f);
    }
    if (!opened.notes.empty() && where == pal::apps::Display::Second) {
        then = machine.LaunchedOnGameScreen(); // it opened over the game after all
    }
    log::Info("apps", "opened {} on the {} display", action.app, action.screen == Screen::Second ? "second" : "main");
}

void Run(const BesideActions& actions, ui::Stage* stage) {
    Services& svc = Svc();
    system::AppBeside& machine = Machine();
    PlayerScreen* screen = PlayerScreen::Current();
    BesideActions then;
    for (const BesideAction& action : actions) {
        switch (action.kind) {
        case Kind::Launch:
            Open(action, stage ? *stage : *svc.stage, then);
            break;
        case Kind::Raise:
            if (!AppsFake()) {
                pal::apps::Raise(DisplayOf(action.screen));
            }
            break;
        case Kind::Lower:
            if (!AppsFake()) {
                pal::apps::Lower(DisplayOf(action.screen));
            } else {
                svc.stage->Toast("Would go back to " + machine.App().label);
                then = machine.PullSettings();
            }
            break;
        case Kind::ShowHandle:
            machine.SetHandle(!AppsFake() && pal::apps::ShowHandle(DisplayOf(action.screen)));
            break;
        case Kind::HideHandle:
            pal::apps::HideHandle();
            break;
        case Kind::OpenMenu:
            if (screen) {
                screen->Pause();
            }
            break;
        case Kind::SaveGame:
            if (screen) {
                svc.player->FlushSaves();
                svc.player->SaveResumeState();
            }
            break;
        case Kind::HoldGame:
        case Kind::ReleaseGame:
            if (screen) {
                screen->Hold(action.kind == Kind::HoldGame);
            }
            break;
        }
    }
    if (!then.empty()) {
        Run(then, stage);
    }
}

void Frame() {
    if (!AppsAvailable() || !Svc().stage) {
        return;
    }
    system::AppBeside& machine = Machine();
    pal::SecondDisplay& second = pal::GetSecondDisplay();
    const PlayerScreen* screen = PlayerScreen::Current();
    machine.SetTwoDisplays(second.Present());
    Run(machine.SetGame(screen != nullptr, screen && screen->Swapped() ? Screen::Second : Screen::Main));
    Run(machine.SetSurfaces(!pal::MainWindowHidden(), second.Ready()));
    const double now = pal::NowSeconds();
    for (const pal::apps::Event& event : pal::apps::TakeEvents()) {
        switch (event.kind) {
        case pal::apps::Event::Kind::Handle:
            Run(machine.PullSettings());
            break;
        case pal::apps::Event::Kind::Back: {
            BesideActions out;
            const Screen where = event.display == pal::apps::Display::Second ? Screen::Second : Screen::Main;
            if (machine.Back(where, out)) {
                Run(out);
            } else {
                // The state moved on since the press: it is the app's own Back after all.
                Svc().input->Inject(input::Event::MakeNav(input::Nav::Back));
            }
            break;
        }
        case pal::apps::Event::Kind::Home:
            machine.Home(now);
            break;
        }
    }
    Run(machine.Tick(now));
    pal::apps::CatchBack(pal::apps::Display::Main, machine.CatchesBack(Screen::Main));
    pal::apps::CatchBack(pal::apps::Display::Second, machine.CatchesBack(Screen::Second));
}

} // namespace

const system::AppBeside& Beside() {
    return Machine();
}

void BesideOpen(const system::AppItem& app, ui::Stage& stage) {
    Run(Machine().Launch({app.id, app.label, app.icon}), &stage);
}

void BesideBackToApp() {
    Run(Machine().BackToApp());
}

void BesideClose() {
    Run(Machine().CloseApp());
}

void BesidePullSettings() {
    Run(Machine().PullSettings());
}

bool PadWithOtherApp() {
    return Machine().AppHasKeys() && !pal::apps::HasInputFocus() && !pal::GetTextInput().Active();
}

std::string ScreenName(Screen screen) {
    const bool dual = system::Device().form == system::FormFactor::HandheldDual;
    if (screen == Screen::Main) {
        return dual ? "Top screen" : "Main screen";
    }
    return dual ? "Bottom screen" : "Second screen";
}

DEN_REGISTER_FRAME(Frame);

} // namespace den::app
