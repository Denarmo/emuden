// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The launcher's texts by key: assets/strings/en.json, and over it the language pack of the
// language in "ui.language" (docs/languages.md). A key is a dotted name or the English text
// itself; a key with no text anywhere is shown as it is, so English text in the code works
// as it stands and a pack translates it by that text.

#pragma once

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>

#include "string_table.h"

namespace den::app {

const std::string& Tr(std::string_view key);

/// A text with {} placeholders filled in.
template <typename... Args>
std::string TrF(std::string_view key, const Args&... args) {
    const std::string& text = Tr(key);
    try {
        return std::vformat(text, std::make_format_args(args...));
    } catch (const std::format_error&) {
        return text; // a text with fields the call does not fill
    }
}

/// The pack's text for `text` (a key), or null: what the UI kit draws in place of an English text.
const std::string* PackText(std::string_view text);
/// Makes `texts` the language pack in use; returns how many of its texts were refused.
std::size_t SetLanguagePack(TextMap texts);
/// Back to English.
void ClearLanguagePack();

/// "1 game", "5 games".
inline std::string GameCount(std::size_t count) {
    return count == 1 ? Tr("library.count_one") : TrF("library.count", count);
}

} // namespace den::app
