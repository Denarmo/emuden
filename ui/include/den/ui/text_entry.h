// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The text entry: a line of text over a grid of keys. A pad moves over the keys, a touch
// taps them, a real keyboard types straight into the line. The stage shows it above the page
// (Stage::AskText).
//
// Where the platform has a keyboard of its own (pal::TextInput: Android's) and the user
// prefers it, that keyboard types into the line instead: on the second display when there is
// one, while the line stays where the entry is drawn; on the same display the line moves up
// above it. A request started with the pad keeps the app's keys as well. A keyboard that
// does not appear within a second, or is put away, gives way to the app's keys, which the app
// can draw on the other display (DrawKeys).

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "den/platform/text_input.h"
#include "den/ui/node.h"

namespace den::ui {

struct TextRequest {
    std::string title;
    std::string text;        // what the line starts with
    std::string placeholder; // shown while the line is empty
    int max_length = 32;     // in characters
    bool allow_empty = false;
    /// Called with the trimmed text whenever it changes (a search that follows the typing).
    std::function<void(const std::string&)> on_change;
};

/// Where the keys of a text entry come from; the stage gives it to every entry it opens.
struct KeyboardSetup {
    pal::TextInput* system = nullptr; // the platform's keyboard; null where there is none
    bool prefer_system = false;       // the user's choice: it rather than the app's keys
    pal::TextDisplay display = pal::TextDisplay::Main; // where the platform shows it
    bool apart = false;      // that is another display than the one the entry is drawn on
    bool keys_apart = false; // the app draws the entry's own keys there when asked (KeysApart)
};

class TextEntry {
public:
    /// `by_pad`: the request was started with a pad, so the app's keys stay at hand.
    TextEntry(TextRequest request, std::function<void(const std::string&)> on_done, KeyboardSetup setup = {},
              bool by_pad = true);
    ~TextEntry();
    TextEntry(const TextEntry&) = delete;
    TextEntry& operator=(const TextEntry&) = delete;

    enum class Outcome { Open, Done, Cancelled };
    /// Pointer positions in `event` are pixels; `scale` is pixels per logical unit.
    Outcome HandleEvent(const input::Event& event, float scale);
    /// Once per frame: what the platform's keyboard did, and the wait for it to appear.
    Outcome Update(float dt);
    void Draw(Context& ctx);

    /// The platform's keyboard did not come and the app's keys belong on the other display:
    /// the app draws them there with DrawKeys and sends that display's presses to KeysPointer.
    bool KeysApart() const {
        return keys_apart_;
    }
    /// The keys alone, filling the canvas (the other display).
    void DrawKeys(Context& ctx);
    /// A press on the keys drawn by DrawKeys; what it leads to comes out of the next Update.
    void KeysPointer(const input::Event& event, float scale);

    const std::string& Title() const {
        return request_.title;
    }
    /// The text with the spaces at its ends removed: what Done hands over.
    std::string Text() const;
    /// The key the selection is on, for automation: "a", "Shift", "Space", "Delete", "Done".
    std::string SelectedKey() const;
    /// "system" while the platform's keyboard types, "apart" while the app's keys are on the
    /// other display, else "keys" (automation).
    const char* Mode() const;
    /// Runs the callback with the text. The stage calls it after the entry has closed.
    void Finish();

private:
    struct Key {
        std::string label; // what it types, or the name of a wide key
        int row = 0, column = 0, span = 1;
        gfx::Rect rect; // in the entry's own panel
        gfx::Rect far;  // on the other display (DrawKeys)
    };
    enum class Action { None, Done, Cancel };

    Action Press(const Key& key);
    Action OnNav(input::Nav nav);
    Action TryFinish();
    void Move(int dx, int dy);
    const Key* Selected() const;
    std::string Typed(const Key& key) const;
    void Append(const std::string& text);
    void Backspace();
    void Changed(bool from_system);
    bool CanFinish() const;
    void StartSystem();
    void GiveUpSystem();
    void LayoutKeys(float left, float top, float key_w, float key_h, bool far);
    void DrawKeyGrid(Context& ctx, bool far, float key_h);

    TextRequest request_;
    std::function<void(const std::string&)> on_done_;
    KeyboardSetup setup_;
    std::vector<Key> keys_;
    std::string text_;
    int row_ = 1, column_ = 0;
    bool shift_ = false;
    bool swallow_nav_ = false; // the next keyboard navigation repeats a key already handled
    bool enter_ = false;       // ... and that key was Enter: it means Done
    float refused_ = 0;        // seconds left of the "cannot finish" shake
    // The platform's keyboard.
    bool by_pad_ = true;
    bool system_ = false;     // a session with it runs
    bool shown_ = false;      // ... and it was seen on screen
    float wait_ = 0;          // seconds left for it to appear
    bool grid_ = true;        // the app's keys are in the entry's own panel
    bool keys_apart_ = false; // the app's keys are wanted on the other display
    bool typing_ = false;     // typed text was asked from den::input (a real keyboard)
    Action pending_ = Action::None; // from a press on the keys of the other display
    gfx::Rect done_rect_, cancel_rect_; // the buttons shown without the app's keys
};

} // namespace den::ui
