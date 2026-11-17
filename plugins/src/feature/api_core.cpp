// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.api, den.plugin, den.device, den.log, den.sleep, den.now, den.include, den.storage.

#include <algorithm>
#include <chrono>
#include <thread>

#include "den/base/files.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

constexpr std::size_t MAX_STORAGE = 1u << 20;
constexpr std::size_t MAX_INCLUDE = 1u << 20;

int Log(lua_State* L) {
    std::size_t size = 0;
    const char* text = luaL_tolstring(L, 1, &size);
    Runner::Of(L).AddLog(std::string(text, std::min<std::size_t>(size, 4000)));
    return 0;
}

int Now(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(Runner::Of(L).Config().now()));
    return 1;
}

int Sleep(lua_State* L) {
    const auto ms = std::clamp<lua_Integer>(luaL_checkinteger(L, 1), 0, 600'000);
    Runner& runner = Runner::Of(L);
    return runner.Suspend(L, [&runner, ms]() -> Outcome {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!runner.Cancel().load() && std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return [](lua_State*) { return 0; };
    });
}

int IncludeDone(lua_State*, int, lua_KContext) {
    return 1;
}

// Runs another script of the package and returns its value. The script may wait.
int Include(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    std::filesystem::path file;
    if (const Status found = runner.Resolve("package/" + name, true, file); !found.ok) {
        return luaL_error(L, "include: %s", found.error.c_str());
    }
    const auto text = name.ends_with(".lua") ? ReadTextFile(file) : std::nullopt;
    if (!text || text->size() > MAX_INCLUDE) {
        return luaL_error(L, "include: %s is not a script of the package", name.c_str());
    }
    if (const Status loaded = Sandbox::Of(L).LoadText(L, *text, "@" + name); !loaded.ok) {
        return luaL_error(L, "%s", loaded.error.c_str());
    }
    lua_callk(L, 0, 1, 0, &IncludeDone);
    return 1;
}

int StorageGet(lua_State* L) {
    const std::string key = luaL_checkstring(L, 1);
    const JsonValue* value = Runner::Of(L).Storage().Find(key);
    if (value == nullptr) {
        lua_pushnil(L);
    } else {
        PushJson(L, *value);
    }
    return 1;
}

int StorageSet(lua_State* L) {
    const std::string key = luaL_checkstring(L, 1);
    luaL_checkany(L, 2);
    JsonValue value = ToJson(L, 2);
    Runner& runner = Runner::Of(L);
    JsonValue& storage = runner.Storage();
    JsonValue before = storage;
    const auto member = std::find_if(storage.members.begin(), storage.members.end(),
                                     [&](const auto& m) { return m.first == key; });
    if (member != storage.members.end()) {
        member->second = std::move(value);
    } else {
        storage.members.emplace_back(key, std::move(value));
    }
    if (key.empty() || key.size() > 200 || CompactJson(storage).size() > MAX_STORAGE) {
        storage = std::move(before);
        return Refuse(L, "the plugin's storage is full (1 MiB)");
    }
    if (!runner.SaveStorage()) {
        return Refuse(L, "the storage could not be written");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int StorageRemove(lua_State* L) {
    const std::string key = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    auto& members = runner.Storage().members;
    const auto removed = std::erase_if(members, [&](const auto& m) { return m.first == key; });
    if (removed > 0) {
        runner.SaveStorage();
    }
    lua_pushboolean(L, removed > 0 ? 1 : 0);
    return 1;
}

int StorageKeys(lua_State* L) {
    const auto& members = Runner::Of(L).Storage().members;
    lua_createtable(L, static_cast<int>(members.size()), 0);
    lua_Integer index = 1;
    for (const auto& [key, value] : members) {
        lua_pushlstring(L, key.data(), key.size());
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

std::string Language(const Runner& runner) {
    const auto& language = runner.Config().language;
    const std::string code = language ? language() : std::string();
    return code.empty() ? std::string("en") : code;
}

// den.language(): the app's language code ("en", "es").
int LanguageCode(lua_State* L) {
    const std::string code = Language(Runner::Of(L));
    lua_pushlstring(L, code.data(), code.size());
    return 1;
}

// den.tr(text, ...): the plugin's own translation of `text` in the app's language (its
// manifest's "translations"), else `text`, with each {} filled by the next value.
int Translate(lua_State* L) {
    Runner& runner = Runner::Of(L);
    const std::string text = luaL_checkstring(L, 1);
    const std::string* translated =
        FindTranslation(runner.Config().manifest.translations, Language(runner), text);
    std::vector<std::string> values;
    const int count = lua_gettop(L);
    for (int i = 2; i <= count; ++i) {
        std::size_t size = 0;
        const char* value = luaL_tolstring(L, i, &size);
        values.emplace_back(value, size);
        lua_pop(L, 1);
    }
    const std::string out = FillFields(translated ? *translated : text, values);
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

} // namespace

void RegisterCore(lua_State* L, Runner& runner) {
    const RunnerConfig& config = runner.Config();
    lua_pushinteger(L, API_VERSION);
    lua_setfield(L, -2, "api");
    lua_newtable(L);
    lua_pushstring(L, config.manifest.id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, config.manifest.version.Text().c_str());
    lua_setfield(L, -2, "version");
    lua_setfield(L, -2, "plugin");
    lua_newtable(L);
    lua_pushstring(L, config.platform.c_str());
    lua_setfield(L, -2, "platform");
    lua_setfield(L, -2, "device");

    SetFunction(L, runner, "log", &Log);
    SetFunction(L, runner, "now", &Now);
    SetFunction(L, runner, "language", &LanguageCode);
    SetFunction(L, runner, "tr", &Translate);
    SetFunction(L, runner, "sleep", &Sleep);
    SetFunction(L, runner, "include", &Include);
    lua_newtable(L);
    SetFunction(L, runner, "get", &StorageGet);
    SetFunction(L, runner, "set", &StorageSet);
    SetFunction(L, runner, "remove", &StorageRemove);
    SetFunction(L, runner, "keys", &StorageKeys);
    lua_setfield(L, -2, "storage");
}

} // namespace den::plugins::feature
