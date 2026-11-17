// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>
#include <map>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "den/app/strings.h"
#include "string_table.h"

namespace den::app {

namespace {

TextMap LoadFile(const std::string& language) {
    const auto file = pal::AssetsDir() / "strings" / (language + ".json");
    const auto text = ReadTextFile(file);
    if (auto texts = text ? ParseTexts(*text) : std::nullopt) {
        return std::move(*texts);
    }
    log::Warn("strings", "no texts in {}", PathToUtf8(file));
    return {};
}

// Texts of this folder's screens that the strings file does not have yet; the file wins.
TextMap BuiltIn() {
    return {
        {"home.recently_added", "Recently added"},
        {"details.added", "Added: {}"},
        {"statistics.week", "Last 7 days"},
        {"statistics.week_value", "{} in {}"},
        {"statistics.sessions_one", "1 session"},
        {"statistics.sessions_count", "{} sessions"},
    };
}

StringTable& Texts() {
    static StringTable table = [] {
        StringTable loaded;
        TextMap base = BuiltIn();
        for (auto& [key, value] : LoadFile("en")) {
            base[key] = std::move(value);
        }
        loaded.SetBase(std::move(base));
        return loaded;
    }();
    return table;
}

// A key no table has is its own text; kept so the reference stays valid.
std::map<std::string, std::string, std::less<>>& Unknown() {
    static std::map<std::string, std::string, std::less<>> keys;
    return keys;
}

} // namespace

const std::string& Tr(std::string_view key) {
    if (const std::string* text = Texts().Find(key)) {
        return *text;
    }
    auto& unknown = Unknown();
    auto it = unknown.find(key);
    if (it == unknown.end()) {
        it = unknown.emplace(std::string(key), std::string(key)).first;
    }
    return it->second;
}


const std::string* PackText(std::string_view text) {
    return Texts().Pack(text);
}

std::size_t SetLanguagePack(TextMap texts) {
    StringTable& table = Texts();
    table.SetPack(std::move(texts));
    return table.Rejected();
}

void ClearLanguagePack() {
    Texts().ClearPack();
}

} // namespace den::app
