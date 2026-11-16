// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Which keyboard a text entry gets (den/ui/text_entry.h). Where the platform has one of its
// own (Android) the user chooses between it and the app's keys: Settings > System,
// "Keyboard" ("ui.keyboard": "system" or "emuden"; System by default on touch devices). The
// system keyboard goes to the second display when the device has one, so the text stays
// whole on the main one. Every frame both stages are told where their keyboard goes.

#include "den/app/second_screen.h"
#include "den/app/player.h"
#include "den/app/app.h"
#include "den/app/pages.h"
#include "den/app/registry.h"
#include "den/platform/text_input.h"
#include "keyboard.h"

namespace den::app {

namespace {

constexpr const char* KEYBOARD_KEY = "ui.keyboard";

bool PreferSystem() {
    const std::string choice =
        Svc().settings->GetString(KEYBOARD_KEY, pal::GetCaps().touch ? "system" : "emuden");
    return choice == "system";
}

void Frame() {
    Services& svc = Svc();
    pal::TextInput& system = pal::GetTextInput();
    if (!svc.stage || !system.Available()) {
        return; // the desktop: the app's keys and a real keyboard, as the kit does by itself
    }
    const bool two = SecondScreenReady();
    const GamePlayer* player = CurrentPlayer();
    const bool swapped = player && player->Swapped();

    // The main window's pages: the keyboard on the other display, the line here.
    ui::KeyboardSetup main;
    main.system = &system;
    main.prefer_system = PreferSystem();
    main.display = two ? pal::TextDisplay::Second : pal::TextDisplay::Main;
    main.apart = two;
    main.keys_apart = two;
    svc.stage->SetKeyboard(main);

    // The second screen's pages are typed into where they are drawn: on their own display,
    // or in the main window while the game has the second one.
    ui::KeyboardSetup companion;
    companion.system = &system;
    companion.prefer_system = main.prefer_system;
    companion.display = two && !swapped ? pal::TextDisplay::Second : pal::TextDisplay::Main;
    CompanionStage().SetKeyboard(companion);
}

} // namespace

bool AddKeyboardRow(std::vector<ui::RowSpec>& rows) {
    if (!pal::GetTextInput().Available()) {
        return false;
    }
    ui::RowSpec row = ChoiceRow(
        "system.keyboard", "Keyboard", {"System", "EmuDEN"}, [] { return PreferSystem() ? 0 : 1; },
        [](int index) { Svc().settings->SetString(KEYBOARD_KEY, index == 0 ? "system" : "emuden"); });
    row.description = "System: the device's keyboard, on the second screen when there is one. "
                      "EmuDEN: the app's own keys, made for the pad";
    rows.push_back(std::move(row));
    return true;
}

DEN_REGISTER_FRAME(Frame);

} // namespace den::app
