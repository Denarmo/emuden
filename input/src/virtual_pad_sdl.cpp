// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Virtual gamepads: SDL attaches them like hardware, with the USB id of a well-known pad of
// the family asked for, so the app's real device path can be driven without a pad in hand.

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>

#include "den/input/input.h"

namespace den::input::virtual_pad {

namespace {

struct Attachment {
    SDL_JoystickID id = 0;
    SDL_Joystick* handle = nullptr;
};

std::vector<Attachment>& Pads() {
    static std::vector<Attachment> pads;
    return pads;
}

Attachment* Find(int id) {
    for (Attachment& pad : Pads()) {
        if (static_cast<int>(pad.id) == id) {
            return &pad;
        }
    }
    return nullptr;
}

int ToSdl(PadButton button) {
    switch (button) {
    case PadButton::South:
        return SDL_GAMEPAD_BUTTON_SOUTH;
    case PadButton::East:
        return SDL_GAMEPAD_BUTTON_EAST;
    case PadButton::West:
        return SDL_GAMEPAD_BUTTON_WEST;
    case PadButton::North:
        return SDL_GAMEPAD_BUTTON_NORTH;
    case PadButton::DpadUp:
        return SDL_GAMEPAD_BUTTON_DPAD_UP;
    case PadButton::DpadDown:
        return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    case PadButton::DpadLeft:
        return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    case PadButton::DpadRight:
        return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    case PadButton::L1:
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    case PadButton::R1:
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    case PadButton::L3:
        return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    case PadButton::R3:
        return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    case PadButton::Start:
        return SDL_GAMEPAD_BUTTON_START;
    case PadButton::Select:
        return SDL_GAMEPAD_BUTTON_BACK;
    case PadButton::Guide:
        return SDL_GAMEPAD_BUTTON_GUIDE;
    default:
        return -1;
    }
}

} // namespace

int Attach(PadFamily family) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    switch (family) {
    case PadFamily::PlayStation:
        desc.vendor_id = 0x054c;
        desc.product_id = 0x0ce6;
        desc.name = "Virtual DualSense PlayStation Controller";
        break;
    case PadFamily::Nintendo:
        desc.vendor_id = 0x057e;
        desc.product_id = 0x2009;
        desc.name = "Virtual Nintendo Switch Pro Controller";
        break;
    default:
        desc.vendor_id = 0x045e;
        desc.product_id = 0x0b12;
        desc.name = "Virtual Xbox Controller";
        break;
    }
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (!id) {
        return 0;
    }
    Attachment pad;
    pad.id = id;
    pad.handle = SDL_OpenJoystick(id);
    if (!pad.handle) {
        SDL_DetachVirtualJoystick(id);
        return 0;
    }
    // A trigger at rest is the bottom of its axis, not the middle.
    SDL_SetJoystickVirtualAxis(pad.handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(pad.handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    Pads().push_back(pad);
    return static_cast<int>(id);
}

bool Detach(int id) {
    auto& pads = Pads();
    for (auto it = pads.begin(); it != pads.end(); ++it) {
        if (static_cast<int>(it->id) == id) {
            SDL_CloseJoystick(it->handle);
            SDL_DetachVirtualJoystick(it->id);
            pads.erase(it);
            return true;
        }
    }
    return false;
}

bool SetButton(int id, PadButton button, bool down) {
    Attachment* pad = Find(id);
    if (!pad) {
        return false;
    }
    // The triggers are axes on a real pad, so they are here.
    if (button == PadButton::L2 || button == PadButton::R2) {
        return SetAxis(id, button == PadButton::L2 ? Axis::LeftTrigger : Axis::RightTrigger,
                       down ? 1.0f : 0.0f);
    }
    const int sdl = ToSdl(button);
    return sdl >= 0 && SDL_SetJoystickVirtualButton(pad->handle, sdl, down);
}

bool SetAxis(int id, Axis axis, float value) {
    Attachment* pad = Find(id);
    if (!pad) {
        return false;
    }
    const bool trigger = axis == Axis::LeftTrigger || axis == Axis::RightTrigger;
    // SDL reads a trigger over the whole axis: 0..1 becomes -32768..32767.
    const float unit = trigger ? std::clamp(value, 0.0f, 1.0f) * 2.0f - 1.0f
                               : std::clamp(value, -1.0f, 1.0f);
    const auto raw = static_cast<Sint16>(std::clamp(std::lround(unit * 32767.5f - 0.5f), -32768L, 32767L));
    return SDL_SetJoystickVirtualAxis(pad->handle, static_cast<int>(axis), raw);
}

std::vector<int> Attached() {
    std::vector<int> ids;
    for (const Attachment& pad : Pads()) {
        ids.push_back(static_cast<int>(pad.id));
    }
    return ids;
}

} // namespace den::input::virtual_pad
