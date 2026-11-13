// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The platform's own keyboard for a text the app asks for (Android: the system keyboard,
// which can sit on the second display while the text is shown on the main one). The app's
// text entry (den/ui/text_entry.h) starts a session, shows what is typed and ends it. On
// the desktop there is none: a real keyboard types through den::input as before.

#pragma once

#include <string>
#include <vector>

namespace den::pal {

struct TextInputRequest {
    std::string text;        // what the line holds at the start
    std::string placeholder; // the hint the platform may show in its own field
    int max_length = 32;     // in characters
};

/// Which display the keyboard is asked to open on, seen from the app's own window.
enum class TextDisplay {
    Main,   // the display the app's window is on: over the bottom of the app
    Second, // the other display (pal::GetSecondDisplay), when the device has one
};

struct TextInputEvent {
    enum class Kind {
        Shown,  // the keyboard is on screen
        Hidden, // it was put away (by the user, or with its display) while the session runs
        Text,   // the whole text as it is now
        Done,   // the keyboard's own done or enter key
        Cancel, // the user closed the keyboard (Back)
    };
    Kind kind = Kind::Text;
    std::string text;
};

/// Main thread only. One session at a time.
class TextInput {
public:
    virtual ~TextInput() = default;
    /// The platform has a keyboard of its own to show. Constant for the life of the process.
    virtual bool Available() const = 0;
    /// Starts a session. False when the platform has no keyboard to show; then nothing was
    /// started. A keyboard that was asked for and never reports Shown did not open (some
    /// refuse a second display): the caller ends the session and shows its own keys.
    virtual bool Begin(const TextInputRequest& request, TextDisplay display) = 0;
    /// Replaces the text of the running session (the app's own keys changed it). No Text
    /// event follows from it.
    virtual void SetText(const std::string& text) = 0;
    /// Ends the session and puts the keyboard away. Events not taken yet are dropped.
    virtual void End() = 0;
    virtual bool Active() const = 0;
    /// Appends what happened since the last call, oldest first.
    virtual void Poll(std::vector<TextInputEvent>& out) = 0;
    /// How much of the bottom of the display the keyboard is on it covers, as a share of
    /// that display's height (0..1); 0 while it is not shown.
    virtual float CoveredShare() const = 0;
};

TextInput& GetTextInput();

} // namespace den::pal
