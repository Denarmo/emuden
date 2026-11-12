// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>

#include "den/app/app.h"
#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/log.h"
#include "strings.h"

namespace den::app {

namespace {

using Table = std::map<std::string, std::string, std::less<>>;

void Load(Table& table, const std::string& language) {
    const auto file = pal::AssetsDir() / "strings" / (language + ".json");
    const auto text = ReadTextFile(file);
    const auto json = text ? ParseJson(*text) : std::nullopt;
    if (!json || json->kind != JsonValue::Kind::Object) {
        log::Warn("strings", "no texts in {}", PathToUtf8(file));
        return;
    }
    for (const auto& [key, value] : json->members) {
        if (value.kind == JsonValue::Kind::String) {
            table[key] = value.string;
        }
    }
}

Table& Texts() {
    static Table table = [] {
        Table loaded;
        Load(loaded, "en");
        const std::string language =
            Svc().settings ? Svc().settings->GetString("ui.language", "en") : std::string("en");
        if (language != "en") {
            Load(loaded, language);
        }
        return loaded;
    }();
    return table;
}

} // namespace

const std::string& Tr(std::string_view key) {
    Table& table = Texts();
    auto it = table.find(key);
    if (it == table.end()) {
        it = table.emplace(std::string(key), std::string(key)).first;
    }
    return it->second;
}

} // namespace den::app
