// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The on-screen text entry: a line of text over a grid of keys. A pad moves over the keys, a
// touch taps them, a real keyboard types straight into the line. The stage shows it above
// the page (Stage::AskText).

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "den/ui/node.h"

namespace den::ui {

struct TextRequest {
    std::string title;
    std::string text;        // what the line starts with
    std::string placeholder; // shown while the line is empty
    int max_length = 32;     // in characters
    bool allow_empty = false;
};

class TextEntry {
public:
    TextEntry(TextRequest request, std::function<void(const std::string&)> on_done);
    ~TextEntry();

    enum class Outcome { Open, Done, Cancelled };
    /// Pointer positions in `event` are pixels; `scale` is pixels per logical unit.
    Outcome HandleEvent(const input::Event& event, float scale);
    void Draw(Context& ctx);

    const std::string& Title() const {
        return request_.title;
    }
    /// The text with the spaces at its ends removed: what Done hands over.
    std::string Text() const;
    /// The key the selection is on, for automation: "a", "Shift", "Space", "Delete", "Done".
    std::string SelectedKey() const;
    /// Runs the callback with the text. The stage calls it after the entry has closed.
    void Finish();

private:
    struct Key {
        std::string label; // what it types, or the name of a wide key
        int row = 0, column = 0, span = 1;
        gfx::Rect rect;
    };
    enum class Action { None, Done, Cancel };

    Action Press(const Key& key);
    Action OnNav(input::Nav nav);
    void Move(int dx, int dy);
    const Key* Selected() const;
    std::string Typed(const Key& key) const;
    void Append(const std::string& text);
    void Backspace();
    bool CanFinish() const;

    TextRequest request_;
    std::function<void(const std::string&)> on_done_;
    std::vector<Key> keys_;
    std::string text_;
    int row_ = 1, column_ = 0;
    bool shift_ = false;
    bool swallow_nav_ = false; // the next keyboard navigation repeats a key already handled
    bool enter_ = false;       // ... and that key was Enter: it means Done
    float refused_ = 0;        // seconds left of the "cannot finish" shake
};

} // namespace den::ui
