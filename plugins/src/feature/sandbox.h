// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A Lua 5.4 state a plugin's script runs in: text chunks only, no os, io, package, debug,
// require, dofile or loadfile, an allocator that stops at the plugin's memory limit and a
// count hook that stops a callback after its instruction budget.
//
// Lua is built as C++ here (cmake/deps.cmake), so a Lua error is a C++ exception while it
// travels: host functions may hold C++ objects when they raise one.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "lua.h"

#include "lauxlib.h"

#include "den/base/json.h"
#include "den/base/result.h"

namespace den::plugins::feature {

class Sandbox {
public:
    Sandbox(std::size_t memory_limit, long long instruction_limit);
    ~Sandbox();
    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    /// Null when the state could not be created within the memory limit.
    lua_State* L() const {
        return state_;
    }
    std::size_t MemoryUsed() const {
        return used_;
    }
    long long InstructionLimit() const {
        return instruction_limit_;
    }

    /// Compiles `text` (never a binary chunk) and leaves the function on `L`'s stack. On
    /// failure nothing is left and the status has the message.
    Status LoadText(lua_State* L, std::string_view text, const std::string& chunk_name);

    /// The instructions the code about to run may still spend; null = uncounted (host setup).
    void SetBudget(long long* remaining) {
        budget_ = remaining;
    }

    static Sandbox& Of(lua_State* L);

private:
    static void* Allocate(void* self, void* block, std::size_t old_size, std::size_t new_size);
    static void CountHook(lua_State* L, lua_Debug* debug);

    lua_State* state_ = nullptr;
    std::size_t limit_;
    std::size_t used_ = 0;
    long long instruction_limit_;
    long long* budget_ = nullptr;
};

/// A Lua value as JSON. Tables with a first element (or none at all) are arrays, others
/// objects with string keys; den.json.null is null. Raises a Lua error for a function, a
/// userdata, a table nested deeper than 32 levels or a value of more than 200000 parts.
JsonValue ToJson(lua_State* L, int index);
void PushJson(lua_State* L, const JsonValue& value);
/// den.json.null: the value JSON null decodes to.
void PushJsonNull(lua_State* L);

/// Pushes nil and the message: what a failing den call returns.
int Refuse(lua_State* L, std::string_view message);

} // namespace den::plugins::feature
