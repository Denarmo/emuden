// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Functions of the script the host calls for their results (the sync engine calling
// plugin.provider.*), and the progress functions it hands them.

#include "feature/runner.h"

namespace den::plugins::feature {

namespace {

// In a protected call: the values on the stack as JSON, into the vector of the upvalue.
int ConvertResults(lua_State* L) {
    auto* out = static_cast<std::vector<JsonValue>*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i) {
        out->push_back(lua_isnil(L, i) ? JsonValue() : ToJson(L, i));
    }
    return 0;
}

} // namespace

void Runner::Answer(Coroutine& co, int results, const std::string& failure) {
    if (co.progress_key != 0) {
        progress_fns_.erase(co.progress_key);
        co.progress_key = 0;
    }
    if (!co.done) {
        return;
    }
    const InvokeDone done = std::move(co.done);
    co.done = nullptr;
    --invoking_;
    if (!failure.empty()) {
        done(Status::Fail(failure));
        return;
    }
    lua_State* L = sandbox_->L();
    const int base = lua_gettop(L);
    std::vector<JsonValue> values;
    std::string error;
    if (results > 0 && lua_checkstack(L, results + 2)) {
        lua_pushlightuserdata(L, &values);
        lua_pushcclosure(L, &ConvertResults, 1);
        lua_xmove(co.thread, L, results);
        if (lua_pcall(L, results, 0, 0) != LUA_OK) {
            error = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "the results are not data";
        }
    }
    lua_settop(L, base);
    if (!error.empty()) {
        done(Status::Fail(error));
    } else if (!values.empty() && values[0].kind == JsonValue::Kind::Null) {
        const bool said = values.size() > 1 && values[1].kind == JsonValue::Kind::String;
        done(Status::Fail(said ? values[1].string : "the plugin returned nothing"));
    } else {
        done(std::move(values));
    }
}

int Runner::ProgressThunk(lua_State* L) {
    Runner& runner = Runner::Of(L);
    const auto found = runner.progress_fns_.find(static_cast<int>(lua_tointeger(L, lua_upvalueindex(1))));
    if (found != runner.progress_fns_.end()) {
        found->second(luaL_optnumber(L, 1, 0), luaL_optnumber(L, 2, 0));
    }
    return 0;
}

} // namespace den::plugins::feature
