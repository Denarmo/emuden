// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Lua values to JSON and back, and den.json.

#include <cmath>
#include <cstdio>

#include "feature/sandbox.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

constexpr int MAX_DEPTH = 32;
constexpr int MAX_PARTS = 200'000;

// The address of this is den.json.null.
const char NULL_MARK = 0;

JsonValue Convert(lua_State* L, int index, int depth, int& parts) {
    if (++parts > MAX_PARTS) {
        luaL_error(L, "the value is too large");
    }
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TNIL:
        return JsonValue();
    case LUA_TBOOLEAN:
        return JsonBool(lua_toboolean(L, index) != 0);
    case LUA_TNUMBER: {
        const double number = lua_tonumber(L, index);
        if (!std::isfinite(number)) {
            luaL_error(L, "a number that is not finite has no JSON form");
        }
        return JsonNumber(number);
    }
    case LUA_TSTRING: {
        std::size_t size = 0;
        const char* text = lua_tolstring(L, index, &size);
        return JsonString(std::string(text, size));
    }
    case LUA_TLIGHTUSERDATA:
        if (lua_touserdata(L, index) == &NULL_MARK) {
            return JsonValue();
        }
        break;
    case LUA_TTABLE: {
        if (depth >= MAX_DEPTH) {
            luaL_error(L, "the table is nested too deeply");
        }
        luaL_checkstack(L, 4, "the table is nested too deeply");
        const lua_Unsigned length = lua_rawlen(L, index);
        lua_pushnil(L);
        const bool empty = lua_next(L, index) == 0;
        if (!empty) {
            lua_pop(L, 2);
        }
        if (length > 0 || empty) {
            std::vector<JsonValue> items;
            for (lua_Unsigned i = 1; i <= length; ++i) {
                lua_rawgeti(L, index, static_cast<lua_Integer>(i));
                items.push_back(Convert(L, -1, depth + 1, parts));
                lua_pop(L, 1);
            }
            return JsonArray(std::move(items));
        }
        std::vector<std::pair<std::string, JsonValue>> members;
        lua_pushnil(L);
        while (lua_next(L, index) != 0) {
            // A number key is copied before it is read as text: lua_tolstring would turn the
            // key itself into a string and confuse lua_next.
            if (lua_type(L, -2) != LUA_TSTRING && lua_type(L, -2) != LUA_TNUMBER) {
                luaL_error(L, "a table key that is not text has no JSON form");
            }
            lua_pushvalue(L, -2);
            std::size_t size = 0;
            const char* key = lua_tolstring(L, -1, &size);
            std::string name(key, size);
            lua_pop(L, 1);
            members.emplace_back(std::move(name), Convert(L, -1, depth + 1, parts));
            lua_pop(L, 1);
        }
        return JsonObject(std::move(members));
    }
    default:
        break;
    }
    luaL_error(L, "a %s has no JSON form", luaL_typename(L, index));
    return JsonValue();
}

void Write(const JsonValue& value, std::string& out) {
    switch (value.kind) {
    case JsonValue::Kind::Array:
        out += '[';
        for (std::size_t i = 0; i < value.items.size(); ++i) {
            out += i == 0 ? "" : ",";
            Write(value.items[i], out);
        }
        out += ']';
        break;
    case JsonValue::Kind::Object:
        out += '{';
        for (std::size_t i = 0; i < value.members.size(); ++i) {
            out += i == 0 ? "" : ",";
            Write(JsonString(value.members[i].first), out);
            out += ':';
            Write(value.members[i].second, out);
        }
        out += '}';
        break;
    default: {
        // Scalars as the base writer spells them (escapes, whole numbers without a fraction).
        std::string text = WriteJson(value);
        while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
            text.pop_back();
        }
        out += text;
        break;
    }
    }
}

int Decode(lua_State* L) {
    std::size_t size = 0;
    const char* text = luaL_checklstring(L, 1, &size);
    const std::optional<JsonValue> value = ParseJson(std::string_view(text, size));
    if (!value) {
        return Refuse(L, "the text is not JSON");
    }
    PushJson(L, *value);
    return 1;
}

int Encode(lua_State* L) {
    luaL_checkany(L, 1);
    const std::string text = CompactJson(ToJson(L, 1));
    lua_pushlstring(L, text.data(), text.size());
    return 1;
}

} // namespace

JsonValue ToJson(lua_State* L, int index) {
    int parts = 0;
    return Convert(L, index, 0, parts);
}

void PushJsonNull(lua_State* L) {
    lua_pushlightuserdata(L, const_cast<char*>(&NULL_MARK));
}

void PushJson(lua_State* L, const JsonValue& value) {
    luaL_checkstack(L, 4, "the value is nested too deeply");
    switch (value.kind) {
    case JsonValue::Kind::Null:
        PushJsonNull(L);
        break;
    case JsonValue::Kind::Bool:
        lua_pushboolean(L, value.boolean ? 1 : 0);
        break;
    case JsonValue::Kind::Number:
        if (value.number == std::floor(value.number) && std::fabs(value.number) < 9.0e15) {
            lua_pushinteger(L, static_cast<lua_Integer>(value.number));
        } else {
            lua_pushnumber(L, value.number);
        }
        break;
    case JsonValue::Kind::String:
        lua_pushlstring(L, value.string.data(), value.string.size());
        break;
    case JsonValue::Kind::Array:
        lua_createtable(L, static_cast<int>(value.items.size()), 0);
        for (std::size_t i = 0; i < value.items.size(); ++i) {
            PushJson(L, value.items[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        break;
    case JsonValue::Kind::Object:
        lua_createtable(L, 0, static_cast<int>(value.members.size()));
        for (const auto& [key, member] : value.members) {
            lua_pushlstring(L, key.data(), key.size());
            PushJson(L, member);
            lua_rawset(L, -3);
        }
        break;
    }
}

std::string CompactJson(const JsonValue& value) {
    std::string out;
    Write(value, out);
    return out;
}

/// Adds den.json to the table on top of the stack.
void RegisterJson(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, &Decode);
    lua_setfield(L, -2, "decode");
    lua_pushcfunction(L, &Encode);
    lua_setfield(L, -2, "encode");
    PushJsonNull(L);
    lua_setfield(L, -2, "null");
    lua_setfield(L, -2, "json");
}

} // namespace den::plugins::feature
