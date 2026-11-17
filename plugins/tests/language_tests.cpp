// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Language packs (kind "interface" with a "language") and a core's language option.

#include <catch2/catch_test_macros.hpp>

#include "den/plugins/feature_manifest.h"
#include "den/plugins/index.h"
#include "den/plugins/manifest.h"
#include "den/plugins/translations.h"

using namespace den;
using namespace den::plugins;

namespace {

const char* const PACK_HEAD = R"({"id":"tester.es","name":"Spanish","version":"1.0.0","kind":"interface","api":1,)";

} // namespace

TEST_CASE("a language pack is an interface plugin with a language") {
    Result<FeatureManifest> pack = ParseFeatureManifest(
        std::string(PACK_HEAD) +
        R"("needs":["kind.interface","languages.1"],"language":{"code":"es_ES","name":"Español"}})");
    REQUIRE(pack.ok());
    CHECK(pack->language == "es-ES");
    CHECK(pack->language_name == "Español");

    FeaturePlugin plugin;
    plugin.kind = KIND_INTERFACE;
    plugin.needs = pack->needs;
    CHECK(OffersLanguage(plugin));
    CHECK_FALSE(OffersHomeInterface(plugin)); // not a look for Home
    CHECK_FALSE(OffersThemes(plugin));
}

TEST_CASE("a language pack names its code and asks for the capability") {
    const std::string needs = R"("needs":["kind.interface","languages.1"],)";
    CHECK_FALSE(ParseFeatureManifest(std::string(PACK_HEAD) + R"("needs":["kind.interface"],)" +
                                     R"("language":{"code":"es","name":"Español"}})").ok());
    CHECK_FALSE(ParseFeatureManifest(std::string(PACK_HEAD) + needs + R"("language":{"name":"Español"}})").ok());
    CHECK_FALSE(ParseFeatureManifest(std::string(PACK_HEAD) + needs + R"("language":{"code":"es"}})").ok());
    // English is built in; a pack cannot replace it.
    CHECK_FALSE(ParseFeatureManifest(std::string(PACK_HEAD) + needs +
                                     R"("language":{"code":"en-GB","name":"English"}})").ok());
}

TEST_CASE("the index entry of a language pack carries its code") {
    const std::string index = R"({"index_version":1,"source":{"name":"T"},"sequence":1,"plugins":[{"id":"tester.es","name":"Spanish",
      "version":"1.0.0","kind":"interface","package":{"url":"x.emuplugin","sha256":")" +
                              std::string(64, 'a') + R"(","size":10},"language":"es_ES","language_name":"Español",
      "needs":["kind.interface","languages.1"]}]})";
    Result<Index> parsed = ParseIndex(index);
    REQUIRE(parsed.ok());
    REQUIRE(parsed->plugins.size() == 1);
    CHECK(parsed->plugins[0].language == "es-ES");
    CHECK(parsed->plugins[0].language_name == "Español");
}

TEST_CASE("a core's language option is chosen from the app's language") {
    const std::string json = R"({"manifest_version":1,"id":"jdoe.duo","name":"Duo","version":"1.0.0",
      "kind":"console","engine":"libretro","api":1,"needs":["engine.libretro"],
      "systems":[{"id":"duo","name":"Duo","group":"other","content":{"extensions":["duo"],"sync":false},
        "cores":["first"]}],
      "components":[{"id":"first","type":"libretro-core",
        "language":{"option":"duo_language","values":{"en":"English","es":"Spanish","pt-BR":"Brazilian","*":"English"}},
        "downloads":{"any":{"url":"https://example.org/a.zip","sha256":")" +
                             std::string(64, 'A') + R"(","size":4,"archive":"zip","entry":"a.dll"}}},
        {"id":"plain","type":"libretro-core",
         "downloads":{"any":{"url":"https://example.org/b.zip","sha256":")" +
                             std::string(64, 'B') + R"(","size":4,"archive":"zip","entry":"b.dll"}}}]})";
    Result<Manifest> manifest = ParseManifest(json);
    REQUIRE(manifest.ok());
    const Component* core = manifest->FindComponent("first");
    REQUIRE(core);
    CHECK(core->language_option == "duo_language");
    CHECK(core->LanguageValue("es") == "Spanish");
    CHECK(core->LanguageValue("es-MX") == "Spanish");
    CHECK(core->LanguageValue("pt-BR") == "Brazilian");
    CHECK(core->LanguageValue("pt-PT") == "English"); // falls to "*"
    CHECK(core->LanguageValue("en") == "English");
    const Component* plain = manifest->FindComponent("plain");
    REQUIRE(plain);
    CHECK(plain->LanguageValue("es").empty());
}

TEST_CASE("a plugin's own translations, by language") {
    Result<Manifest> manifest = ParseManifest(
        R"({"manifest_version":1,"id":"tester.gb","name":"Game Boy","version":"1.0.0","kind":"console",)"
        R"("engine":"libretro","api":1,"needs":["engine.libretro"],"systems":[{"id":"tt","name":"T","cores":[]}],)"
        R"("translations":{"es":{"Colour":"Color","{} saves":"{} partidas"},"es_MX":{"Colour":"Colorcito"},)"
        R"("en":{"Colour":"Color"},"fr":7}})");
    REQUIRE(manifest.ok());
    CHECK(manifest->translations.size() == 2); // "en" and a non-object are skipped
    CHECK(TranslationsFor(manifest->translations, "en").empty());
    CHECK(TranslationsFor(manifest->translations, "es").at("Colour") == "Color");
    const Texts mexican = TranslationsFor(manifest->translations, "es-MX");
    CHECK(mexican.at("Colour") == "Colorcito"); // the whole code over its language
    CHECK(mexican.at("{} saves") == "{} partidas");
    CHECK(TranslationsFor(manifest->translations, "de").empty());

    REQUIRE(FindTranslation(manifest->translations, "es-ES", "Colour") != nullptr);
    CHECK(*FindTranslation(manifest->translations, "es-ES", "Colour") == "Color");
    CHECK(FindTranslation(manifest->translations, "es", "Hue") == nullptr);
}

TEST_CASE("fields are filled in order, doubled braces are braces") {
    CHECK(FillFields("{} de {}", {"3", "7"}) == "3 de 7");
    CHECK(FillFields("{{{}}}", {"x"}) == "{x}");
    CHECK(FillFields("{} y {}", {"uno"}) == "uno y ");
    CHECK(FillFields("sin campos", {"a"}) == "sin campos");
}

TEST_CASE("a language pack's texts for other plugins") {
    const Texts texts = ParsePluginStrings(
        R"({"emuden.sync.gdrive":{"Sign in":"Iniciar sesión"},"other.plugin":{"Go":"Ir"},"bad":3})");
    CHECK(texts.at("Sign in") == "Iniciar sesión");
    CHECK(texts.at("Go") == "Ir");
    CHECK(ParsePluginStrings("not json").empty());

    Texts into{{"Go", "Vamos"}};
    AddMissing(into, texts);
    CHECK(into.at("Go") == "Vamos"); // what was there stays
    CHECK(into.at("Sign in") == "Iniciar sesión");
}
