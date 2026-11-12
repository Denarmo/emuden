// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What SDL's activity calls once libmain.so is loaded.

#include "den/app/app.h"

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    return den::app::Run(argc, argv);
}
