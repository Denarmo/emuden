// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/plugins/translations.h"

#include "den/base/language.h"

namespace den::plugins {

namespace {

Texts StringMembers(const JsonValue& object) {
    Texts out;
    for (const auto& [key, value] : object.members) {
        if (value.kind == JsonValue::Kind::String && !key.empty()) {
            out[key] = value.string;
        }
    }
    return out;
}

} // namespace

Translations ParseTranslations(const JsonValue* value) {
    Translations out;
    if (value == nullptr || value->kind != JsonValue::Kind::Object) {
        return out;
    }
    for (const auto& [code, texts] : value->members) {
        const std::string normal = NormalizeLanguage(code);
        if (normal.empty() || LanguageBase(normal) == "en" || texts.kind != JsonValue::Kind::Object) {
            continue;
        }
        Texts& into = out[normal];
        for (auto& [key, text] : StringMembers(texts)) {
            into[key] = std::move(text);
        }
    }
    return out;
}

Texts TranslationsFor(const Translations& translations, std::string_view language) {
    const std::string whole = NormalizeLanguage(language);
    const std::string base = LanguageBase(whole);
    Texts out;
    if (whole.empty() || base == "en") {
        return out;
    }
    if (const auto it = translations.find(whole); it != translations.end()) {
        out = it->second;
    }
    if (const auto it = translations.find(base); it != translations.end() && base != whole) {
        AddMissing(out, it->second);
    }
    return out;
}

const std::string* FindTranslation(const Translations& translations, std::string_view language,
                                   std::string_view text) {
    const std::string whole = NormalizeLanguage(language);
    for (const std::string& code : {whole, LanguageBase(whole)}) {
        const auto texts = translations.find(code);
        if (code.empty() || texts == translations.end()) {
            continue;
        }
        if (const auto it = texts->second.find(text); it != texts->second.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

std::string FillFields(std::string_view text, const std::vector<std::string>& values) {
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char after = i + 1 < text.size() ? text[i + 1] : '\0';
        if ((c == '{' && after == '{') || (c == '}' && after == '}')) {
            out += c;
            ++i;
        } else if (c == '{' && after == '}') {
            if (next < values.size()) {
                out += values[next];
            }
            ++next;
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

void AddMissing(Texts& into, const Texts& from) {
    for (const auto& [key, text] : from) {
        into.emplace(key, text);
    }
}

Texts ParsePluginStrings(std::string_view json) {
    Texts out;
    const auto root = ParseJson(json);
    if (!root || root->kind != JsonValue::Kind::Object) {
        return out;
    }
    for (const auto& [plugin, texts] : root->members) {
        if (texts.kind == JsonValue::Kind::Object) {
            AddMissing(out, StringMembers(texts));
        }
    }
    return out;
}

} // namespace den::plugins
