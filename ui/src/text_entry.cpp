// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cctype>

#include "den/ui/text_entry.h"
#include "text_entry_parts.h"

namespace den::ui {

namespace {

using input::Nav;
using namespace text_entry;

constexpr const char* ROWS[] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-"};
constexpr const char* SHIFTED_DIGITS = "!?&+()_:/@";
constexpr float KEYBOARD_WAIT = 1.0f; // seconds the platform's keyboard has to appear

// SDL key codes of the keys a real keyboard edits the line with.
constexpr int KEY_BACKSPACE = 8, KEY_RETURN = 13, KEY_KP_ENTER = 0x40000058;

} // namespace

TextEntry::TextEntry(TextRequest request, std::function<void(const std::string&)> on_done, KeyboardSetup setup,
                     bool by_pad)
    : request_(std::move(request)), on_done_(std::move(on_done)), setup_(setup), text_(request_.text),
      by_pad_(by_pad) {
    for (int row = 0; row < LAST_ROW; ++row) {
        for (int column = 0; column < COLUMNS; ++column) {
            keys_.push_back(Key{std::string(1, ROWS[row][column]), row, column, 1, {}, {}});
        }
    }
    keys_.push_back(Key{"Shift", LAST_ROW, 0, 2, {}, {}});
    keys_.push_back(Key{"Space", LAST_ROW, 2, 4, {}, {}});
    keys_.push_back(Key{"Delete", LAST_ROW, 6, 2, {}, {}});
    keys_.push_back(Key{"Done", LAST_ROW, 8, 2, {}, {}});
    // A new name starts with a capital.
    shift_ = text_.empty();
    StartSystem();
    if (!system_) {
        typing_ = true;
        input::RequestTextInput(true);
    }
}

TextEntry::~TextEntry() {
    if (system_) {
        setup_.system->End();
    }
    if (typing_) {
        input::RequestTextInput(false);
    }
}

// The platform's keyboard takes the typing when the user prefers it and it has a place: the
// other display, or this one for a request made by touch (a pad cannot press its keys, and
// both keyboards do not fit on one display).
void TextEntry::StartSystem() {
    if (!setup_.system || !setup_.prefer_system || !setup_.system->Available() || (by_pad_ && !setup_.apart)) {
        return;
    }
    pal::TextInputRequest wanted;
    wanted.text = text_;
    wanted.placeholder = request_.placeholder;
    wanted.max_length = request_.max_length;
    if (!setup_.system->Begin(wanted, setup_.display)) {
        return;
    }
    system_ = true;
    shown_ = false;
    wait_ = KEYBOARD_WAIT;
    grid_ = by_pad_;
}

// It did not come, or went: the app's own keys take over, on the other display when the
// app draws them there.
void TextEntry::GiveUpSystem() {
    if (!system_) {
        return;
    }
    setup_.system->End();
    system_ = false;
    shown_ = false;
    if (setup_.apart && setup_.keys_apart) {
        keys_apart_ = true;
    } else {
        grid_ = true;
    }
    if (!typing_) {
        typing_ = true;
        input::RequestTextInput(true);
    }
}

std::string TextEntry::Text() const {
    const auto first = text_.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    return text_.substr(first, text_.find_last_not_of(' ') - first + 1);
}

std::string TextEntry::SelectedKey() const {
    const Key* key = Selected();
    return key ? Typed(*key) : std::string();
}

const char* TextEntry::Mode() const {
    return system_ ? "system" : keys_apart_ ? "apart" : "keys";
}

void TextEntry::Finish() {
    if (on_done_) {
        on_done_(Text());
    }
}

const TextEntry::Key* TextEntry::Selected() const {
    for (const Key& key : keys_) {
        if (key.row == row_ && column_ >= key.column && column_ < key.column + key.span) {
            return &key;
        }
    }
    return nullptr;
}

std::string TextEntry::Typed(const Key& key) const {
    if (key.row == LAST_ROW || !shift_) {
        return key.label;
    }
    const char c = key.label[0];
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return std::string(1, SHIFTED_DIGITS[c == '0' ? 9 : c - '1']);
    }
    return std::string(1, c == '\'' ? '"' : static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
}

void TextEntry::Append(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        std::size_t end = i + 1;
        while (end < text.size() && IsContinuation(text[end])) {
            ++end;
        }
        const bool control = static_cast<unsigned char>(text[i]) < 32 || text[i] == 127;
        const bool leading_space = text[i] == ' ' && text_.empty();
        if (!control && !leading_space && Characters(text_) < request_.max_length) {
            text_.append(text, i, end - i);
        }
        i = end;
    }
}

void TextEntry::Backspace() {
    while (!text_.empty()) {
        const char c = text_.back();
        text_.pop_back();
        if (!IsContinuation(c)) {
            break;
        }
    }
}

// The text is another one: the platform's field follows what the app's keys did, and
// whoever follows the typing hears of it.
void TextEntry::Changed(bool from_system) {
    if (system_ && !from_system) {
        setup_.system->SetText(text_);
    }
    if (request_.on_change) {
        request_.on_change(Text());
    }
}

bool TextEntry::CanFinish() const {
    return request_.allow_empty || !Text().empty();
}

TextEntry::Action TextEntry::TryFinish() {
    if (CanFinish()) {
        return Action::Done;
    }
    refused_ = 0.5f;
    return Action::None;
}

void TextEntry::Move(int dx, int dy) {
    row_ = std::clamp(row_ + dy, 0, LAST_ROW);
    if (dx != 0) {
        if (const Key* key = Selected()) {
            column_ = dx > 0 ? key->column + key->span : key->column - 1;
        }
        column_ = (column_ + COLUMNS) % COLUMNS;
    }
}

TextEntry::Action TextEntry::Press(const Key& key) {
    const std::string before = text_;
    Action action = Action::None;
    if (key.row != LAST_ROW) {
        Append(Typed(key));
        shift_ = false;
    } else if (key.label == "Shift") {
        shift_ = !shift_;
    } else if (key.label == "Space") {
        Append(" ");
    } else if (key.label == "Delete") {
        Backspace();
    } else {
        action = TryFinish();
    }
    if (text_ != before) {
        Changed(false);
    }
    return action;
}

TextEntry::Action TextEntry::OnNav(Nav nav) {
    const std::string before = text_;
    Action action = Action::None;
    // Without the app's keys there is nothing to move over: Confirm is Done.
    const bool keys = grid_ || keys_apart_;
    switch (nav) {
    case Nav::Up:
        Move(0, -1);
        break;
    case Nav::Down:
        Move(0, 1);
        break;
    case Nav::Left:
        Move(-1, 0);
        break;
    case Nav::Right:
        Move(1, 0);
        break;
    case Nav::Confirm:
        if (!keys) {
            action = TryFinish();
        } else if (const Key* key = Selected()) {
            return Press(*key);
        }
        break;
    case Nav::Option:
        Backspace();
        break;
    case Nav::Extra:
        Append(" ");
        break;
    case Nav::ShoulderL:
        shift_ = !shift_;
        break;
    case Nav::Menu:
        action = TryFinish();
        break;
    case Nav::Back:
        return Action::Cancel;
    default:
        break;
    }
    if (text_ != before) {
        Changed(false);
    }
    return action;
}

TextEntry::Outcome TextEntry::HandleEvent(const input::Event& event, float scale) {
    using Type = input::Event::Type;
    Action action = Action::None;
    switch (event.type) {
    case Type::Key:
        if (!event.down) {
            break;
        }
        // A real keyboard types into the line. Its keys also arrive as navigation (X, Y, Q, E,
        // Space, Backspace, Enter), which must not act a second time.
        enter_ = event.key == KEY_RETURN || event.key == KEY_KP_ENTER;
        swallow_nav_ = enter_ || event.key == KEY_BACKSPACE || (event.key >= 32 && event.key < 127);
        if (event.key == KEY_BACKSPACE && !text_.empty()) {
            Backspace();
            Changed(false);
        }
        break;
    case Type::Text: {
        const std::string before = text_;
        Append(event.text);
        shift_ = false;
        if (text_ != before) {
            Changed(false);
        }
        break;
    }
    case Type::Nav: {
        const bool from_key = swallow_nav_ && event.family == input::PadFamily::Keyboard;
        swallow_nav_ = false;
        if (!from_key) {
            const bool once = event.nav == Nav::Confirm || event.nav == Nav::Back || event.nav == Nav::Menu ||
                              event.nav == Nav::ShoulderL;
            action = once && event.repeat ? Action::None : OnNav(event.nav);
        } else if (enter_ && !event.repeat) {
            action = TryFinish();
        }
        enter_ = false;
        break;
    }
    case Type::PointerUp: {
        const float x = event.x / scale, y = event.y / scale;
        if (!grid_) {
            // The line alone: its two buttons.
            action = done_rect_.Contains(x, y)     ? TryFinish()
                     : cancel_rect_.Contains(x, y) ? Action::Cancel
                                                   : Action::None;
            break;
        }
        for (const Key& key : keys_) {
            if (key.rect.Contains(x, y)) {
                row_ = key.row;
                column_ = key.column;
                action = Press(key);
                break;
            }
        }
        break;
    }
    default:
        break;
    }
    return action == Action::Done ? Outcome::Done : action == Action::Cancel ? Outcome::Cancelled : Outcome::Open;
}

void TextEntry::KeysPointer(const input::Event& event, float scale) {
    if (!keys_apart_ || event.type != input::Event::Type::PointerUp) {
        return;
    }
    const float x = event.x / scale, y = event.y / scale;
    for (const Key& key : keys_) {
        if (key.far.Contains(x, y)) {
            row_ = key.row;
            column_ = key.column;
            if (const Action action = Press(key); action != Action::None) {
                pending_ = action;
            }
            break;
        }
    }
}

TextEntry::Outcome TextEntry::Update(float dt) {
    Action action = pending_;
    pending_ = Action::None;
    if (system_) {
        std::vector<pal::TextInputEvent> events;
        setup_.system->Poll(events);
        for (const pal::TextInputEvent& event : events) {
            using Kind = pal::TextInputEvent::Kind;
            switch (event.kind) {
            case Kind::Shown:
                shown_ = true;
                break;
            case Kind::Hidden:
                GiveUpSystem();
                break;
            case Kind::Text: {
                // The whole line as the platform has it, held to the same rules as typed text.
                const std::string before = text_;
                text_.clear();
                Append(event.text);
                if (text_ != before) {
                    shift_ = false;
                    Changed(true);
                }
                if (text_ != event.text && system_) {
                    setup_.system->SetText(text_);
                }
                break;
            }
            case Kind::Done:
                action = TryFinish();
                break;
            case Kind::Cancel:
                action = Action::Cancel;
                break;
            }
            if (!system_ || action != Action::None) {
                break;
            }
        }
    }
    if (system_ && !shown_) {
        // Some keyboards refuse a second display: without one after a second, the app's keys.
        wait_ -= dt;
        if (wait_ <= 0) {
            GiveUpSystem();
        }
    }
    return action == Action::Done ? Outcome::Done : action == Action::Cancel ? Outcome::Cancelled : Outcome::Open;
}

} // namespace den::ui
