// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.patch and den.library, over FeatureServices. A plugin sees only the systems its
// manifest names and changes only the hacks it installed.

#include <cstdlib>

#include "den/base/files.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr const char* NO_SERVICES = "this version of the app cannot patch games yet";

void SetText(lua_State* L, const char* key, const std::string& value) {
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}

// A game as scripts see it. The cover is an image reference, never a path of the device.
void PushGame(lua_State* L, const GameFacts& game) {
    lua_createtable(L, 0, 6);
    SetText(L, "id", game.id);
    SetText(L, "name", game.name);
    SetText(L, "system", game.system);
    lua_pushinteger(L, static_cast<lua_Integer>(game.size));
    lua_setfield(L, -2, "size");
    SetText(L, "cover", "game:" + game.id);
    if (!game.hack_of.empty()) {
        SetText(L, "hack_of", game.hack_of);
    }
}

template <typename T>
void SetNumber(lua_State* L, const char* key, const std::optional<T>& value) {
    if (value) {
        lua_pushinteger(L, static_cast<lua_Integer>(*value));
        lua_setfield(L, -2, key);
    }
}

// A CRC32 given as a number or as hex text.
std::optional<std::uint32_t> FieldCrc(lua_State* L, int table, const char* key) {
    std::optional<std::uint32_t> crc;
    lua_getfield(L, table, key);
    if (lua_isinteger(L, -1)) {
        crc = static_cast<std::uint32_t>(lua_tointeger(L, -1));
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        char* end = nullptr;
        const char* text = lua_tostring(L, -1);
        const unsigned long long value = std::strtoull(text, &end, 16);
        if (end != text && *end == 0 && value <= 0xffffffffull) {
            crc = static_cast<std::uint32_t>(value);
        }
    }
    lua_pop(L, 1);
    return crc;
}

bool FieldBool(lua_State* L, int table, const char* key) {
    lua_getfield(L, table, key);
    const bool value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

int Inspect(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    const auto services = runner.Config().services;
    if (!runner.Config().manifest.access.Has(PERMISSION_PATCH)) {
        return Refuse(L, "the plugin has no permission to patch games");
    }
    fs::path patch;
    if (const Status found = runner.Resolve(name, false, patch); !found.ok) {
        return Refuse(L, found.error);
    }
    if (!services) {
        return Refuse(L, NO_SERVICES);
    }
    return runner.Suspend(L, [services, patch]() -> Outcome {
        Result<PatchFacts> facts = services->InspectPatch(patch);
        return [facts = std::move(facts)](lua_State* T) {
            if (!facts) {
                return Refuse(T, facts.error());
            }
            lua_createtable(T, 0, 5);
            SetText(T, "format", facts.value().format);
            SetNumber(T, "source_size", facts.value().source_size);
            SetNumber(T, "source_crc32", facts.value().source_crc32);
            SetNumber(T, "target_size", facts.value().target_size);
            SetNumber(T, "target_crc32", facts.value().target_crc32);
            return 1;
        };
    });
}

int Find(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    const FeatureAccess& access = runner.Config().manifest.access;
    const auto services = runner.Config().services;
    if (!access.Has(PERMISSION_LIBRARY_READ)) {
        return Refuse(L, "the plugin has no permission to see your games");
    }
    BaseWanted wanted;
    const std::string system = FieldText(L, 1, "system");
    if (!system.empty()) {
        if (!access.SeesSystem(system)) {
            return Refuse(L, "the plugin did not declare the console " + system);
        }
        wanted.systems = {system};
    } else {
        wanted.systems = access.systems;
    }
    wanted.crc32 = FieldCrc(L, 1, "crc32");
    wanted.sha1 = ToLower(FieldText(L, 1, "sha1"));
    wanted.md5 = ToLower(FieldText(L, 1, "md5"));
    lua_getfield(L, 1, "size");
    if (lua_isinteger(L, -1) && lua_tointeger(L, -1) >= 0) {
        wanted.size = static_cast<std::uint64_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    if (!wanted.crc32 && wanted.sha1.empty() && wanted.md5.empty()) {
        return Refuse(L, "find needs a crc32, sha1 or md5");
    }
    if (!services) {
        return Refuse(L, NO_SERVICES);
    }
    return runner.Suspend(L, [services, wanted, &runner]() -> Outcome {
        std::optional<GameFacts> game = services->FindBase(wanted, runner.Cancel());
        return [game = std::move(game)](lua_State* T) {
            if (!game) {
                lua_pushnil(T);
            } else {
                PushGame(T, *game);
            }
            return 1;
        };
    });
}

int Games(lua_State* L) {
    Runner& runner = Runner::Of(L);
    const FeatureAccess& access = runner.Config().manifest.access;
    if (!access.Has(PERMISSION_LIBRARY_READ)) {
        return Refuse(L, "the plugin has no permission to see your games");
    }
    std::vector<std::string> systems = access.systems;
    if (lua_istable(L, 1)) {
        const std::string system = FieldText(L, 1, "system");
        if (!system.empty()) {
            if (!access.SeesSystem(system)) {
                return Refuse(L, "the plugin did not declare the console " + system);
            }
            systems = {system};
        }
    }
    if (!runner.Config().services) {
        return Refuse(L, NO_SERVICES);
    }
    const std::vector<GameFacts> games = runner.Config().services->Games(systems);
    lua_createtable(L, static_cast<int>(games.size()), 0);
    lua_Integer index = 1;
    for (const GameFacts& game : games) {
        if (access.SeesSystem(game.system)) {
            PushGame(L, game);
            lua_rawseti(L, -2, index++);
        }
    }
    return 1;
}

int Hacks(lua_State* L) {
    Runner& runner = Runner::Of(L);
    lua_newtable(L);
    if (!runner.Config().services) {
        return 1;
    }
    lua_Integer index = 1;
    for (const HackFacts& hack : runner.Config().services->Hacks(runner.Config().manifest.id)) {
        lua_createtable(L, 0, 6);
        SetText(L, "id", hack.id);
        SetText(L, "name", hack.name);
        SetText(L, "base", hack.base);
        SetText(L, "version", hack.version);
        SetText(L, "source", hack.source);
        lua_pushinteger(L, static_cast<lua_Integer>(hack.installed));
        lua_setfield(L, -2, "installed");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// A cover the script names: a file of the plugin's folder. Anything else is no cover, and
// the hack shows its base game's.
fs::path CoverFile(Runner& runner, const std::string& cover) {
    fs::path file;
    std::error_code ec;
    if (cover.empty() || cover.find("://") != std::string::npos ||
        !runner.Resolve(cover, true, file).ok || !fs::is_regular_file(file, ec)) {
        return {};
    }
    return file;
}

int Apply(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    const FeatureManifest& manifest = runner.Config().manifest;
    const auto services = runner.Config().services;
    if (!manifest.access.Has(PERMISSION_PATCH)) {
        return Refuse(L, "the plugin has no permission to patch games");
    }
    if (!services) {
        return Refuse(L, NO_SERVICES);
    }
    HackOrder order;
    if (const Status found = runner.Resolve(FieldText(L, 1, "patch"), false, order.patch);
        !found.ok) {
        return Refuse(L, found.error);
    }
    order.base_id = FieldText(L, 1, "game");
    order.title = Trim(CutText(FieldText(L, 1, "title")));
    order.version = CutText(FieldText(L, 1, "version"));
    order.description = CutText(FieldText(L, 1, "description"));
    order.source = CutText(FieldText(L, 1, "source"));
    order.cover = CoverFile(runner, FieldText(L, 1, "cover"));
    order.keep_old = FieldBool(L, 1, "keep_old");
    order.plugin = manifest.id;
    if (order.title.empty() || order.title.size() > 200) {
        return Refuse(L, "the hack needs a title");
    }
    const std::optional<GameFacts> base = services->Game(order.base_id);
    if (!base || !manifest.access.SeesSystem(base->system)) {
        return Refuse(L, "there is no such game of the plugin's consoles");
    }
    return runner.Suspend(L, [services, order, &runner]() -> Outcome {
        Result<HackMade> made = services->InstallHack(
            order, [&runner](float) { return !runner.Cancel().load(); });
        return [made = std::move(made)](lua_State* T) {
            if (!made) {
                return Refuse(T, made.error());
            }
            lua_createtable(T, 0, 2);
            PushGame(T, made.value().game);
            lua_setfield(T, -2, "game");
            SetText(T, "path", PathToUtf8(made.value().path.filename()));
            return 1;
        };
    });
}

int Finish(lua_State* L, const Status& status) {
    if (!status.ok) {
        return Refuse(L, status.error);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int Update(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    const FeatureManifest& manifest = runner.Config().manifest;
    if (!manifest.access.Has(PERMISSION_LIBRARY_WRITE)) {
        return Refuse(L, "the plugin has no permission to change its hacks");
    }
    if (!runner.Config().services) {
        return Refuse(L, NO_SERVICES);
    }
    std::optional<std::string> title, description;
    if (const std::string text = Trim(CutText(FieldText(L, 2, "title"))); !text.empty()) {
        title = text;
    }
    lua_getfield(L, 2, "description");
    const bool described = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (described) {
        description = CutText(FieldText(L, 2, "description"));
    }
    return Finish(L, runner.Config().services->UpdateHack(
                         id, manifest.id, title, description,
                         CoverFile(runner, FieldText(L, 2, "cover"))));
}

int Remove(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    const FeatureManifest& manifest = runner.Config().manifest;
    if (!manifest.access.Has(PERMISSION_LIBRARY_WRITE)) {
        return Refuse(L, "the plugin has no permission to change its hacks");
    }
    if (!runner.Config().services) {
        return Refuse(L, NO_SERVICES);
    }
    return Finish(L, runner.Config().services->RemoveHack(id, manifest.id));
}

} // namespace

void RegisterLibrary(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "inspect", &Inspect);
    SetFunction(L, runner, "apply", &Apply);
    lua_setfield(L, -2, "patch");
    lua_newtable(L);
    SetFunction(L, runner, "find", &Find);
    SetFunction(L, runner, "games", &Games);
    SetFunction(L, runner, "hacks", &Hacks);
    SetFunction(L, runner, "update", &Update);
    SetFunction(L, runner, "remove", &Remove);
    lua_setfield(L, -2, "library");
}

} // namespace den::plugins::feature
