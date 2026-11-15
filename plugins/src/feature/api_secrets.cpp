// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.secrets: like den.storage, but kept in the platform's credential store and never
// synced. The names (not the values) are listed in the plugin's folder, so removing the
// plugin with its data removes its secrets too.

#include <algorithm>

#include "den/base/files.h"
#include "den/platform/pal.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

constexpr std::size_t MAX_SECRET = 2048;
constexpr std::size_t MAX_SECRETS = 32;

pal::Secrets& Store(Runner& runner) {
    return runner.Config().secrets != nullptr ? *runner.Config().secrets : pal::GetSecrets();
}

bool IsKey(const std::string& key) {
    return !key.empty() && key.size() <= 64 && std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
               c == '_' || c == '-';
    });
}

std::vector<std::string> Names(Runner& runner) {
    std::vector<std::string> names;
    if (const auto text = ReadTextFile(runner.Config().data_dir / "secret-keys.json")) {
        if (const auto list = ParseJson(*text); list && list->kind == JsonValue::Kind::Array) {
            for (const JsonValue& item : list->items) {
                names.push_back(item.string);
            }
        }
    }
    return names;
}

void SaveNames(Runner& runner, const std::vector<std::string>& names) {
    JsonValue list = JsonArray();
    for (const std::string& name : names) {
        list.items.push_back(JsonString(name));
    }
    WriteFileAtomic(runner.Config().data_dir / "secret-keys.json", CompactJson(list));
}

int Get(lua_State* L) {
    const std::string key = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    if (!IsKey(key)) {
        return Refuse(L, "a secret's name is up to 64 letters, digits, '.', '_' or '-'");
    }
    const auto stored = Store(runner).Get(SecretName(runner.Config().manifest.id, key));
    const auto value = stored ? ParseJson(*stored) : std::nullopt;
    if (!value) {
        lua_pushnil(L);
    } else {
        PushJson(L, *value);
    }
    return 1;
}

int Set(lua_State* L) {
    const std::string key = luaL_checkstring(L, 1);
    luaL_checkany(L, 2);
    Runner& runner = Runner::Of(L);
    if (!IsKey(key)) {
        return Refuse(L, "a secret's name is up to 64 letters, digits, '.', '_' or '-'");
    }
    const std::string text = CompactJson(ToJson(L, 2));
    if (text.size() > MAX_SECRET) {
        return Refuse(L, "a secret is at most 2 KiB");
    }
    std::vector<std::string> names = Names(runner);
    if (std::find(names.begin(), names.end(), key) == names.end()) {
        if (names.size() >= MAX_SECRETS) {
            return Refuse(L, "a plugin keeps at most 32 secrets");
        }
        names.push_back(key);
        SaveNames(runner, names);
    }
    if (const Status stored = Store(runner).Set(SecretName(runner.Config().manifest.id, key), text);
        !stored.ok) {
        return Refuse(L, stored.error);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int Remove(lua_State* L) {
    const std::string key = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    if (!IsKey(key)) {
        return Refuse(L, "a secret's name is up to 64 letters, digits, '.', '_' or '-'");
    }
    Store(runner).Remove(SecretName(runner.Config().manifest.id, key));
    std::vector<std::string> names = Names(runner);
    if (std::erase(names, key) > 0) {
        SaveNames(runner, names);
    }
    lua_pushboolean(L, 1);
    return 1;
}

} // namespace

std::string SecretName(const std::string& plugin, const std::string& key) {
    return "plugin/" + plugin + "/" + key;
}

void RemoveSecrets(const std::string& plugin, const std::filesystem::path& data_dir) {
    if (const auto text = ReadTextFile(data_dir / "secret-keys.json")) {
        if (const auto list = ParseJson(*text); list && list->kind == JsonValue::Kind::Array) {
            for (const JsonValue& item : list->items) {
                pal::GetSecrets().Remove(SecretName(plugin, item.string));
            }
        }
    }
}

void RegisterSecrets(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "get", &Get);
    SetFunction(L, runner, "set", &Set);
    SetFunction(L, runner, "remove", &Remove);
    lua_setfield(L, -2, "secrets");
}

} // namespace den::plugins::feature
