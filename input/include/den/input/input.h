// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace den::input {

enum class PadFamily { Xbox, PlayStation, Nintendo, Keyboard };

/// What the UI understands. Pads are mapped by label: Confirm is the button labelled A (the
/// bottom face button on Xbox and PlayStation pads, the right one on Nintendo pads), Back is
/// B, Option is X and Extra is Y.
enum class Nav {
    None,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    Option,
    Extra,
    Menu, // Start, Options, +
    View, // Back, Share, -
    ShoulderL,
    ShoulderR,
    Home,
};

/// Pad buttons by position, for the player (a libretro RetroPad is positional).
enum class PadButton : std::uint32_t {
    South,
    East,
    West,
    North,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    L1,
    R1,
    L2,
    R2,
    L3,
    R3,
    Start,
    Select,
    Guide,
    Count,
};

struct PadState {
    bool connected = false;
    PadFamily family = PadFamily::Xbox;
    std::string name;
    std::uint32_t buttons = 0; // bit per PadButton
    float lx = 0, ly = 0, rx = 0, ry = 0; // -1..1, down and right positive
    float lt = 0, rt = 0;                 // 0..1

    bool Down(PadButton button) const {
        return (buttons >> static_cast<std::uint32_t>(button)) & 1u;
    }
};

struct Event {
    enum class Type {
        Nav,         // nav, repeat, family
        PointerDown, // x, y in pixels (mouse and touch alike)
        PointerMove, // x, y; down says whether the button or finger is held
        PointerUp,
        Wheel, // x, y, dx, dy (lines)
        Text,  // text
        Key,   // key (SDL keycode), down
        Quit,
        Resized,
        PadsChanged,
    };
    Type type = Type::Nav;
    Nav nav = Nav::None;
    bool repeat = false;
    PadFamily family = PadFamily::Keyboard;
    float x = 0, y = 0, dx = 0, dy = 0;
    bool down = false;
    int key = 0;
    std::string text;

    static Event MakeNav(Nav nav, PadFamily family = PadFamily::Keyboard) {
        Event event;
        event.type = Type::Nav;
        event.nav = nav;
        event.family = family;
        return event;
    }
};

// Mapping by label (pure functions).
/// The Nav a pad button stands for on a pad of that family; Nav::None for buttons the UI
/// does not use.
Nav NavForButton(PadFamily family, PadButton button);
/// The button that triggers `nav` on that family (the reverse of NavForButton).
PadButton ButtonForNav(PadFamily family, Nav nav);
/// The glyph asset name for a Nav on a family, e.g. "xbox_a", "ps_cross", "switch_plus",
/// "key_enter".
const char* GlyphName(PadFamily family, Nav nav);
/// A short text for the same glyph, drawn in a badge until icon glyphs exist ("A", "Esc").
const char* GlyphLabel(PadFamily family, Nav nav);
const char* NavName(Nav nav);
/// "up", "confirm", ... as used by the automation endpoint; Nav::None when unknown.
Nav NavFromName(const std::string& name);
const char* FamilyName(PadFamily family);

/// Asks for typed text (Event::Type::Text) while something on screen takes it. The System
/// applies it on its next Poll, and only where a keyboard is attached, so no system keyboard
/// slides over the app's own. Calls are counted: every true needs its false.
void RequestTextInput(bool on);
bool TextInputWanted();

/// Owns SDL's event queue: gamepads, keyboard, mouse and touch all become Events.
class System {
public:
    /// `sdl_window` is pal::Window::SdlWindow().
    explicit System(void* sdl_window);
    ~System();

    /// Pumps the OS and appends this frame's events. Call once per frame on the main thread.
    void Poll(std::vector<Event>& out);
    /// Queues an event as if a device had produced it (automation).
    void Inject(const Event& event);

    /// The family of the device used last, which decides the button hints shown.
    PadFamily ActiveFamily() const;
    int PadCount() const;
    /// Pads by player: index 0 is player 1. New pads join at the end; a default state for an
    /// index out of range.
    const PadState& Pad(int index) const;
    /// Makes the pad at `index` the pad of `player` (0 = player 1); the pad that was there
    /// takes its place.
    void SetPlayer(int index, int player);
    /// The keyboard as a pad: arrows, Z/X/A/S as south/east/west/north, Q/W shoulders,
    /// Enter start, right Shift select.
    PadState KeyboardPad() const;
    /// Holds pad buttons (a PadButton bit mask) on the keyboard pad for a while (automation).
    void HoldButtons(std::uint32_t buttons, double seconds);
    /// Navigation from pads is held back while true (the player owns the pad).
    void SetNavFromPads(bool on);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace den::input
