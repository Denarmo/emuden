// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "den/base/json.h"

namespace den {

namespace {

constexpr int MAX_DEPTH = 64;

struct Reader {
    std::string_view text;
    std::size_t at = 0;

    void SkipSpace() {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            ++at;
        }
    }

    bool Take(char c) {
        SkipSpace();
        if (at < text.size() && text[at] == c) {
            ++at;
            return true;
        }
        return false;
    }

    bool TakeWord(std::string_view word) {
        if (text.substr(at, word.size()) == word) {
            at += word.size();
            return true;
        }
        return false;
    }

    static void AppendUtf8(std::string& out, std::uint32_t code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool Hex4(std::uint32_t& out) {
        if (at + 4 > text.size()) {
            return false;
        }
        const auto result = std::from_chars(text.data() + at, text.data() + at + 4, out, 16);
        if (result.ec != std::errc() || result.ptr != text.data() + at + 4) {
            return false;
        }
        at += 4;
        return true;
    }

    // After the opening quote.
    bool String(std::string& out) {
        while (at < text.size()) {
            const char c = text[at++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at >= text.size()) {
                return false;
            }
            const char escaped = text[at++];
            switch (escaped) {
            case 'n':
                out.push_back('\n');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 'b':
            case 'f':
                out.push_back(' ');
                break;
            case 'u': {
                std::uint32_t code = 0;
                if (!Hex4(code)) {
                    return false;
                }
                // Two escapes make one character outside the basic plane.
                if (code >= 0xD800 && code < 0xDC00 && text.substr(at, 2) == "\\u") {
                    const std::size_t back = at;
                    at += 2;
                    std::uint32_t low = 0;
                    if (Hex4(low) && low >= 0xDC00 && low < 0xE000) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        at = back;
                    }
                }
                AppendUtf8(out, code);
                break;
            }
            default:
                out.push_back(escaped);
            }
        }
        return false;
    }

    bool Value(JsonValue& out, int depth) {
        if (depth > MAX_DEPTH) {
            return false;
        }
        SkipSpace();
        if (at >= text.size()) {
            return false;
        }
        const char c = text[at];
        if (c == '{') {
            ++at;
            out.kind = JsonValue::Kind::Object;
            if (Take('}')) {
                return true;
            }
            do {
                if (!Take('"')) {
                    return false;
                }
                std::string key;
                JsonValue value;
                if (!String(key) || !Take(':') || !Value(value, depth + 1)) {
                    return false;
                }
                out.members.emplace_back(std::move(key), std::move(value));
            } while (Take(','));
            return Take('}');
        }
        if (c == '[') {
            ++at;
            out.kind = JsonValue::Kind::Array;
            if (Take(']')) {
                return true;
            }
            do {
                JsonValue value;
                if (!Value(value, depth + 1)) {
                    return false;
                }
                out.items.push_back(std::move(value));
            } while (Take(','));
            return Take(']');
        }
        if (c == '"') {
            ++at;
            out.kind = JsonValue::Kind::String;
            return String(out.string);
        }
        if (TakeWord("true")) {
            out.kind = JsonValue::Kind::Bool;
            out.boolean = true;
            return true;
        }
        if (TakeWord("false")) {
            out.kind = JsonValue::Kind::Bool;
            return true;
        }
        if (TakeWord("null")) {
            return true;
        }
        std::size_t end = at;
        while (end < text.size() &&
               (text[end] == '-' || text[end] == '+' || text[end] == '.' || text[end] == 'e' ||
                text[end] == 'E' || (text[end] >= '0' && text[end] <= '9'))) {
            ++end;
        }
        if (end == at) {
            return false;
        }
        out.kind = JsonValue::Kind::Number;
        const std::string_view token = text.substr(at, end - at);
        if (token.find_first_of(".eE") == std::string_view::npos) {
            long long whole = 0;
            std::from_chars(token.data(), token.data() + token.size(), whole);
            out.number = static_cast<double>(whole);
        } else {
            // Read by hand: strtod follows the process locale.
            std::size_t i = 0;
            const bool negative = token[0] == '-';
            i += negative || token[0] == '+' ? 1 : 0;
            double number = 0.0;
            for (; i < token.size() && token[i] >= '0' && token[i] <= '9'; ++i) {
                number = number * 10.0 + (token[i] - '0');
            }
            if (i < token.size() && token[i] == '.') {
                double scale = 0.1;
                for (++i; i < token.size() && token[i] >= '0' && token[i] <= '9'; ++i) {
                    number += (token[i] - '0') * scale;
                    scale *= 0.1;
                }
            }
            if (i < token.size() && (token[i] == 'e' || token[i] == 'E')) {
                int exponent = 0;
                ++i;
                i += i < token.size() && token[i] == '+' ? 1 : 0;
                std::from_chars(token.data() + i, token.data() + token.size(), exponent);
                number *= std::pow(10.0, exponent);
            }
            out.number = negative ? -number : number;
        }
        at = end;
        return true;
    }
};

} // namespace

const JsonValue* JsonValue::Find(std::string_view key) const {
    for (const auto& [name, value] : members) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

std::string JsonValue::String(std::string_view key) const {
    const JsonValue* value = Find(key);
    return value != nullptr && value->kind == Kind::String ? value->string : std::string();
}

std::optional<JsonValue> ParseJson(std::string_view text) {
    Reader reader{text};
    JsonValue value;
    if (!reader.Value(value, 0)) {
        return std::nullopt;
    }
    reader.SkipSpace();
    return reader.at == text.size() ? std::optional<JsonValue>(std::move(value)) : std::nullopt;
}

namespace {

void WriteString(std::string& out, std::string_view text) {
    static constexpr char HEX[] = "0123456789abcdef";
    out.push_back('"');
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(raw);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20) {
            out += "\\u00";
            out.push_back(HEX[c >> 4]);
            out.push_back(HEX[c & 0xF]);
        } else {
            out.push_back(raw);
        }
    }
    out.push_back('"');
}

void WriteValue(std::string& out, const JsonValue& value, int depth) {
    const auto indent = [&](int level) { out.append(static_cast<std::size_t>(level) * 2, ' '); };
    switch (value.kind) {
    case JsonValue::Kind::Null:
        out += "null";
        break;
    case JsonValue::Kind::Bool:
        out += value.boolean ? "true" : "false";
        break;
    case JsonValue::Kind::Number: {
        char buffer[40];
        const bool whole =
            value.number > -9.0e15 && value.number < 9.0e15 &&
            value.number == static_cast<double>(static_cast<std::int64_t>(value.number));
        if (whole) {
            const auto end = std::to_chars(buffer, buffer + sizeof(buffer),
                                           static_cast<std::int64_t>(value.number));
            out.append(buffer, end.ptr);
        } else {
            out.append(buffer, static_cast<std::size_t>(std::snprintf(buffer, sizeof(buffer),
                                                                      "%.17g", value.number)));
        }
        break;
    }
    case JsonValue::Kind::String:
        WriteString(out, value.string);
        break;
    case JsonValue::Kind::Array:
        if (value.items.empty()) {
            out += "[]";
            break;
        }
        out += "[\n";
        for (std::size_t i = 0; i < value.items.size(); ++i) {
            indent(depth + 1);
            WriteValue(out, value.items[i], depth + 1);
            out += i + 1 < value.items.size() ? ",\n" : "\n";
        }
        indent(depth);
        out.push_back(']');
        break;
    case JsonValue::Kind::Object:
        if (value.members.empty()) {
            out += "{}";
            break;
        }
        out += "{\n";
        for (std::size_t i = 0; i < value.members.size(); ++i) {
            indent(depth + 1);
            WriteString(out, value.members[i].first);
            out += ": ";
            WriteValue(out, value.members[i].second, depth + 1);
            out += i + 1 < value.members.size() ? ",\n" : "\n";
        }
        indent(depth);
        out.push_back('}');
        break;
    }
}

} // namespace

std::string WriteJson(const JsonValue& value) {
    std::string out;
    WriteValue(out, value, 0);
    out.push_back('\n');
    return out;
}

JsonValue JsonString(std::string text) {
    JsonValue value;
    value.kind = JsonValue::Kind::String;
    value.string = std::move(text);
    return value;
}

JsonValue JsonNumber(double number) {
    JsonValue value;
    value.kind = JsonValue::Kind::Number;
    value.number = number;
    return value;
}

JsonValue JsonBool(bool boolean) {
    JsonValue value;
    value.kind = JsonValue::Kind::Bool;
    value.boolean = boolean;
    return value;
}

JsonValue JsonArray(std::vector<JsonValue> items) {
    JsonValue value;
    value.kind = JsonValue::Kind::Array;
    value.items = std::move(items);
    return value;
}

JsonValue JsonObject(std::vector<std::pair<std::string, JsonValue>> members) {
    JsonValue value;
    value.kind = JsonValue::Kind::Object;
    value.members = std::move(members);
    return value;
}

} // namespace den
