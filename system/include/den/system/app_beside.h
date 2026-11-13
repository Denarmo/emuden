// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Another app open beside a running game on a device with two displays: which display has
// the game, which the quick settings, where the app is, and what has to happen when the user
// opens an app, pulls the quick settings over it, goes back to it or closes it, and when one
// of this app's own surfaces is covered or comes back. Pure state: every transition returns
// what the platform and the player have to do, so it is tested without either.

#pragma once

#include <string>
#include <vector>

namespace den::system {

/// A display, seen from this app: the one its main window is on, and the other one.
enum class BesideScreen { Main, Second };

inline BesideScreen Other(BesideScreen screen) {
    return screen == BesideScreen::Main ? BesideScreen::Second : BesideScreen::Main;
}

struct BesideApp {
    std::string id, label, icon;
};

/// One thing to do, in the order given.
struct BesideAction {
    enum class Kind {
        Launch,      // open `app` on `screen`
        Raise,       // this app's own window on `screen` comes to the front of it
        Lower,       // this app's own window on `screen` goes behind the app under it
        ShowHandle,  // the small tab over the other app on `screen`
        HideHandle,
        OpenMenu,    // pause the game with its menu (the app takes the game's only display)
        SaveGame,    // battery save and resume state on disk, in case the process ends
        HoldGame,    // the game's display is covered: pause without a menu
        ReleaseGame, // it is back: go on where HoldGame stopped
    };
    Kind kind = Kind::Launch;
    BesideScreen screen = BesideScreen::Main;
    std::string app;

    bool operator==(const BesideAction&) const = default;
};

using BesideActions = std::vector<BesideAction>;

class AppBeside {
public:
    enum class Stage {
        None,         // no app beside the game
        AppFront,     // the app covers the quick settings
        SettingsOver, // the quick settings were pulled over the app, which is still under them
    };

    // What is there. None of these asks for anything by itself except where noted.
    /// The device has a second display this app draws on.
    void SetTwoDisplays(bool two);
    /// A game runs, and the display it is drawn on. A game that ends or changes display
    /// while an app is open ends the side-by-side state (the handle goes).
    BesideActions SetGame(bool running, BesideScreen screen);
    /// The platform could show the handle over the other app (ShowHandle's answer).
    void SetHandle(bool shown);

    // What the user does.
    /// An app chosen in the quick settings or the pause menu while the game runs.
    BesideActions Launch(const BesideApp& app);
    /// The platform could not open it.
    BesideActions LaunchFailed();
    /// It opened, but on the game's display (the other one takes no apps).
    BesideActions LaunchedOnGameScreen();
    /// The handle, or the tab on the game's display: the quick settings over the app.
    BesideActions PullSettings();
    /// "Back to <app>": the quick settings go away again and the app has its display.
    BesideActions BackToApp();
    /// "Close <app>": plain quick settings; the app is no longer beside the game.
    BesideActions CloseApp();
    /// System Back on a display. True when it was used (the quick settings over an app go
    /// back to the app); `out` gets what to do.
    bool Back(BesideScreen screen, BesideActions& out);
    /// The system's Home was pressed somewhere. Decided a moment later in Tick: when the
    /// game is still shown then, Home was on the app's display and the app is gone from it.
    void Home(double now);

    // What the system does.
    /// Which of this app's own surfaces are shown (an activity stopped or started again).
    BesideActions SetSurfaces(bool main_shown, bool second_shown);
    /// Timed work: the quick settings that Home sent away come back by themselves.
    BesideActions Tick(double now);

    Stage GetStage() const {
        return stage_;
    }
    /// The app beside the game; empty id when there is none.
    const BesideApp& App() const {
        return app_;
    }
    BesideScreen GameScreen() const {
        return game_screen_;
    }
    /// The display of the quick settings (and of an app opened beside the game).
    BesideScreen SettingsScreen() const {
        return two_ ? Other(game_screen_) : BesideScreen::Main;
    }
    BesideScreen AppScreen() const {
        return app_screen_;
    }
    bool GameHeld() const {
        return held_;
    }
    bool HandleShown() const {
        return handle_;
    }
    /// System Back on that display belongs to Back() right now.
    bool CatchesBack(BesideScreen screen) const {
        return stage_ == Stage::SettingsOver && screen == app_screen_;
    }
    /// The app has the keys: it is in front on a display that is not the game's.
    bool AppHasKeys() const {
        return stage_ == Stage::AppFront && two_ && app_screen_ != game_screen_;
    }
    /// The game's display should show a tab for the quick settings: they are covered and
    /// nothing of ours is over what covers them.
    bool WantsTab() const;

private:
    bool Shown(BesideScreen screen) const {
        return screen == BesideScreen::Main ? main_shown_ : second_shown_;
    }
    void Forget(BesideActions& out);
    void Lost(BesideScreen screen, BesideActions& out);
    void Returned(BesideScreen screen, BesideActions& out);

    bool two_ = false;
    bool running_ = false;
    BesideScreen game_screen_ = BesideScreen::Main;
    Stage stage_ = Stage::None;
    BesideApp app_;
    BesideScreen app_screen_ = BesideScreen::Second;
    bool main_shown_ = true, second_shown_ = false;
    bool handle_ = false;
    bool held_ = false;
    bool raise_pending_ = false; // Raise was asked: the surface coming back is our doing
    BesideScreen raise_screen_ = BesideScreen::Second;
    bool lower_pending_ = false; // Lower was asked: the surface going is our doing
    double home_at_ = -1;        // Home was pressed then; -1 = not
    double lost_at_ = -1;        // the quick settings' surface went then, with no app over it
    bool lost_noted_ = false;    // lost_at_ still waits for the time of the next Tick
    int raises_ = 0;             // tries to bring it back since
};

} // namespace den::system
