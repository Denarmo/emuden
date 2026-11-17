// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The texts the kit draws (labels, rows, buttons, dialogs, toasts) go through the app's
// translator when it has one: a text it knows in the app's language is drawn in that language,
// any other as it is.

#pragma once

#include <string>
#include <string_view>

namespace den::ui {

/// The translation of a text, or null when there is none.
using Translator = const std::string* (*)(std::string_view);

void SetTranslator(Translator translator);

/// `text` in the app's language when the translator has it, else `text` itself.
const std::string& Translate(const std::string& text);

} // namespace den::ui
