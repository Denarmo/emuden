// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the text entry's logic (text_entry.cpp) and its drawing (text_entry_draw.cpp) share.

#pragma once

#include <algorithm>
#include <string>

namespace den::ui::text_entry {

constexpr int COLUMNS = 10;
constexpr int LAST_ROW = 4;
constexpr float KEY_H = 44, KEY_GAP = 6, PANEL_PAD = 28;

inline bool IsContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

inline int Characters(const std::string& text) {
    return static_cast<int>(std::count_if(text.begin(), text.end(), [](char c) { return !IsContinuation(c); }));
}

} // namespace den::ui::text_entry
