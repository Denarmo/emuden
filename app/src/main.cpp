// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/app/app.h"
#include "den/update/portable.h"

int main(int argc, char** argv) {
    // The copy of the app that swaps in a downloaded update (Windows) does only that.
    if (const auto code = den::update::RunHelperIfAsked(argc, argv)) {
        return *code;
    }
    return den::app::Run(argc, argv);
}
