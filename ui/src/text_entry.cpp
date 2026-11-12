// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cctype>
#include <cmath>

#include "den/ui/glyphs.h"
#include "den/ui/selection.h"
#include "den/ui/text_entry.h"

namespace den::ui {

namespace {

using input::Nav;

constexpr int COLUMNS = 10;
constexpr int LAST_ROW = 4;
constexpr const char* ROWS[] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-"};
constexpr const char* SHIFTED_DIGITS = "!?&+()_:/@";
constexpr float KEY_H = 44, KEY_GAP = 6, PANEL_PAD = 28;

// SDL key codes of the keys a real keyboard edits the line with.
constexpr int KEY_BACKSPACE = 8, KEY_RETURN = 13, KEY_KP_ENTER = 0x40000058;

bool IsContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

int Characters(const std::string& text) {
    return static_cast<int>(std::count_if(text.begin(), text.end(), [](char c) { return !IsContinuation(c); }));
}

} // namespace

TextEntry::TextEntry(TextRequest request, std::function<void(const std::string&)> on_done)
    : request_(std::move(request)), on_done_(std::move(on_done)), text_(request_.text) {
    for (int row = 0; row < LAST_ROW; ++row) {
        for (int column = 0; column < COLUMNS; ++column) {
            keys_.push_back(Key{std::string(1, ROWS[row][column]), row, column, 1, {}});
        }
    }
    keys_.push_back(Key{"Shift", LAST_ROW, 0, 2, {}});
    keys_.push_back(Key{"Space", LAST_ROW, 2, 4, {}});
    keys_.push_back(Key{"Delete", LAST_ROW, 6, 2, {}});
    keys_.push_back(Key{"Done", LAST_ROW, 8, 2, {}});
    // A new name starts with a capital.
    shift_ = text_.empty();
    input::RequestTextInput(true);
}

TextEntry::~TextEntry() {
    input::RequestTextInput(false);
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

bool TextEntry::CanFinish() const {
    return request_.allow_empty || !Text().empty();
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
    if (key.row != LAST_ROW) {
        Append(Typed(key));
        shift_ = false;
    } else if (key.label == "Shift") {
        shift_ = !shift_;
    } else if (key.label == "Space") {
        Append(" ");
    } else if (key.label == "Delete") {
        Backspace();
    } else if (CanFinish()) {
        return Action::Done;
    } else {
        refused_ = 0.5f;
    }
    return Action::None;
}

TextEntry::Action TextEntry::OnNav(Nav nav) {
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
        if (const Key* key = Selected()) {
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
        if (CanFinish()) {
            return Action::Done;
        }
        refused_ = 0.5f;
        break;
    case Nav::Back:
        return Action::Cancel;
    default:
        break;
    }
    return Action::None;
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
        if (event.key == KEY_BACKSPACE) {
            Backspace();
        }
        break;
    case Type::Text:
        Append(event.text);
        shift_ = false;
        break;
    case Type::Nav: {
        const bool from_key = swallow_nav_ && event.family == input::PadFamily::Keyboard;
        swallow_nav_ = false;
        if (!from_key) {
            const bool once = event.nav == Nav::Confirm || event.nav == Nav::Back || event.nav == Nav::Menu ||
                              event.nav == Nav::ShoulderL;
            action = once && event.repeat ? Action::None : OnNav(event.nav);
        } else if (enter_ && !event.repeat) {
            if (CanFinish()) {
                action = Action::Done;
            } else {
                refused_ = 0.5f;
            }
        }
        enter_ = false;
        break;
    }
    case Type::PointerUp: {
        const float x = event.x / scale, y = event.y / scale;
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

void TextEntry::Draw(Context& ctx) {
    const Theme& theme = *ctx.theme;
    gfx::Canvas& canvas = *ctx.canvas;
    gfx::Text& text = ctx.Text();
    refused_ = std::max(0.0f, refused_ - ctx.dt);
    canvas.FillRect(gfx::Rect{0, 0, canvas.Width(), canvas.Height()}, theme.scrim);

    const float w = std::min(630.0f, canvas.Width() - 32);
    const float key_w = (w - PANEL_PAD * 2 - KEY_GAP * (COLUMNS - 1)) / COLUMNS;
    const float keys_h = KEY_H * (LAST_ROW + 1) + KEY_GAP * LAST_ROW;
    const float h = 24 + 30 + 14 + 52 + 16 + keys_h + 16 + 26 + 22;
    const gfx::Rect box{(canvas.Width() - w) * 0.5f, (canvas.Height() - h) * 0.5f, w, h};
    canvas.FillRect(box, theme.dialog, theme.radius + 6);
    const float left = box.x + PANEL_PAD, inner = w - PANEL_PAD * 2;
    canvas.DrawText(text.Elide(request_.title, theme.text_heading, inner, gfx::FONT_BOLD), left, box.y + 24,
                    theme.text_heading, theme.fg, gfx::FONT_BOLD);

    // The line: its end stays in view, with a caret after it.
    const float shake = refused_ > 0 ? std::sin(refused_ * 50.0f) * 5.0f * refused_ : 0.0f;
    const gfx::Rect field{left + shake, box.y + 68, inner, 52};
    canvas.FillRect(field, theme.panel, theme.radius);
    const float size = theme.text_body + 2;
    const std::string count = std::to_string(Characters(text_)) + "/" + std::to_string(request_.max_length);
    const float count_w = text.Measure(count, theme.text_small);
    canvas.DrawText(count, field.Right() - 16 - count_w, field.y + 18, theme.text_small, theme.text_dim);
    const gfx::Rect line{field.x + 16, field.y, field.w - 48 - count_w, field.h};
    const float text_y = field.y + (field.h - size * 1.22f) * 0.5f;
    canvas.PushClip(line);
    const float text_w = text.Measure(text_, size);
    const float text_x = line.x + std::min(0.0f, line.w - 4 - text_w);
    if (text_.empty()) {
        canvas.DrawText(request_.placeholder, line.x + 8, text_y, size, theme.text_dim);
    } else {
        canvas.DrawText(text_, text_x, text_y, size, theme.fg);
    }
    if (std::fmod(ctx.time, 1.0) < 0.6) {
        canvas.FillRect(gfx::Rect{text_x + text_w + 2, field.y + 13, 2, field.h - 26}, theme.fg);
    }
    canvas.PopClip();

    const float keys_y = field.Bottom() + 16;
    const Key* selected = Selected();
    for (Key& key : keys_) {
        key.rect = gfx::Rect{left + static_cast<float>(key.column) * (key_w + KEY_GAP),
                             keys_y + static_cast<float>(key.row) * (KEY_H + KEY_GAP),
                             key_w * static_cast<float>(key.span) + KEY_GAP * static_cast<float>(key.span - 1),
                             KEY_H};
        // Shift held is shown inverted; the selection is the border alone.
        const bool lit = key.label == "Shift" && key.row == LAST_ROW && shift_;
        canvas.FillRect(key.rect, lit ? theme.fg : &key == selected ? theme.panel_strong : theme.panel,
                        theme.radius_small);
        const std::string label = Typed(key);
        const bool wide = key.row == LAST_ROW;
        const float label_size = wide ? theme.text_small + 1 : theme.text_body + 1;
        const float label_w = text.Measure(label, label_size, gfx::FONT_BOLD);
        canvas.DrawText(label, key.rect.CenterX() - label_w * 0.5f,
                        key.rect.y + (KEY_H - label_size * 1.22f) * 0.5f, label_size,
                        lit ? theme.dialog : theme.fg, gfx::FONT_BOLD);
    }
    if (selected) {
        DrawSelectionBorder(canvas, selected->rect, theme.radius_small, ctx.time);
    }

    std::vector<std::pair<Nav, const char*>> hints;
    if (ctx.family == input::PadFamily::Keyboard) {
        hints = {{Nav::Confirm, "Done"}, {Nav::Back, "Cancel"}};
    } else {
        hints = {{Nav::Option, "Delete"}, {Nav::Extra, "Space"}, {Nav::ShoulderL, "Shift"},
                 {Nav::Menu, "Done"},     {Nav::Back, "Cancel"}};
    }
    float x = left;
    const float hints_y = keys_y + keys_h + 16;
    for (const auto& [nav, what] : hints) {
        x += DrawHint(ctx, ctx.family, nav, what, x, hints_y, 24, theme.text_dim) + 18;
    }
}

} // namespace den::ui
