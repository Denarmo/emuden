// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <random>

#include "ecp/wire.h"

namespace den::engine::ecp {

namespace {

constexpr char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void WriteString(std::string& out, const std::string& text) {
    out += '"';
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (u < 0x20) {
                constexpr char HEX[] = "0123456789abcdef";
                out += "\\u00";
                out += HEX[u >> 4];
                out += HEX[u & 15];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void Write(std::string& out, const JsonValue& value) {
    switch (value.kind) {
    case JsonValue::Kind::Null:
        out += "null";
        break;
    case JsonValue::Kind::Bool:
        out += value.boolean ? "true" : "false";
        break;
    case JsonValue::Kind::Number: {
        if (!std::isfinite(value.number)) {
            out += "0";
        } else if (value.number == std::floor(value.number) && std::fabs(value.number) < 9e15) {
            out += std::to_string(static_cast<long long>(value.number));
        } else {
            std::array<char, 32> buffer{};
            const auto end = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value.number).ptr;
            out.append(buffer.data(), end);
        }
        break;
    }
    case JsonValue::Kind::String:
        WriteString(out, value.string);
        break;
    case JsonValue::Kind::Array:
        out += '[';
        for (std::size_t i = 0; i < value.items.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            Write(out, value.items[i]);
        }
        out += ']';
        break;
    case JsonValue::Kind::Object:
        out += '{';
        for (std::size_t i = 0; i < value.members.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            WriteString(out, value.members[i].first);
            out += ':';
            Write(out, value.members[i].second);
        }
        out += '}';
        break;
    }
}

} // namespace

std::optional<JsonValue> ReadFrame(pal::Stream& stream) {
    std::uint8_t header[4];
    if (!stream.Read(header, sizeof(header))) {
        return std::nullopt;
    }
    const std::size_t size = static_cast<std::size_t>(header[0]) | (static_cast<std::size_t>(header[1]) << 8) |
                             (static_cast<std::size_t>(header[2]) << 16) |
                             (static_cast<std::size_t>(header[3]) << 24);
    if (size == 0 || size > MAX_FRAME) {
        return std::nullopt;
    }
    std::string text(size, '\0');
    if (!stream.Read(text.data(), size)) {
        return std::nullopt;
    }
    std::optional<JsonValue> value = ParseJson(text);
    if (!value || value->kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    return value;
}

bool WriteFrame(pal::Stream& stream, const JsonValue& message) {
    const std::string text = CompactJson(message);
    if (text.size() > MAX_FRAME) {
        return false;
    }
    std::string frame(4, '\0');
    const auto size = static_cast<std::uint32_t>(text.size());
    for (int i = 0; i < 4; ++i) {
        frame[static_cast<std::size_t>(i)] = static_cast<char>((size >> (8 * i)) & 0xff);
    }
    frame += text;
    return stream.Write(frame.data(), frame.size());
}

std::string CompactJson(const JsonValue& value) {
    std::string out;
    Write(out, value);
    return out;
}

std::string Base64Encode(const void* data, std::size_t size) {
    const auto* in = static_cast<const std::uint8_t*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t a = in[i];
        const std::uint32_t b = i + 1 < size ? in[i + 1] : 0;
        const std::uint32_t c = i + 2 < size ? in[i + 2] : 0;
        const std::uint32_t triple = (a << 16) | (b << 8) | c;
        out += BASE64[(triple >> 18) & 63];
        out += BASE64[(triple >> 12) & 63];
        out += i + 1 < size ? BASE64[(triple >> 6) & 63] : '=';
        out += i + 2 < size ? BASE64[triple & 63] : '=';
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> Base64Decode(std::string_view text) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    std::uint32_t bits = 0;
    int count = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') {
            v = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            v = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            v = c - '0' + 52;
        } else if (c == '+') {
            v = 62;
        } else if (c == '/') {
            v = 63;
        } else if (c == '=' || c == '\n' || c == '\r') {
            continue;
        } else {
            return std::nullopt;
        }
        bits = (bits << 6) | static_cast<std::uint32_t>(v);
        if (++count == 4) {
            out.push_back(static_cast<std::uint8_t>(bits >> 16));
            out.push_back(static_cast<std::uint8_t>(bits >> 8));
            out.push_back(static_cast<std::uint8_t>(bits));
            bits = 0;
            count = 0;
        }
    }
    if (count == 2) {
        out.push_back(static_cast<std::uint8_t>(bits >> 4));
    } else if (count == 3) {
        out.push_back(static_cast<std::uint8_t>(bits >> 10));
        out.push_back(static_cast<std::uint8_t>(bits >> 2));
    } else if (count == 1) {
        return std::nullopt;
    }
    return out;
}

std::string NewToken() {
    std::random_device device;
    constexpr char HEX[] = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; ++i) {
        const std::uint32_t word = device();
        for (int b = 0; b < 4; ++b) {
            const std::uint32_t byte = (word >> (8 * b)) & 0xff;
            out += HEX[byte >> 4];
            out += HEX[byte & 15];
        }
    }
    return out;
}

bool SameToken(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

Picture ReadPicture(const JsonValue* value) {
    Picture picture;
    if (value == nullptr || value->kind != JsonValue::Kind::Object) {
        return picture;
    }
    if (auto bytes = Base64Decode(value->String("png"))) {
        picture.png = std::move(*bytes);
        picture.width = static_cast<int>(NumberOf(*value, "width", 0));
        picture.height = static_cast<int>(NumberOf(*value, "height", 0));
    }
    return picture;
}

std::string ValueText(const JsonValue& value) {
    switch (value.kind) {
    case JsonValue::Kind::Bool:
        return value.boolean ? "true" : "false";
    case JsonValue::Kind::Number:
    case JsonValue::Kind::Array:
    case JsonValue::Kind::Object:
        return CompactJson(value);
    case JsonValue::Kind::String:
        return value.string;
    case JsonValue::Kind::Null:
        break;
    }
    return {};
}

std::optional<SettingDef> ReadSetting(const JsonValue& value) {
    SettingDef setting;
    setting.key = value.String("key");
    if (setting.key.empty()) {
        return std::nullopt;
    }
    setting.label = value.String("label");
    if (setting.label.empty()) {
        setting.label = setting.key;
    }
    setting.help = value.String("help");
    setting.type = value.String("type");
    setting.group = value.String("group");
    setting.min = NumberOf(value, "min", 0);
    setting.max = NumberOf(value, "max", 0);
    setting.step = NumberOf(value, "step", 1);
    if (const JsonValue* fallback = value.Find("default")) {
        setting.default_value = ValueText(*fallback);
    }
    const std::string applies = value.String("applies");
    if (!applies.empty()) {
        setting.applies = applies;
    }
    if (const JsonValue* choices = value.Find("choices")) {
        for (const JsonValue& choice : choices->items) {
            // [value, label]
            if (choice.kind == JsonValue::Kind::Array && !choice.items.empty()) {
                const std::string text = ValueText(choice.items[0]);
                const std::string label = choice.items.size() > 1 ? ValueText(choice.items[1]) : text;
                setting.choices.emplace_back(text, label);
            }
        }
    }
    return setting;
}

JsonValue TypedValue(const SettingDef* setting, const std::string& text) {
    if (setting != nullptr && setting->type == "toggle") {
        return JsonBool(text == "true" || text == "1" || text == "on");
    }
    if (setting != nullptr && setting->type == "range") {
        return JsonNumber(std::strtod(text.c_str(), nullptr));
    }
    if (setting != nullptr && setting->type == "choice" && !text.empty()) {
        // A choice keeps the JSON type its values have in the schema ("4" can be a number).
        if (const auto parsed = ParseJson(text);
            parsed && (parsed->kind == JsonValue::Kind::Number || parsed->kind == JsonValue::Kind::Bool)) {
            for (const auto& choice : setting->choices) {
                if (choice.first == text) {
                    return *parsed;
                }
            }
        }
    }
    return JsonString(text);
}

double NumberOf(const JsonValue& object, std::string_view key, double fallback) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Number ? value->number : fallback;
}

bool BoolOf(const JsonValue& object, std::string_view key, bool fallback) {
    const JsonValue* value = object.Find(key);
    return value != nullptr && value->kind == JsonValue::Kind::Bool ? value->boolean : fallback;
}

std::vector<std::string> StringsOf(const JsonValue& object, std::string_view key) {
    std::vector<std::string> out;
    if (const JsonValue* list = object.Find(key)) {
        for (const JsonValue& item : list->items) {
            if (item.kind == JsonValue::Kind::String) {
                out.push_back(item.string);
            }
        }
    }
    return out;
}

JsonValue Message(std::string type, std::int64_t id, std::vector<std::pair<std::string, JsonValue>> fields) {
    std::vector<std::pair<std::string, JsonValue>> members;
    members.emplace_back("type", JsonString(std::move(type)));
    if (id > 0) {
        members.emplace_back("id", JsonNumber(static_cast<double>(id)));
    }
    for (auto& field : fields) {
        members.push_back(std::move(field));
    }
    return JsonObject(std::move(members));
}

std::string ErrorText(const JsonValue& result) {
    const JsonValue* error = result.Find("error");
    if (error == nullptr || error->kind != JsonValue::Kind::Object) {
        return "The engine refused it.";
    }
    std::string message = error->String("message");
    if (message.empty()) {
        const std::string code = error->String("code");
        if (code == "bios_missing") {
            message = "The engine needs a BIOS that is not installed.";
        } else if (code == "firmware_missing") {
            message = "The engine needs firmware that is not installed.";
        } else if (code == "keys_missing") {
            message = "The engine needs keys that are not installed.";
        } else if (code == "content_missing" || code == "content_invalid") {
            message = "The engine cannot read this game.";
        } else if (code == "slot_empty") {
            message = "That slot is empty.";
        } else if (code == "state_incompatible") {
            message = "That state is from another version and cannot be loaded.";
        } else if (code == "unsupported") {
            message = "The engine cannot do that.";
        } else {
            message = code.empty() ? "The engine refused it." : "The engine refused it (" + code + ").";
        }
    }
    return message;
}

} // namespace den::engine::ecp
