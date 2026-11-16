// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Desktop refresh rates through SDL. A full screen window is borderless on the desktop's own
// mode; asking for a rate makes it exclusive on that mode, and 0 makes it borderless again.

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "den/base/log.h"
#include "den/platform/refresh.h"

namespace den::pal {

namespace {

bool g_switched = false; // the window was given a mode of its own

bool Fullscreen(SDL_Window* window) {
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
}

} // namespace

RefreshRates MainRefreshRates(Window& window) {
    RefreshRates rates;
    auto* sdl = static_cast<SDL_Window*>(window.SdlWindow());
    const SDL_DisplayID display = sdl ? SDL_GetDisplayForWindow(sdl) : 0;
    const SDL_DisplayMode* current = display ? SDL_GetCurrentDisplayMode(display) : nullptr;
    if (!current) {
        return rates;
    }
    rates.current = current->refresh_rate;
    rates.available.push_back(current->refresh_rate);
    if (!Fullscreen(sdl)) {
        return rates;
    }
    int count = 0;
    if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count)) {
        for (int i = 0; i < count; ++i) {
            const SDL_DisplayMode& mode = *modes[i];
            const bool known = std::any_of(rates.available.begin(), rates.available.end(),
                                           [&](float hz) { return std::fabs(hz - mode.refresh_rate) < 0.5f; });
            if (mode.w == current->w && mode.h == current->h && mode.refresh_rate > 0 && !known) {
                rates.available.push_back(mode.refresh_rate);
            }
        }
        SDL_free(modes);
    }
    return rates;
}

void RequestMainRefresh(Window& window, float hz) {
    auto* sdl = static_cast<SDL_Window*>(window.SdlWindow());
    if (!sdl) {
        return;
    }
    if (hz <= 0) {
        if (g_switched) {
            SDL_SetWindowFullscreenMode(sdl, nullptr);
            g_switched = false;
        }
        return;
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(sdl);
    const SDL_DisplayMode* current = display ? SDL_GetCurrentDisplayMode(display) : nullptr;
    if (!current || !Fullscreen(sdl) || std::fabs(current->refresh_rate - hz) < 0.5f) {
        return;
    }
    SDL_DisplayMode mode{};
    if (!SDL_GetClosestFullscreenDisplayMode(display, current->w, current->h, hz, false, &mode) ||
        std::fabs(mode.refresh_rate - hz) >= 0.5f) {
        return;
    }
    if (SDL_SetWindowFullscreenMode(sdl, &mode)) {
        g_switched = true;
        log::Info("platform", "display mode {}x{} at {:.2f} Hz", mode.w, mode.h, mode.refresh_rate);
    }
}

} // namespace den::pal
