// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A small JSON reader for the answers of web services (Steam's store, Wikipedia).

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace den {

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    /// An object's member, null when this is no object or has no such member.
    const JsonValue* Find(std::string_view key) const;
    /// A member that is a string, "" otherwise.
    std::string String(std::string_view key) const;
};

/// nullopt for text that is not one JSON value (or nests deeper than 64 levels).
std::optional<JsonValue> ParseJson(std::string_view text);

/// The value as JSON text, two spaces per level. Whole numbers are written without a fraction.
std::string WriteJson(const JsonValue& value);

JsonValue JsonString(std::string text);
JsonValue JsonNumber(double number);
JsonValue JsonBool(bool value);
JsonValue JsonArray(std::vector<JsonValue> items = {});
JsonValue JsonObject(std::vector<std::pair<std::string, JsonValue>> members = {});

} // namespace den
