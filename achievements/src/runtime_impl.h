// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The state behind achievements::Runtime, shared by its files.

#pragma once

#include <deque>
#include <mutex>

#include "den/achievements/runtime.h"
#include "rc_client.h"
#include "rc_libretro.h"

namespace den::achievements {

/// Server answers waiting for the main thread. Shared with the helper threads, so an answer
/// that comes after the runtime went has somewhere to go.
struct Inbox {
    struct Answer {
        rc_client_server_callback_t callback = nullptr;
        void* data = nullptr;
        HttpResponse response;
    };
    std::mutex mutex;
    std::deque<Answer> answers;
    bool closed = false;
};

struct Runtime::Impl {
    Config config;
    rc_client_t* client = nullptr;
    std::shared_ptr<Inbox> inbox = std::make_shared<Inbox>();
    std::vector<Event> events;

    GameMemory memory;
    rc_libretro_memory_regions_t regions{};
    bool regions_ready = false;
    std::uint32_t console = 0;
    LoadState load = LoadState::None;

    std::function<void(int, const char*)> login_done;
    std::function<void(Status)> load_done;

    void MapMemory();
    void FreeMemory();
};

/// runtime_events.cpp
void RC_CCONV HandleEvent(const rc_client_event_t* event, rc_client_t* client);
uint32_t RC_CCONV ReadMemory(uint32_t address, uint8_t* buffer, uint32_t bytes, rc_client_t* client);
Achievement ToAchievement(const rc_client_achievement_t& from);

} // namespace den::achievements
