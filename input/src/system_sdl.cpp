// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>

#include <SDL3/SDL.h>

#include "den/input/input.h"
#include "den/platform/lifecycle.h"

namespace den::input {

namespace {

constexpr double REPEAT_DELAY = 0.40;
constexpr double REPEAT_INTERVAL = 0.085;
constexpr float STICK_ON = 0.55f;
constexpr float STICK_OFF = 0.35f;

PadFamily FamilyOf(SDL_Gamepad* pad) {
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return PadFamily::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return PadFamily::Nintendo;
    default: {
        // A pad SDL has no entry for: its name is the next best hint.
        const char* name = SDL_GetGamepadName(pad);
        return FamilyFromName(name ? name : "");
    }
    }
}

PadButton FromSdl(Uint8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return PadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST:
        return PadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST:
        return PadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return PadButton::North;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return PadButton::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return PadButton::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return PadButton::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return PadButton::DpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return PadButton::L1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return PadButton::R1;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return PadButton::L3;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return PadButton::R3;
    case SDL_GAMEPAD_BUTTON_START:
        return PadButton::Start;
    case SDL_GAMEPAD_BUTTON_BACK:
        return PadButton::Select;
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return PadButton::Guide;
    default:
        return PadButton::Count;
    }
}

Nav NavForKey(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP:
        return Nav::Up;
    case SDLK_DOWN:
        return Nav::Down;
    case SDLK_LEFT:
        return Nav::Left;
    case SDLK_RIGHT:
        return Nav::Right;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        return Nav::Confirm;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
    case SDLK_AC_BACK:
        return Nav::Back;
    case SDLK_X:
        return Nav::Option;
    case SDLK_Y:
        return Nav::Extra;
    case SDLK_TAB:
        return Nav::Menu;
    case SDLK_F1:
        return Nav::View;
    case SDLK_Q:
    case SDLK_PAGEUP:
        return Nav::ShoulderL;
    case SDLK_E:
    case SDLK_PAGEDOWN:
        return Nav::ShoulderR;
    case SDLK_HOME:
        return Nav::Home;
    default:
        return Nav::None;
    }
}

std::atomic<int> g_text_requests{0};

struct OpenPad {
    SDL_JoystickID id = 0;
    SDL_Gamepad* handle = nullptr;
    PadState state;
    Nav stick_nav = Nav::None;
};

} // namespace

void RequestTextInput(bool on) {
    g_text_requests += on ? 1 : -1;
}

bool TextInputWanted() {
    return g_text_requests.load() > 0;
}

struct System::Impl {
    SDL_Window* window = nullptr;
    bool text_input = false;
    std::vector<OpenPad> pads;
    std::deque<Event> injected;
    PadFamily active = PadFamily::Keyboard;
    DeviceKind kind = DeviceKind::Desktop;
    bool touch_only = false;
    bool used = false; // a button or key was pressed: the hints follow the device used last
    bool nav_from_pads = true;
    bool pointer_down = false;
    // The direction or button held for auto-repeat.
    Nav held = Nav::None;
    PadFamily held_family = PadFamily::Xbox;
    double next_repeat = 0;
    PadState none;
    std::uint32_t virtual_buttons = 0;
    Uint64 virtual_until = 0;

    OpenPad* Find(SDL_JoystickID id) {
        for (OpenPad& pad : pads) {
            if (pad.id == id) {
                return &pad;
            }
        }
        return nullptr;
    }

    // The hints for what exists, while nothing has been used.
    void ChooseStartHints() {
        const HintChoice choice = StartHints(kind, pads.empty() ? nullptr : &pads.front().state.family);
        active = choice.family;
        touch_only = choice.touch_only;
        used = false;
    }

    void Used(PadFamily family) {
        active = family;
        touch_only = false;
        used = true;
    }

    void Open(SDL_JoystickID id) {
        if (Find(id)) {
            return;
        }
        SDL_Gamepad* handle = SDL_OpenGamepad(id);
        if (!handle) {
            return;
        }
        OpenPad pad;
        pad.id = id;
        pad.handle = handle;
        pad.state.connected = true;
        pad.state.family = FamilyOf(handle);
        const char* name = SDL_GetGamepadName(handle);
        pad.state.name = name ? name : "Controller";
        pads.push_back(std::move(pad));
    }

    void Close(SDL_JoystickID id) {
        for (auto it = pads.begin(); it != pads.end(); ++it) {
            if (it->id == id) {
                // What it held is not held any more: a direction must not repeat for ever.
                Release(it->stick_nav);
                for (std::uint32_t b = 0; b < static_cast<std::uint32_t>(PadButton::Count); ++b) {
                    if (it->state.buttons & (1u << b)) {
                        Release(NavForButton(it->state.family, static_cast<PadButton>(b)));
                    }
                }
                SDL_CloseGamepad(it->handle);
                pads.erase(it);
                return;
            }
        }
    }

    float Density() const {
        const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
        return density > 0.0f ? density : 1.0f;
    }

    void Press(Nav nav, PadFamily family, std::vector<Event>& out) {
        if (nav == Nav::None || !nav_from_pads) {
            return;
        }
        out.push_back(Event::MakeNav(nav, family));
        Used(family);
        if (nav == Nav::Up || nav == Nav::Down || nav == Nav::Left || nav == Nav::Right) {
            held = nav;
            held_family = family;
            next_repeat = SDL_GetTicks() / 1000.0 + REPEAT_DELAY;
        }
    }

    void Release(Nav nav) {
        if (nav == held) {
            held = Nav::None;
        }
    }

    void StickNav(OpenPad& pad, std::vector<Event>& out) {
        const float x = pad.state.lx, y = pad.state.ly;
        Nav now = Nav::None;
        const float limit = pad.stick_nav == Nav::None ? STICK_ON : STICK_OFF;
        if (std::fabs(x) > std::fabs(y)) {
            now = x > limit ? Nav::Right : x < -limit ? Nav::Left : Nav::None;
        } else {
            now = y > limit ? Nav::Down : y < -limit ? Nav::Up : Nav::None;
        }
        if (now != pad.stick_nav) {
            Release(pad.stick_nav);
            pad.stick_nav = now;
            Press(now, pad.state.family, out);
        }
    }

    void Handle(const SDL_Event& e, std::vector<Event>& out) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            Event event;
            event.type = Event::Type::Quit;
            out.push_back(event);
            break;
        }
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            Event event;
            event.type = Event::Type::Resized;
            out.push_back(event);
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED: {
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                Open(e.gdevice.which);
            } else {
                Close(e.gdevice.which);
            }
            // Nothing used yet, or the pad in use left: the hints follow what exists now.
            if (!used || (pads.empty() && active != PadFamily::Keyboard)) {
                ChooseStartHints();
            }
            Event event;
            event.type = Event::Type::PadsChanged;
            out.push_back(event);
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            OpenPad* pad = Find(e.gbutton.which);
            const PadButton button = FromSdl(e.gbutton.button);
            if (!pad || button == PadButton::Count) {
                break;
            }
            const std::uint32_t bit = 1u << static_cast<std::uint32_t>(button);
            const Nav nav = NavForButton(pad->state.family, button);
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                pad->state.buttons |= bit;
                Used(pad->state.family);
                Press(nav, pad->state.family, out);
            } else {
                pad->state.buttons &= ~bit;
                Release(nav);
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            OpenPad* pad = Find(e.gaxis.which);
            if (!pad) {
                break;
            }
            const float value = std::clamp(e.gaxis.value / 32767.0f, -1.0f, 1.0f);
            const auto trigger = [&](float& slot, PadButton button) {
                slot = value;
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(button);
                pad->state.buttons = value > 0.5f ? pad->state.buttons | bit : pad->state.buttons & ~bit;
            };
            switch (e.gaxis.axis) {
            case SDL_GAMEPAD_AXIS_LEFTX:
                pad->state.lx = value;
                break;
            case SDL_GAMEPAD_AXIS_LEFTY:
                pad->state.ly = value;
                break;
            case SDL_GAMEPAD_AXIS_RIGHTX:
                pad->state.rx = value;
                break;
            case SDL_GAMEPAD_AXIS_RIGHTY:
                pad->state.ry = value;
                break;
            case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
                trigger(pad->state.lt, PadButton::L2);
                break;
            case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
                trigger(pad->state.rt, PadButton::R2);
                break;
            default:
                break;
            }
            StickNav(*pad, out);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            Event key;
            key.type = Event::Type::Key;
            key.key = static_cast<int>(e.key.key);
            key.down = e.type == SDL_EVENT_KEY_DOWN;
            key.repeat = e.key.repeat;
            out.push_back(key);
            // Android's Back is a system gesture, not a keyboard.
            if (key.down && e.key.key != SDLK_AC_BACK) {
                Used(PadFamily::Keyboard);
            }
            if (key.down) {
                const Nav nav = NavForKey(e.key.key);
                if (nav != Nav::None) {
                    Event event = Event::MakeNav(nav, PadFamily::Keyboard);
                    event.repeat = e.key.repeat;
                    out.push_back(event);
                }
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            Event event;
            event.type = Event::Type::Text;
            event.text = e.text.text ? e.text.text : "";
            out.push_back(event);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                if (e.button.button == SDL_BUTTON_RIGHT && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    out.push_back(Event::MakeNav(Nav::Back, PadFamily::Keyboard));
                }
                break;
            }
            Event event;
            pointer_down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.type = pointer_down ? Event::Type::PointerDown : Event::Type::PointerUp;
            event.x = e.button.x * Density();
            event.y = e.button.y * Density();
            event.down = pointer_down;
            event.touch_synthetic = e.button.which == SDL_TOUCH_MOUSEID;
            out.push_back(event);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            Event event;
            event.type = Event::Type::PointerMove;
            event.x = e.motion.x * Density();
            event.y = e.motion.y * Density();
            event.dx = e.motion.xrel * Density();
            event.dy = e.motion.yrel * Density();
            event.down = pointer_down;
            event.touch_synthetic = e.motion.which == SDL_TOUCH_MOUSEID;
            out.push_back(event);
            break;
        }
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            if (e.type == SDL_EVENT_FINGER_DOWN && kind == DeviceKind::TouchOnly && pads.empty()) {
                ChooseStartHints();
            }
            int pw = 0, ph = 0;
            if (window) {
                SDL_GetWindowSizeInPixels(window, &pw, &ph);
            }
            if (pw > 0 && ph > 0) {
                Event event;
                event.type = e.type == SDL_EVENT_FINGER_DOWN   ? Event::Type::FingerDown
                           : e.type == SDL_EVENT_FINGER_MOTION ? Event::Type::FingerMove
                                                               : Event::Type::FingerUp;
                event.finger = static_cast<std::uint64_t>(e.tfinger.fingerID);
                event.x = e.tfinger.x * static_cast<float>(pw);
                event.y = e.tfinger.y * static_cast<float>(ph);
                event.dx = e.tfinger.dx * static_cast<float>(pw);
                event.dy = e.tfinger.dy * static_cast<float>(ph);
                event.down = e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION;
                out.push_back(event);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            Event event;
            event.type = Event::Type::Wheel;
            event.x = e.wheel.mouse_x * Density();
            event.y = e.wheel.mouse_y * Density();
            event.dx = e.wheel.x;
            event.dy = e.wheel.y;
            out.push_back(event);
            break;
        }
        default:
            break;
        }
    }
};

System::System(void* sdl_window) : impl_(std::make_unique<Impl>()) {
    impl_->window = static_cast<SDL_Window*>(sdl_window);
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i) {
            impl_->Open(ids[i]);
        }
        SDL_free(ids);
    }
    impl_->ChooseStartHints();
}

System::~System() {
    for (OpenPad& pad : impl_->pads) {
        SDL_CloseGamepad(pad.handle);
    }
}

void System::Poll(std::vector<Event>& out) {
    const bool want_text = TextInputWanted() && SDL_HasKeyboard();
    if (want_text != impl_->text_input && impl_->window) {
        impl_->text_input = want_text;
        if (want_text) {
            SDL_StartTextInput(impl_->window);
        } else {
            SDL_StopTextInput(impl_->window);
        }
    }
    SDL_Event e;
    if (pal::EventPumpHeld()) {
        // The main window is hidden while the app goes on: the system's pump would wait for
        // it. Pads and keys are queued by the platform itself.
        SDL_UpdateJoysticks();
        while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) > 0) {
            impl_->Handle(e, out);
        }
    } else {
        while (SDL_PollEvent(&e)) {
            impl_->Handle(e, out);
        }
    }
    if (impl_->held != Nav::None && impl_->nav_from_pads) {
        const double now = SDL_GetTicks() / 1000.0;
        if (now >= impl_->next_repeat) {
            Event event = Event::MakeNav(impl_->held, impl_->held_family);
            event.repeat = true;
            out.push_back(event);
            impl_->next_repeat = now + REPEAT_INTERVAL;
        }
    }
    while (!impl_->injected.empty()) {
        out.push_back(std::move(impl_->injected.front()));
        impl_->injected.pop_front();
    }
}

void System::Inject(const Event& event) {
    impl_->injected.push_back(event);
}

void System::SetDeviceKind(DeviceKind kind) {
    impl_->kind = kind;
    if (!impl_->used) {
        impl_->ChooseStartHints();
    }
}

PadFamily System::ActiveFamily() const {
    return impl_->active;
}

bool System::TouchOnly() const {
    return impl_->touch_only;
}

int System::PadCount() const {
    return static_cast<int>(impl_->pads.size());
}

const PadState& System::Pad(int index) const {
    if (index < 0 || index >= PadCount()) {
        return impl_->none;
    }
    return impl_->pads[static_cast<std::size_t>(index)].state;
}

void System::SetPlayer(int index, int player) {
    const int count = PadCount();
    if (index < 0 || index >= count || player < 0 || player >= count || index == player) {
        return;
    }
    std::swap(impl_->pads[static_cast<std::size_t>(index)], impl_->pads[static_cast<std::size_t>(player)]);
}

PadState System::KeyboardPad() const {
    PadState pad;
    pad.connected = true;
    pad.family = PadFamily::Keyboard;
    pad.name = "Keyboard";
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const auto set = [&](SDL_Scancode code, PadButton button) {
        if (keys[code]) {
            pad.buttons |= 1u << static_cast<std::uint32_t>(button);
        }
    };
    set(SDL_SCANCODE_UP, PadButton::DpadUp);
    set(SDL_SCANCODE_DOWN, PadButton::DpadDown);
    set(SDL_SCANCODE_LEFT, PadButton::DpadLeft);
    set(SDL_SCANCODE_RIGHT, PadButton::DpadRight);
    set(SDL_SCANCODE_Z, PadButton::South);
    set(SDL_SCANCODE_X, PadButton::East);
    set(SDL_SCANCODE_A, PadButton::West);
    set(SDL_SCANCODE_S, PadButton::North);
    set(SDL_SCANCODE_Q, PadButton::L1);
    set(SDL_SCANCODE_W, PadButton::R1);
    set(SDL_SCANCODE_RETURN, PadButton::Start);
    set(SDL_SCANCODE_RSHIFT, PadButton::Select);
    if (SDL_GetTicks() < impl_->virtual_until) {
        pad.buttons |= impl_->virtual_buttons;
    }
    return pad;
}

void System::HoldButtons(std::uint32_t buttons, double seconds) {
    impl_->virtual_buttons = buttons;
    impl_->virtual_until = SDL_GetTicks() + static_cast<Uint64>(seconds * 1000.0);
}

void System::SetNavFromPads(bool on) {
    impl_->nav_from_pads = on;
    if (!on) {
        impl_->held = Nav::None;
    }
}

} // namespace den::input
