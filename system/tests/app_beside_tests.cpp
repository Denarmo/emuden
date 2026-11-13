// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/system/app_beside.h"

using den::system::AppBeside;
using den::system::BesideAction;
using den::system::BesideActions;
using den::system::BesideApp;
using Screen = den::system::BesideScreen;
using Kind = den::system::BesideAction::Kind;
using Stage = den::system::AppBeside::Stage;

namespace {

const BesideApp VIDEO{"com.video/.Main", "Video", "v.png"};
const BesideApp FILES{"com.files/.Main", "Files", ""};

// A stand-in for the device: it does what the actions ask the way Android would, and tells
// the machine which of this app's surfaces are shown afterwards.
struct Device {
    AppBeside beside;
    bool main = true, second = true;
    bool handle_possible = true;
    bool paused = false, menu = false;
    int saves = 0;
    std::vector<Kind> log;

    explicit Device(Screen game = Screen::Main, bool two = true) {
        second = two;
        beside.SetTwoDisplays(two);
        Run(beside.SetSurfaces(main, second));
        Run(beside.SetGame(true, game));
    }

    bool& Surface(Screen screen) {
        return screen == Screen::Main ? main : second;
    }

    void Run(const BesideActions& actions) {
        for (const BesideAction& action : actions) {
            log.push_back(action.kind);
            switch (action.kind) {
            case Kind::Launch:
            case Kind::Lower:
                Surface(action.screen) = false; // the other app covers our window there
                break;
            case Kind::Raise:
                Surface(action.screen) = true;
                break;
            case Kind::ShowHandle:
                beside.SetHandle(handle_possible);
                break;
            case Kind::HideHandle:
                beside.SetHandle(false);
                break;
            case Kind::OpenMenu:
                menu = true;
                break;
            case Kind::SaveGame:
                ++saves;
                break;
            case Kind::HoldGame:
                paused = true;
                break;
            case Kind::ReleaseGame:
                paused = false;
                break;
            }
        }
        if (!actions.empty()) {
            Run(beside.SetSurfaces(main, second));
        }
    }

    // What the user or the system does outside this app.
    void Cover(Screen screen) {
        Surface(screen) = false;
        Run(beside.SetSurfaces(main, second));
    }
    void Uncover(Screen screen) {
        Surface(screen) = true;
        Run(beside.SetSurfaces(main, second));
    }
    bool Did(Kind kind) const {
        for (const Kind done : log) {
            if (done == kind) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("an app opens on the display of the quick settings and the game goes on") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    CHECK(d.beside.GetStage() == Stage::AppFront);
    CHECK(d.beside.AppScreen() == Screen::Second);
    CHECK(d.beside.App().label == "Video");
    CHECK_FALSE(d.second);
    CHECK(d.main);
    CHECK_FALSE(d.paused);
    CHECK_FALSE(d.menu);
    CHECK(d.saves == 1); // a resume state before anything that could end the process
    CHECK(d.beside.HandleShown());
    CHECK(d.beside.AppHasKeys());
}

TEST_CASE("the game on the second display keeps running under an app on the main one") {
    Device d(Screen::Second);
    d.Run(d.beside.Launch(VIDEO));
    CHECK(d.beside.AppScreen() == Screen::Main);
    CHECK_FALSE(d.main); // the main activity is stopped
    CHECK(d.second);
    CHECK_FALSE(d.paused);
    CHECK_FALSE(d.beside.GameHeld());
    CHECK(d.beside.GetStage() == Stage::AppFront);
}

TEST_CASE("the handle pulls the quick settings over the app, and Back to the app hides them again") {
    for (const Screen game : {Screen::Main, Screen::Second}) {
        Device d(game);
        const Screen side = den::system::Other(game);
        d.Run(d.beside.Launch(VIDEO));
        d.log.clear();
        d.Run(d.beside.PullSettings());
        CHECK(d.beside.GetStage() == Stage::SettingsOver);
        CHECK(d.Surface(side));
        CHECK(d.beside.App().id == VIDEO.id); // still there, under the quick settings
        CHECK_FALSE(d.beside.HandleShown());
        CHECK_FALSE(d.Did(Kind::Launch)); // nothing is opened again

        d.log.clear();
        d.Run(d.beside.BackToApp());
        CHECK(d.beside.GetStage() == Stage::AppFront);
        CHECK_FALSE(d.Surface(side));
        CHECK(d.Did(Kind::Lower));
        CHECK_FALSE(d.Did(Kind::Launch));
        CHECK(d.beside.HandleShown());
        CHECK_FALSE(d.paused);
    }
}

TEST_CASE("system Back on the display of the quick settings goes back to the app only while they cover one") {
    Device d(Screen::Main);
    BesideActions out;
    CHECK_FALSE(d.beside.Back(Screen::Second, out));
    d.Run(d.beside.Launch(VIDEO));
    CHECK_FALSE(d.beside.Back(Screen::Second, out));
    d.Run(d.beside.PullSettings());
    CHECK(d.beside.CatchesBack(Screen::Second));
    CHECK_FALSE(d.beside.Back(Screen::Main, out)); // Back on the game is the game's
    REQUIRE(d.beside.Back(Screen::Second, out));
    d.Run(out);
    CHECK(d.beside.GetStage() == Stage::AppFront);
    CHECK_FALSE(d.second);
}

TEST_CASE("leaving the app with Back brings the plain quick settings back") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    d.log.clear();
    d.Uncover(Screen::Second); // the app finished: our window under it is shown again
    CHECK(d.beside.GetStage() == Stage::None);
    CHECK(d.beside.App().id.empty());
    CHECK(d.Did(Kind::HideHandle));
    CHECK_FALSE(d.beside.HandleShown());
    CHECK_FALSE(d.Did(Kind::Raise));
}

TEST_CASE("Close ends the side-by-side state") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    d.Run(d.beside.PullSettings());
    d.log.clear();
    d.Run(d.beside.CloseApp());
    CHECK(d.beside.GetStage() == Stage::None);
    CHECK(d.second);
    CHECK_FALSE(d.beside.HandleShown());
    CHECK_FALSE(d.beside.CatchesBack(Screen::Second));

    // Closed while the app is in front (not offered, but asked for): the settings come up.
    d.Run(d.beside.Launch(FILES));
    d.Run(d.beside.CloseApp());
    CHECK(d.beside.GetStage() == Stage::None);
    CHECK(d.second);
}

TEST_CASE("another app replaces the one beside the game") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    d.Run(d.beside.PullSettings());
    d.Run(d.beside.Launch(FILES));
    CHECK(d.beside.GetStage() == Stage::AppFront);
    CHECK(d.beside.App().id == FILES.id);
    CHECK(d.beside.HandleShown());
}

TEST_CASE("the game is held while its own display is covered and goes on when it is back") {
    Device d(Screen::Main);
    d.Cover(Screen::Main); // Home on the game's display
    CHECK(d.paused);
    CHECK(d.beside.GameHeld());
    CHECK(d.saves == 1);
    CHECK_FALSE(d.beside.WantsTab());
    d.Uncover(Screen::Main);
    CHECK_FALSE(d.paused);
    CHECK_FALSE(d.beside.GameHeld());
}

TEST_CASE("both surfaces gone and back: the game is held once and released once") {
    Device d(Screen::Second);
    d.Cover(Screen::Main);
    CHECK_FALSE(d.paused); // the game is on the second display
    d.Cover(Screen::Second);
    CHECK(d.paused);
    d.Uncover(Screen::Main);
    CHECK(d.paused);
    d.Uncover(Screen::Second);
    CHECK_FALSE(d.paused);
}

TEST_CASE("Home on the second display: the quick settings come back by themselves") {
    Device d(Screen::Main);
    d.Run(d.beside.Tick(10.0));
    d.Cover(Screen::Second);
    CHECK_FALSE(d.paused);
    d.Run(d.beside.Tick(10.1));
    CHECK_FALSE(d.second); // not at once
    d.Run(d.beside.Tick(10.6));
    CHECK(d.second);
    CHECK(d.beside.GetStage() == Stage::None);
}

TEST_CASE("the quick settings are not called back while an app of ours covers them, or while this app is left") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    d.Run(d.beside.Tick(1.0));
    d.Run(d.beside.Tick(5.0));
    CHECK_FALSE(d.second);
    CHECK(d.beside.GetStage() == Stage::AppFront);

    Device left(Screen::Main);
    left.Cover(Screen::Main);
    left.Cover(Screen::Second);
    left.Run(left.beside.Tick(1.0));
    left.Run(left.beside.Tick(5.0));
    CHECK_FALSE(left.second);
    left.Uncover(Screen::Main);
    left.Run(left.beside.Tick(6.0));
    CHECK(left.second); // back in the app: its second display comes with it
}

TEST_CASE("a display that refuses to come back is asked three times, not for ever") {
    AppBeside beside;
    beside.SetTwoDisplays(true);
    beside.SetSurfaces(true, true);
    beside.SetGame(true, Screen::Main);
    beside.SetSurfaces(true, false);
    int raises = 0;
    for (double t = 0; t < 60; t += 0.1) {
        for (const BesideAction& action : beside.Tick(t)) {
            raises += action.kind == Kind::Raise;
        }
    }
    CHECK(raises == 3);
}

TEST_CASE("Home on the app's display ends the side-by-side state; Home on the game does not") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    d.beside.Home(20.0);
    d.Run(d.beside.Tick(20.1));
    CHECK(d.beside.GetStage() == Stage::AppFront);
    d.Run(d.beside.Tick(20.6));
    CHECK(d.beside.GetStage() == Stage::None);
    CHECK(d.second); // the quick settings are back
    CHECK_FALSE(d.beside.HandleShown());

    Device game(Screen::Main);
    game.Run(game.beside.Launch(VIDEO));
    game.beside.Home(20.0);
    game.Cover(Screen::Main); // Home was on the game's display
    game.Run(game.beside.Tick(20.6));
    CHECK(game.beside.GetStage() == Stage::AppFront);
    CHECK(game.paused);
}

TEST_CASE("Home while the quick settings are over the app drops the app and brings them back") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    d.Run(d.beside.PullSettings());
    d.Run(d.beside.Tick(1.0));
    d.Cover(Screen::Second);
    CHECK(d.beside.GetStage() == Stage::None);
    d.Run(d.beside.Tick(1.1));
    d.Run(d.beside.Tick(1.6));
    CHECK(d.second);
}

TEST_CASE("with the settings on the main display nothing is forced back; the game shows a tab") {
    Device d(Screen::Second);
    d.handle_possible = false; // the main display takes no window over other apps
    d.Run(d.beside.Launch(VIDEO));
    CHECK_FALSE(d.beside.HandleShown());
    CHECK(d.beside.WantsTab());
    d.beside.Home(1.0);
    d.Run(d.beside.Tick(2.0));
    CHECK(d.beside.GetStage() == Stage::None);
    CHECK_FALSE(d.main); // the main display is the user's
    CHECK(d.beside.WantsTab());
    d.Run(d.beside.PullSettings()); // the tab
    CHECK(d.main);
    CHECK_FALSE(d.beside.WantsTab());
    CHECK_FALSE(d.paused);
}

TEST_CASE("with a handle over the app there is no tab on the game") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    CHECK(d.beside.HandleShown());
    CHECK_FALSE(d.beside.WantsTab());
}

TEST_CASE("one display: the app takes it, the game waits in its menu and is not resumed behind the user's back") {
    Device d(Screen::Main, false);
    d.Run(d.beside.Launch(VIDEO));
    CHECK(d.menu);
    CHECK(d.saves >= 1);
    CHECK_FALSE(d.main);
    CHECK_FALSE(d.Did(Kind::ShowHandle));
    CHECK_FALSE(d.beside.AppHasKeys());
    d.Uncover(Screen::Main);
    CHECK(d.beside.GetStage() == Stage::None);
    CHECK(d.Did(Kind::ReleaseGame)); // releases the hold only; the menu stays the user's
}

TEST_CASE("an app that opened on the game's display holds the game until it is left") {
    Device e(Screen::Main);
    e.beside.Launch(VIDEO); // the platform put it on the main display instead
    e.beside.SetHandle(true);
    e.Run(e.beside.LaunchedOnGameScreen());
    CHECK_FALSE(e.beside.HandleShown());
    e.Cover(Screen::Main);
    CHECK(e.paused);
    e.Uncover(Screen::Main);
    CHECK_FALSE(e.paused);
    CHECK(e.beside.GetStage() == Stage::None);
}

TEST_CASE("a launch that failed leaves nothing behind") {
    AppBeside beside;
    beside.SetTwoDisplays(true);
    beside.SetSurfaces(true, true);
    beside.SetGame(true, Screen::Main);
    beside.Launch(VIDEO);
    const BesideActions out = beside.LaunchFailed();
    CHECK(beside.GetStage() == Stage::None);
    REQUIRE(out.size() == 1);
    CHECK(out[0].kind == Kind::HideHandle);
}

TEST_CASE("swapping the displays or ending the game ends the side-by-side state") {
    Device d(Screen::Main);
    d.Run(d.beside.Launch(VIDEO));
    d.Run(d.beside.PullSettings());
    d.Run(d.beside.SetGame(true, Screen::Second));
    CHECK(d.beside.GetStage() == Stage::None);
    CHECK(d.beside.SettingsScreen() == Screen::Main);
    CHECK_FALSE(d.paused);

    d.Run(d.beside.Launch(FILES));
    d.Run(d.beside.SetGame(false, Screen::Second));
    CHECK(d.beside.GetStage() == Stage::None);
    CHECK_FALSE(d.beside.HandleShown());
    CHECK_FALSE(d.beside.WantsTab());
}

TEST_CASE("without a game an app just opens") {
    AppBeside beside;
    beside.SetTwoDisplays(true);
    const BesideActions out = beside.Launch(VIDEO);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == BesideAction{Kind::Launch, Screen::Main, VIDEO.id});
    CHECK(beside.GetStage() == Stage::None);
}
