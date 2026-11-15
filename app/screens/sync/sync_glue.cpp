// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "../home/games.h"
#include "../plugins/feature_api.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/platform/device.h"
#include "den/plugins/feature_host.h"
#include "den/system/settings.h"
#include "sync_glue.h"

namespace den::app {

namespace {

std::shared_ptr<sync::Engine>& EngineSlot() {
    static std::shared_ptr<sync::Engine> engine;
    return engine;
}

std::string DeviceName() {
    const pal::DeviceFacts facts = pal::GetDeviceFacts();
    if (!facts.device_name.empty()) {
        return facts.device_name;
    }
    return !facts.model.empty() ? facts.model : pal::GetCaps().os;
}

void Create() {
    Services& svc = Svc();
    sync::EngineConfig config;
    config.data_dir = svc.data_dir;
    config.jobs = svc.jobs;
    config.settings = svc.settings;
    config.layers = system::HasStore() ? &system::Store() : nullptr;
    config.library = svc.library;
    config.consoles = svc.plugins;
    config.features = FeatureHostSvc();
    config.device = DeviceName();
    EngineSlot() = sync::CreateEngine(std::move(config));
    if (plugins::FeatureHost* host = FeatureHostSvc()) {
        host->SetSyncLink(EngineSlot()->Link());
    }
    EngineSlot()->SyncNow(); // the app started
}

// The library id of the core game that runs now: its saves are never replaced meanwhile.
std::string RunningGame() {
    if (Svc().player == nullptr) {
        return {};
    }
    const player::Snapshot snapshot = Svc().player->GetSnapshot();
    const bool open = snapshot.state == player::State::Running || snapshot.state == player::State::Paused;
    return open && !snapshot.external ? snapshot.game_id : std::string();
}

void EveryFrame() {
    if (!EngineSlot()) {
        Create();
    }
    EngineSlot()->Pump(RunningGame());
    // Settings, Sync shows what a run changed once it is over.
    static bool was_running = false;
    const bool running = EngineSlot()->State().running;
    if (was_running && !running) {
        if (ui::Page* top = Svc().stage->Top(); top && top->Route() == "settings") {
            top->Rebuild();
        }
    }
    was_running = running;
}

} // namespace

sync::Engine* SyncSvc() {
    return EngineSlot().get();
}

std::string SyncTimeText(std::int64_t unix_time) {
    return unix_time <= 0 ? std::string("Never") : LastPlayedText(unix_time);
}

DEN_REGISTER_FRAME(EveryFrame);

} // namespace den::app
