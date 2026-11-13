// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "feature/sandbox.h"
#include "lualib.h"

namespace den::plugins::feature {

namespace {

constexpr int HOOK_STEP = 1000;

// An error outside any protected call (the host's own setup): out as a C++ exception.
int Panic(lua_State* L) {
    const char* message = lua_tostring(L, -1);
    throw std::runtime_error(message != nullptr ? message : "the script failed");
}

// load(text [, name [, mode [, env]]]): text chunks only, whatever mode is asked for.
int SafeLoad(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        return Refuse(L, "load takes the script as text");
    }
    std::size_t size = 0;
    const char* text = lua_tolstring(L, 1, &size);
    const char* name = luaL_optstring(L, 2, "=(load)");
    if (luaL_loadbufferx(L, text, size, name, "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (!lua_isnone(L, 4)) {
        lua_pushvalue(L, 4);
        if (lua_setupvalue(L, -2, 1) == nullptr) {
            lua_pop(L, 1);
        }
    }
    return 1;
}

// collectgarbage(): a full collection, nothing else.
int SafeCollect(lua_State* L) {
    if (lua_gettop(L) > 0 && !(lua_type(L, 1) == LUA_TSTRING &&
                               std::strcmp(lua_tostring(L, 1), "count") == 0)) {
        return luaL_error(L, "collectgarbage takes no arguments here");
    }
    if (lua_gettop(L) > 0) {
        lua_pushnumber(L, lua_gc(L, LUA_GCCOUNT) + lua_gc(L, LUA_GCCOUNTB) / 1024.0);
        return 1;
    }
    lua_gc(L, LUA_GCCOLLECT);
    lua_pushinteger(L, 0);
    return 1;
}

void RemoveGlobal(lua_State* L, const char* name) {
    lua_pushnil(L);
    lua_setglobal(L, name);
}

} // namespace

void* Sandbox::Allocate(void* self, void* block, std::size_t old_size, std::size_t new_size) {
    auto& sandbox = *static_cast<Sandbox*>(self);
    if (block == nullptr) {
        old_size = 0; // it holds the type of the new object
    }
    if (new_size == 0) {
        std::free(block);
        sandbox.used_ -= old_size;
        return nullptr;
    }
    if (new_size > old_size && sandbox.used_ + (new_size - old_size) > sandbox.limit_) {
        return nullptr; // Lua collects and tries once more, then raises "not enough memory"
    }
    void* moved = std::realloc(block, new_size);
    if (moved != nullptr) {
        sandbox.used_ = sandbox.used_ - old_size + new_size;
    }
    return moved;
}

// Runs every HOOK_STEP instructions. Once the budget is spent it runs on every instruction,
// so the error is raised again after each pcall that swallowed it.
void Sandbox::CountHook(lua_State* L, lua_Debug*) {
    Sandbox& sandbox = Of(L);
    if (sandbox.budget_ == nullptr) {
        return;
    }
    *sandbox.budget_ -= HOOK_STEP;
    if (*sandbox.budget_ <= 0) {
        *sandbox.budget_ = 0;
        lua_sethook(L, &Sandbox::CountHook, LUA_MASKCOUNT, 1);
        luaL_error(L, "the plugin ran too long without a pause and was stopped");
    }
}

Sandbox& Sandbox::Of(lua_State* L) {
    return **static_cast<Sandbox**>(lua_getextraspace(L));
}

Sandbox::Sandbox(std::size_t memory_limit, long long instruction_limit)
    : limit_(memory_limit), instruction_limit_(instruction_limit) {
    state_ = lua_newstate(&Sandbox::Allocate, this);
    if (state_ == nullptr) {
        return;
    }
    lua_State* L = state_;
    *static_cast<Sandbox**>(lua_getextraspace(L)) = this;
    lua_atpanic(L, &Panic);
    try {
        static const luaL_Reg LIBRARIES[] = {
            {LUA_GNAME, luaopen_base},          {LUA_STRLIBNAME, luaopen_string},
            {LUA_TABLIBNAME, luaopen_table},    {LUA_MATHLIBNAME, luaopen_math},
            {LUA_UTF8LIBNAME, luaopen_utf8},    {LUA_COLIBNAME, luaopen_coroutine},
        };
        for (const luaL_Reg& library : LIBRARIES) {
            luaL_requiref(L, library.name, library.func, 1);
            lua_pop(L, 1);
        }
        for (const char* name : {"dofile", "loadfile", "require", "print"}) {
            RemoveGlobal(L, name);
        }
        lua_pushcfunction(L, &SafeLoad);
        lua_setglobal(L, "load");
        lua_pushcfunction(L, &SafeCollect);
        lua_setglobal(L, "collectgarbage");
        // string.dump would hand out bytecode; nothing here loads it, so it is not offered.
        lua_getglobal(L, LUA_STRLIBNAME);
        lua_pushnil(L);
        lua_setfield(L, -2, "dump");
        lua_pop(L, 1);
        lua_sethook(L, &Sandbox::CountHook, LUA_MASKCOUNT, HOOK_STEP);
    } catch (const std::exception&) {
        lua_close(state_);
        state_ = nullptr;
    }
}

Sandbox::~Sandbox() {
    if (state_ != nullptr) {
        lua_close(state_);
    }
}

Status Sandbox::LoadText(lua_State* L, std::string_view text, const std::string& chunk_name) {
    if (luaL_loadbufferx(L, text.data(), text.size(), chunk_name.c_str(), "t") != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        Status status = Status::Fail(message != nullptr ? message : "the script does not load");
        lua_pop(L, 1);
        return status;
    }
    return {};
}

int Refuse(lua_State* L, std::string_view message) {
    lua_pushnil(L);
    lua_pushlstring(L, message.data(), message.size());
    return 2;
}

} // namespace den::plugins::feature
