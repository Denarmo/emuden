// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "language_packs.h"
#include "string_table.h"

using namespace den::app;

TEST_CASE("texts are read from a JSON object") {
    const auto texts = ParseTexts(R"({"a.b": "One", "c": "Two {}", "n": 3})");
    REQUIRE(texts);
    CHECK(texts->size() == 2);
    CHECK(texts->at("c") == "Two {}");
    CHECK_FALSE(ParseTexts("[1]"));
    CHECK_FALSE(ParseTexts("not json"));
}

TEST_CASE("format fields are counted") {
    CHECK(CountFields("none") == 0);
    CHECK(CountFields("{} of {}") == 2);
    CHECK(CountFields("{{literal}} {}") == 1);
    CHECK(CountFields("{") == -1);
    CHECK(CountFields("{0}") == -1);
    CHECK(CountFields("{:x} {:.1f}") == 2);
    CHECK(CountFields("a }") == -1);
}

TEST_CASE("a pack's texts win, the English stay for what it lacks") {
    StringTable table;
    table.SetBase({{"common.ok", "OK"}, {"home.position", "{} of {}"}});
    CHECK(*table.Find("common.ok") == "OK");
    CHECK(table.Find("Check for updates") == nullptr);

    table.SetPack({{"common.ok", "Aceptar"}, {"Check for updates", "Buscar actualizaciones"}});
    CHECK(*table.Find("common.ok") == "Aceptar");
    CHECK(*table.Find("home.position") == "{} of {}"); // not in the pack: English
    CHECK(*table.Find("Check for updates") == "Buscar actualizaciones");
    CHECK(*table.Pack("common.ok") == "Aceptar");
    CHECK(table.Pack("home.position") == nullptr);

    table.ClearPack();
    CHECK(*table.Find("common.ok") == "OK");
    CHECK_FALSE(table.HasPack());
}

TEST_CASE("a pack text with other fields than the English is refused") {
    StringTable table;
    table.SetBase({{"home.position", "{} of {}"}});
    table.SetPack({{"home.position", "{} de"},             // one field short
                   {"Closed {} games", "Cerrados {} juegos"}, // matches the key's own text
                   {"Closed {} games again", "Cerrados"},     // lost its field
                   {"Save", "Guardar {"}});                   // would break std::format
    CHECK(table.Rejected() == 3);
    CHECK(table.Pack("home.position") == nullptr);
    CHECK(*table.Find("home.position") == "{} of {}");
    CHECK(*table.Pack("Closed {} games") == "Cerrados {} juegos");
    CHECK(table.PackSize() == 1);
}

namespace {

LanguageOffer Offer(const char* plugin, const char* code, const char* name, bool installed) {
    return {plugin, code, name, installed};
}

} // namespace

TEST_CASE("one pack per language, installed ones first") {
    const auto offers = LanguageOffers({Offer("a.es", "es", "Español", false), Offer("b.es", "es", "Español", true),
                                        Offer("c.pt", "pt-BR", "Português", false)});
    REQUIRE(offers.size() == 2);
    CHECK(offers[0].plugin == "b.es");
    CHECK(offers[1].code == "pt-BR");
}

TEST_CASE("the first-run offer is for the system's language, once") {
    const std::vector<LanguageOffer> offers = {Offer("e.es", "es", "Español", false)};
    const std::vector<std::string> spanish = {"es-ES", "en-US"};
    REQUIRE(FirstRunOffer(offers, spanish, "", false));
    CHECK(FirstRunOffer(offers, spanish, "", false)->plugin == "e.es");
    CHECK(FirstRunOffer(offers, spanish, "", true) == nullptr);   // already asked
    CHECK(FirstRunOffer(offers, spanish, "en", false) == nullptr); // the user chose
    CHECK(FirstRunOffer(offers, {"fr-FR"}, "", false) == nullptr); // no pack for French
    CHECK(FirstRunOffer(offers, {"en-US"}, "", false) == nullptr);
    CHECK(FirstRunOffer({Offer("e.es", "es", "Español", true)}, spanish, "", false) == nullptr); // have it
}
