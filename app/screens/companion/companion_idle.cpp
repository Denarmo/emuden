// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The second screen while the main one shows the Apps section, which is about no game: a
// plain page instead of the details of the game last looked at. Route "companion.idle" on the
// companion stage (under the in-app browser: companion_web.cpp).

#include "companion_page.h"
#include "den/app/app.h"
#include "den/app/registry.h"

namespace den::app {

namespace {

class CompanionIdle final : public CompanionPage {
public:
    void Build() override {
        Services& svc = Svc();
        Root().SetPadding(40, 32);
        Root().gap = 10;
        Root().justify = ui::Align::Center;
        Root().Emplace<ui::Label>("Apps", svc.theme->text_title, gfx::FONT_BOLD);
        for (const char* line : {"Choose an app on the other screen.",
                                 "While a game runs, the quick settings here open one beside it."}) {
            Root().Emplace<ui::Label>(line)->tone = ui::Tone::Dim;
        }
    }
};

} // namespace

DEN_REGISTER_SCREEN("companion.idle", CompanionIdle);

} // namespace den::app
