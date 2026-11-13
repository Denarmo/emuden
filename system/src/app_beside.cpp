// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/system/app_beside.h"

namespace den::system {

namespace {

using Kind = BesideAction::Kind;

constexpr double HOME_SETTLE = 0.5;  // seconds until a Home press shows which display it was on
constexpr double RAISE_AFTER = 0.4;  // seconds the quick settings may be gone before they are called back
constexpr double RAISE_AGAIN = 1.5;  // between tries
constexpr int RAISE_TRIES = 3;

} // namespace

void AppBeside::SetTwoDisplays(bool two) {
    two_ = two;
}

void AppBeside::SetHandle(bool shown) {
    handle_ = shown && stage_ == Stage::AppFront;
}

void AppBeside::Forget(BesideActions& out) {
    if (stage_ != Stage::None) {
        out.push_back({Kind::HideHandle, app_screen_, {}});
    }
    stage_ = Stage::None;
    app_ = {};
    handle_ = false;
    raise_pending_ = lower_pending_ = false;
    home_at_ = -1;
}

BesideActions AppBeside::SetGame(bool running, BesideScreen screen) {
    BesideActions out;
    if (running == running_ && (screen == game_screen_ || !running)) {
        return out;
    }
    // The app was beside that game on that display: neither is so any more.
    Forget(out);
    running_ = running;
    game_screen_ = screen;
    if (!running) {
        held_ = false;
    } else if (held_ && Shown(screen)) {
        held_ = false;
        out.push_back({Kind::ReleaseGame, screen, {}});
    } else if (!held_ && !Shown(screen)) {
        held_ = true;
        out.push_back({Kind::HoldGame, screen, {}});
        out.push_back({Kind::SaveGame, screen, {}});
    }
    return out;
}

BesideActions AppBeside::Launch(const BesideApp& app) {
    BesideActions out;
    if (!running_) {
        out.push_back({Kind::Launch, BesideScreen::Main, app.id});
        return out;
    }
    Forget(out);
    app_ = app;
    stage_ = Stage::AppFront;
    if (!two_) {
        // One display: the app takes it and the game waits in its menu.
        app_screen_ = BesideScreen::Main;
        out.push_back({Kind::OpenMenu, BesideScreen::Main, {}});
        out.push_back({Kind::SaveGame, BesideScreen::Main, {}});
        out.push_back({Kind::Launch, BesideScreen::Main, app.id});
        return out;
    }
    app_screen_ = SettingsScreen();
    out.push_back({Kind::SaveGame, game_screen_, {}});
    out.push_back({Kind::Launch, app_screen_, app.id});
    out.push_back({Kind::ShowHandle, app_screen_, {}});
    return out;
}

BesideActions AppBeside::LaunchFailed() {
    BesideActions out;
    if (stage_ == Stage::AppFront) {
        Forget(out);
    }
    return out;
}

BesideActions AppBeside::LaunchedOnGameScreen() {
    BesideActions out;
    if (stage_ == Stage::AppFront && app_screen_ != game_screen_) {
        // It covers the game: the game is held when its surface goes, and comes back with it.
        app_screen_ = game_screen_;
        handle_ = false;
        out.push_back({Kind::HideHandle, Other(game_screen_), {}});
    }
    return out;
}

BesideActions AppBeside::PullSettings() {
    BesideActions out;
    switch (stage_) {
    case Stage::AppFront:
        if (app_screen_ == game_screen_) {
            break; // the app covers the game itself: leaving it is the way back
        }
        stage_ = Stage::SettingsOver;
        handle_ = false;
        raise_pending_ = true;
        raise_screen_ = app_screen_;
        out.push_back({Kind::HideHandle, app_screen_, {}});
        out.push_back({Kind::Raise, app_screen_, {}});
        break;
    case Stage::None:
        if (two_ && running_) {
            raise_pending_ = !Shown(SettingsScreen());
            raise_screen_ = SettingsScreen();
            out.push_back({Kind::Raise, SettingsScreen(), {}});
        }
        break;
    case Stage::SettingsOver:
        break;
    }
    return out;
}

BesideActions AppBeside::BackToApp() {
    BesideActions out;
    if (stage_ != Stage::SettingsOver) {
        return out;
    }
    stage_ = Stage::AppFront;
    lower_pending_ = true;
    raise_pending_ = false;
    out.push_back({Kind::Lower, app_screen_, app_.id});
    out.push_back({Kind::ShowHandle, app_screen_, {}});
    return out;
}

BesideActions AppBeside::CloseApp() {
    BesideActions out;
    const Stage was = stage_;
    const BesideScreen screen = app_screen_;
    Forget(out);
    if (was == Stage::AppFront && two_ && screen != game_screen_) {
        raise_pending_ = true;
        raise_screen_ = screen;
        out.push_back({Kind::Raise, screen, {}});
    }
    return out;
}

bool AppBeside::Back(BesideScreen screen, BesideActions& out) {
    if (!CatchesBack(screen)) {
        return false;
    }
    out = BackToApp();
    return true;
}

void AppBeside::Home(double now) {
    home_at_ = now;
}

void AppBeside::Lost(BesideScreen screen, BesideActions& out) {
    if (running_ && screen == game_screen_ && !held_) {
        held_ = true;
        out.push_back({Kind::HoldGame, screen, {}});
        out.push_back({Kind::SaveGame, screen, {}});
    }
    if (stage_ == Stage::AppFront && screen == app_screen_) {
        lower_pending_ = false; // the app is over it, as asked
        return;
    }
    if (stage_ == Stage::SettingsOver && screen == app_screen_) {
        // Something else took the display from the quick settings (Home): the app is no
        // longer what lies under them.
        Forget(out);
    }
    if (stage_ == Stage::None && two_ && screen == BesideScreen::Second) {
        lost_noted_ = true;
        raises_ = 0;
    }
}

void AppBeside::Returned(BesideScreen screen, BesideActions& out) {
    if (held_ && screen == game_screen_) {
        held_ = false;
        out.push_back({Kind::ReleaseGame, screen, {}});
    }
    if (screen == BesideScreen::Second) {
        lost_at_ = -1;
        lost_noted_ = false;
        raises_ = 0;
    }
    if (raise_pending_ && screen == raise_screen_) {
        raise_pending_ = false;
        return;
    }
    if (stage_ == Stage::AppFront && screen == app_screen_) {
        Forget(out); // the app was left (Back, or it closed): the quick settings are there again
    }
}

BesideActions AppBeside::SetSurfaces(bool main_shown, bool second_shown) {
    BesideActions out;
    const bool main_was = main_shown_, second_was = second_shown_;
    main_shown_ = main_shown;
    second_shown_ = second_shown;
    if (main_was != main_shown) {
        main_shown ? Returned(BesideScreen::Main, out) : Lost(BesideScreen::Main, out);
    }
    if (second_was != second_shown) {
        second_shown ? Returned(BesideScreen::Second, out) : Lost(BesideScreen::Second, out);
    }
    return out;
}

BesideActions AppBeside::Tick(double now) {
    BesideActions out;
    if (lost_noted_) {
        lost_noted_ = false;
        lost_at_ = now;
    }
    if (home_at_ >= 0 && now - home_at_ >= HOME_SETTLE) {
        home_at_ = -1;
        // Home on the game's display stops the game's surface; it is still shown, so Home
        // was pressed on the app, which is gone from its display now.
        if (stage_ == Stage::AppFront && running_ && two_ && app_screen_ != game_screen_ &&
            Shown(game_screen_) && !Shown(app_screen_)) {
            const BesideScreen screen = app_screen_;
            Forget(out);
            if (screen == BesideScreen::Second && main_shown_) {
                raise_pending_ = true;
                raise_screen_ = screen;
                out.push_back({Kind::Raise, screen, {}});
            }
        }
    }
    // The quick settings went from the second display with no app of ours over them (Home
    // there): they come back by themselves while this app is in front on the main one.
    if (lost_at_ >= 0 && stage_ == Stage::None && two_ && !second_shown_ && main_shown_ &&
        raises_ < RAISE_TRIES && now >= lost_at_ + RAISE_AFTER + RAISE_AGAIN * raises_) {
        ++raises_;
        raise_pending_ = true;
        raise_screen_ = BesideScreen::Second;
        out.push_back({Kind::Raise, BesideScreen::Second, {}});
    }
    return out;
}

bool AppBeside::WantsTab() const {
    if (!two_ || !running_ || held_ || handle_ || !Shown(game_screen_) || Shown(SettingsScreen())) {
        return false;
    }
    return (stage_ == Stage::AppFront && app_screen_ != game_screen_) || SettingsScreen() == BesideScreen::Main;
}

} // namespace den::system
