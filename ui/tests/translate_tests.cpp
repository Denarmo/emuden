// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/ui/translate.h"

using namespace den::ui;

namespace {

const std::string* Spanish(std::string_view text) {
    static const std::string back = "Atrás";
    return text == "Back" ? &back : nullptr;
}

} // namespace

TEST_CASE("the kit draws a text in the app's language when the translator has it") {
    const std::string back = "Back", other = "Something else", empty;
    SetTranslator(nullptr);
    CHECK(&Translate(back) == &back); // no translator: the text itself

    SetTranslator(&Spanish);
    CHECK(Translate(back) == "Atrás");
    CHECK(&Translate(other) == &other); // no translation: the text itself
    CHECK(&Translate(empty) == &empty);

    SetTranslator(nullptr);
    CHECK(Translate(back) == "Back");
}
