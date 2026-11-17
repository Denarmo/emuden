// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/ui/translate.h"

namespace den::ui {

namespace {

Translator g_translator = nullptr;

} // namespace

void SetTranslator(Translator translator) {
    g_translator = translator;
}

const std::string& Translate(const std::string& text) {
    if (g_translator == nullptr || text.empty()) {
        return text;
    }
    const std::string* translated = g_translator(text);
    return translated != nullptr ? *translated : text;
}

} // namespace den::ui
