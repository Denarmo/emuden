// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "den/base/files.h"
#include "den/base/json.h"

namespace den::plugins::json {

inline double Number(const JsonValue& object, std::string_view key, double fallback) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Number ? value->number : fallback;
}

inline bool Bool(const JsonValue& object, std::string_view key, bool fallback) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Bool ? value->boolean : fallback;
}

inline std::vector<std::string> Strings(const JsonValue& object, std::string_view key,
                                        bool lower) {
    std::vector<std::string> out;
    if (const JsonValue* list = object.Find(key)) {
        for (const JsonValue& item : list->items) {
            if (item.kind == JsonValue::Kind::String && !item.string.empty()) {
                out.push_back(lower ? ToLower(item.string) : item.string);
            }
        }
    }
    return out;
}

inline JsonValue StringArray(const std::vector<std::string>& values) {
    std::vector<JsonValue> items;
    for (const std::string& value : values) {
        items.push_back(JsonString(value));
    }
    return JsonArray(std::move(items));
}

} // namespace den::plugins::json
