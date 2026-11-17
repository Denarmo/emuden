// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "den/base/language.h"

using namespace den;

TEST_CASE("language codes are normalised") {
    CHECK(NormalizeLanguage("es_ES.UTF-8") == "es-ES");
    CHECK(NormalizeLanguage("es-es") == "es-ES");
    CHECK(NormalizeLanguage("ES") == "es");
    CHECK(NormalizeLanguage("es-419") == "es-419");
    CHECK(NormalizeLanguage("zh-Hans-CN") == "zh-CN");
    CHECK(NormalizeLanguage("sr_RS@latin") == "sr-RS");
    CHECK(NormalizeLanguage("C").empty());
    CHECK(NormalizeLanguage("").empty());
    CHECK(NormalizeLanguage("1234").empty());
    CHECK(LanguageBase("pt-BR") == "pt");
    CHECK(LanguageBase("es") == "es");
}

TEST_CASE("the system's languages pick a pack") {
    const std::vector<std::string> packs = {"es", "pt-BR"};
    CHECK(MatchLanguage({"es-MX", "en-US"}, packs) == "es");
    CHECK(MatchLanguage({"en-US", "es-ES"}, packs) == "es"); // English is built in: look on
    CHECK(MatchLanguage({"pt-PT"}, packs) == "pt-BR");       // same language, other region
    CHECK(MatchLanguage({"pt-BR"}, {"pt-PT", "pt-BR"}) == "pt-BR"); // the exact one first
    CHECK(MatchLanguage({"fr-FR"}, packs).empty());
    CHECK(MatchLanguage({"en-GB"}, packs).empty());
    CHECK(MatchLanguage({}, packs).empty());
    CHECK(MatchLanguage({"es"}, {}).empty());
}

TEST_CASE("an app language maps to libretro's") {
    CHECK(RetroLanguage("en") == 0);
    CHECK(RetroLanguage("es") == 3);
    CHECK(RetroLanguage("es-MX") == 3);
    CHECK(RetroLanguage("pt-BR") == 7);
    CHECK(RetroLanguage("pt-PT") == 8);
    CHECK(RetroLanguage("zh-TW") == 11);
    CHECK(RetroLanguage("zh-CN") == 12);
    CHECK(RetroLanguage("xx") == 0);
}
