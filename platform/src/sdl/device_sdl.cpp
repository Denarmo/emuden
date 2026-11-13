// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>

#include <SDL3/SDL.h>

#include "../device_native.h"
#include "den/platform/device.h"

namespace den::pal {

DeviceFacts GetDeviceFacts() {
    DeviceFacts facts;
    facts.cpu_cores = SDL_GetNumLogicalCPUCores();
    facts.ram_mb = SDL_GetSystemRAM();

    int count = 0;
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (SDL_DisplayID* displays = SDL_GetDisplays(&count)) {
        for (int i = 0; i < count; ++i) {
            const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(displays[i]);
            if (!mode) {
                continue;
            }
            DisplayFacts display;
            if (const char* name = SDL_GetDisplayName(displays[i])) {
                display.name = name;
            }
            const float pixels = mode->pixel_density > 0 ? mode->pixel_density : 1.0f;
            display.width = static_cast<int>(static_cast<float>(mode->w) * pixels + 0.5f);
            display.height = static_cast<int>(static_cast<float>(mode->h) * pixels + 0.5f);
            display.refresh_hz = mode->refresh_rate;
            const float scale = SDL_GetDisplayContentScale(displays[i]);
            display.density = scale > 0 ? scale : 1.0f;
            if (displays[i] == primary) {
                facts.displays.insert(facts.displays.begin(), std::move(display));
            } else {
                facts.displays.push_back(std::move(display));
            }
        }
        SDL_free(displays);
    }

    int touches = 0;
    if (SDL_TouchID* devices = SDL_GetTouchDevices(&touches)) {
        for (int i = 0; i < touches; ++i) {
            facts.touch = facts.touch || SDL_GetTouchDeviceType(devices[i]) == SDL_TOUCH_DEVICE_DIRECT;
        }
        SDL_free(devices);
    }
    FillNativeFacts(facts);
    return facts;
}

} // namespace den::pal
