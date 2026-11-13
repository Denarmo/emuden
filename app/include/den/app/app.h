// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include "den/base/jobs.h"
#include "den/engine/host.h"
#include "den/gfx/canvas.h"
#include "den/gfx/images.h"
#include "den/input/input.h"
#include "den/library/library.h"
#include "den/platform/pal.h"
#include "den/player/session.h"
#include "den/plugins/host.h"
#include "den/store/settings.h"
#include "den/ui/page.h"
#include "den/ui/theme.h"

namespace den::app {

/// Everything a screen may use. Filled by Run before the first screen is built and valid
/// until the app ends. Main thread only, except the members that say otherwise themselves.
struct Services {
    pal::Window* window = nullptr;
    gfx::Device* device = nullptr;
    gfx::Canvas* canvas = nullptr;
    gfx::TextureCache* textures = nullptr;
    input::System* input = nullptr;
    Jobs* jobs = nullptr;
    ui::Stage* stage = nullptr;
    store::Settings* settings = nullptr;
    library::Library* library = nullptr;
    plugins::Host* plugins = nullptr;
    engine::Host* engine = nullptr;
    player::Session* player = nullptr;
    const ui::Theme* theme = nullptr;
    std::filesystem::path data_dir;
    double fps = 0;
    bool quit = false;
    /// Pixels the app's own title bar takes at the top of the window (0 without one); the
    /// screens are drawn below it and their pointer positions start under it.
    int title_bar_px = 0;
};

Services& Svc();

/// Switches the theme by name and remembers it ("ui.theme").
void SetTheme(const std::string& name);
/// Switches and remembers full screen ("window.fullscreen").
void SetFullscreen(bool on);

/// The whole app: starts everything, runs the frame loop, returns the exit code.
int Run(int argc, char** argv);

} // namespace den::app
