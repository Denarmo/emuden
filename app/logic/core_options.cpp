// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core_options.h"

#include <algorithm>
#include <cctype>

#include "den/base/json.h"

namespace den::app {

namespace {

std::string Plain(const std::string& text) {
    std::string out;
    for (const char c : text) {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    }
    return out;
}

} // namespace

std::string WriteKnownOptions(const std::vector<KnownOption>& options) {
    JsonValue list = JsonArray();
    for (const KnownOption& option : options) {
        JsonValue values = JsonArray();
        for (const std::string& value : option.values) {
            values.items.push_back(JsonString(value));
        }
        list.items.push_back(JsonObject({{"key", JsonString(option.key)},
                                         {"label", JsonString(option.label)},
                                         {"value", JsonString(option.fallback)},
                                         {"values", std::move(values)}}));
    }
    return WriteJson(list);
}

std::vector<KnownOption> ParseKnownOptions(std::string_view text) {
    std::vector<KnownOption> out;
    const auto parsed = ParseJson(text);
    if (!parsed) {
        return out;
    }
    for (const JsonValue& item : parsed->items) {
        KnownOption option{item.String("key"), item.String("label"), item.String("value"), {}};
        if (const JsonValue* values = item.Find("values")) {
            for (const JsonValue& value : values->items) {
                option.values.push_back(value.string);
            }
        }
        if (!option.key.empty() && !option.values.empty()) {
            out.push_back(std::move(option));
        }
    }
    return out;
}

std::string KnownOptionsFileName(const std::string& core_id) {
    return Plain(core_id) + ".json";
}

std::string OptionKey(const std::string& core_id, const std::string& option_key) {
    return "core_option." + Plain(core_id) + "." + Plain(option_key);
}

std::string GlobalOptionKey(const std::string& core_name, const std::string& option_key) {
    std::string name;
    for (const char c : core_name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    std::string safe = option_key;
    std::replace(safe.begin(), safe.end(), '.', '_');
    return "core_options." + name + "." + safe;
}

} // namespace den::app
