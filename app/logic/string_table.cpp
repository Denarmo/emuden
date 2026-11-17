// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "string_table.h"

#include <utility>

#include "den/base/json.h"

namespace den::app {

std::optional<TextMap> ParseTexts(std::string_view json) {
    const auto root = ParseJson(json);
    if (!root || root->kind != JsonValue::Kind::Object) {
        return std::nullopt;
    }
    TextMap out;
    for (const auto& [key, value] : root->members) {
        if (value.kind == JsonValue::Kind::String) {
            out[key] = value.string;
        }
    }
    return out;
}

int CountFields(std::string_view text) {
    int fields = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            if (i + 1 < text.size() && text[i + 1] == '{') {
                ++i;
                continue;
            }
            // "{}" or "{:spec}"; an argument index ("{0}") would not do for a pack.
            const std::size_t close = text.find('}', i);
            if (close == std::string_view::npos || (close > i + 1 && text[i + 1] != ':')) {
                return -1;
            }
            ++fields;
            i = close;
        } else if (text[i] == '}') {
            if (i + 1 < text.size() && text[i + 1] == '}') {
                ++i;
            } else {
                return -1;
            }
        }
    }
    return fields;
}

void StringTable::SetBase(TextMap texts) {
    base_ = std::move(texts);
}

TextMap StringTable::Checked(TextMap texts) {
    TextMap out;
    rejected_ = 0;
    for (auto& [key, value] : texts) {
        const auto base = base_.find(key);
        const int wanted = CountFields(base != base_.end() ? base->second : key);
        if (wanted < 0 || CountFields(value) != wanted) {
            ++rejected_;
            continue;
        }
        out[key] = std::move(value);
    }
    return out;
}

void StringTable::SetPack(TextMap texts) {
    pack_ = Checked(std::move(texts));
}

void StringTable::SetPluginTexts(TextMap texts) {
    plugins_ = Checked(std::move(texts));
}

void StringTable::ClearPack() {
    pack_.clear();
    plugins_.clear();
    rejected_ = 0;
}

const std::string* StringTable::Pack(std::string_view key) const {
    if (const auto it = pack_.find(key); it != pack_.end()) {
        return &it->second;
    }
    const auto it = plugins_.find(key);
    return it == plugins_.end() ? nullptr : &it->second;
}

const std::string* StringTable::Find(std::string_view key) const {
    if (const std::string* text = Pack(key)) {
        return text;
    }
    const auto it = base_.find(key);
    return it == base_.end() ? nullptr : &it->second;
}

} // namespace den::app
