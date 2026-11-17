// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A plugin's own texts in other languages: "translations" in its plugin.json, by language code,
// each text by its English one ({"es": {"Screens": "Pantallas"}}). A language pack may add texts
// for other plugins in plugin_strings.json ({"<plugin id>": {"Screens": "Pantallas"}}).

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/json.h"

namespace den::plugins {

using Texts = std::map<std::string, std::string, std::less<>>;
using Translations = std::map<std::string, Texts, std::less<>>; // by language code

/// The language pack's file of texts for other plugins.
inline constexpr const char* PLUGIN_STRINGS_FILE = "plugin_strings.json";

/// "translations" of a manifest: the string members of each language's object. Codes are
/// normalised ("es_ES" is "es-ES"); a value that is not an object, or an English one, is left out.
Translations ParseTranslations(const JsonValue* value);

/// The texts for an app language: the language part's ("es") with the whole code's ("es-MX")
/// over them. Nothing for English.
Texts TranslationsFor(const Translations& translations, std::string_view language);

/// The translation of one text in an app language (the whole code's, else the language part's);
/// null when there is none.
const std::string* FindTranslation(const Translations& translations, std::string_view language,
                                   std::string_view text);

/// `text` with each "{}" replaced by the next of `values` ("{{" and "}}" are braces); a field
/// without a value stays empty.
std::string FillFields(std::string_view text, const std::vector<std::string>& values);

/// Adds `from` to `into`; a text already there stays.
void AddMissing(Texts& into, const Texts& from);

/// A language pack's plugin_strings.json: every plugin's texts in one table. Nothing for text
/// that is not a JSON object of objects.
Texts ParsePluginStrings(std::string_view json);

} // namespace den::plugins
