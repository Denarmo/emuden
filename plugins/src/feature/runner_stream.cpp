// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Asks the screen answers many times (the in-app browser hands over every file the user
// downloads), and coroutines that wait for something other than one answer.

#include <algorithm>
#include <atomic>

#include "feature/runner.h"

namespace den::plugins::feature {

// Shared with the asks of runner.cpp, so an id names one ask of one runner.
std::uint64_t NextAskId();

void Runner::PostStream(Ask ask, StreamFn on_answer) {
    ask.id = NextAskId();
    streams_[ask.id] = std::move(on_answer);
    std::lock_guard lock(mutex_);
    stream_asks_.push_back(ask.id);
    asks_.push_back(std::move(ask));
    ++revision_;
}

bool Runner::IsStream(std::uint64_t ask_id, bool last) {
    const auto found = std::find(stream_asks_.begin(), stream_asks_.end(), ask_id);
    if (found == stream_asks_.end()) {
        return false;
    }
    if (last) {
        stream_asks_.erase(found);
    }
    return true;
}

void Runner::AnswerStream(std::uint64_t ask_id, bool accepted, const std::string& value) {
    Post([this, ask_id, accepted, value] {
        const auto found = streams_.find(ask_id);
        if (found == streams_.end()) {
            return;
        }
        const StreamFn answer = found->second;
        if (!accepted) {
            streams_.erase(found);
        }
        answer(accepted, value);
    });
}

bool Runner::UserTriggered(lua_State* L) {
    const Coroutine* co = Current(L);
    return co != nullptr && co->from_user;
}

std::uint64_t Runner::CoroutineOf(lua_State* L) {
    const Coroutine* co = Current(L);
    return co != nullptr ? co->id : 0;
}

int Runner::Park(lua_State* L) {
    Coroutine* co = Current(L);
    if (co == nullptr) {
        return luaL_error(L, "a den call that waits must be made from the plugin's own function, "
                             "not from a coroutine of the script");
    }
    co->waits_for_ask = true;
    return lua_yield(L, 0);
}

void Runner::Wake(std::uint64_t coroutine, const Outcome& outcome) {
    const auto found = coroutines_.find(coroutine);
    if (found == coroutines_.end()) {
        return;
    }
    found->second.waits_for_ask = false;
    ResumeWith(coroutine, outcome);
}

} // namespace den::plugins::feature
