// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <dwmapi.h>

#include <SDL3/SDL.h>

#include "../frame_native.h"

namespace den::pal {

// Windows 11's attributes; older systems refuse them and keep square corners.
constexpr DWORD CORNER_PREFERENCE = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD BORDER_COLOR = 34;      // DWMWA_BORDER_COLOR
constexpr int CORNER_ROUND = 2;         // DWMWCP_ROUND

void StyleCustomFrame(void* sdl_window, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto* window = static_cast<SDL_Window*>(sdl_window);
    const auto hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) {
        return;
    }
    const int corners = CORNER_ROUND;
    DwmSetWindowAttribute(hwnd, CORNER_PREFERENCE, &corners, sizeof(corners));
    const COLORREF color = RGB(r, g, b);
    DwmSetWindowAttribute(hwnd, BORDER_COLOR, &color, sizeof(color));
}

} // namespace den::pal
