// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>

#include "den/app/games.h"
#include "den/app/player.h"
#include "den/app/feature_api.h"
#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/platform/device.h"
#include "den/plugins/feature_host.h"
#include "den/system/device.h"
#include "den/system/settings.h"
#include "den/app/sync_glue.h"

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
    // "Legion Go S" rather than its DMI product name: Continue and conflicts show it.
    if (const system::DeviceProfile& device = system::Device(); device.known && !device.name.empty()) {
        return device.name;
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

// After a game ended its saves, states and the picture of the moment it was left go up at
// once (docs/sync.md, section 3.4): a note says so while they do, outside the game.
void ShowSaving(const std::string& ended) {
    static std::string game;    // the game whose files the note is about
    static std::int64_t since = 0;
    if (!ended.empty()) {
        game = ended;
    }
    sync::Engine& engine = *EngineSlot();
    ui::Stage& stage = *Svc().stage;
    const ui::Page* top = stage.Top();
    const bool in_game = top != nullptr && top->Route() == "player";
    if (engine.GameUploadsPending() > 0 && !in_game) {
        const sync::GameTransfer transfer = engine.GameProgress(false);
        std::string text = "Saving to the cloud\xE2\x80\xA6";
        float fraction = -1;
        if (!transfer.game_id.empty() && game.empty()) {
            game = transfer.game_id;
        }
        if (transfer.total > 0) {
            const float part = transfer.fraction >= 0 ? transfer.fraction : 0.0f;
            fraction = (static_cast<float>(transfer.done) + part) / static_cast<float>(transfer.total);
            text += std::format(" {} of {}", std::min(transfer.done + 1, transfer.total), transfer.total);
        }
        if (since == 0) {
            since = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
        }
        stage.SetNote(text, fraction);
    } else if (since != 0) {
        stage.SetNote({});
        const bool saved = !game.empty() && engine.GameState(game).synced >= since;
        stage.Toast(saved ? "Saved to the cloud" : "Not saved to the cloud yet: it goes up with the next sync",
                    saved ? 2.0f : 4.0f);
        game.clear();
        since = 0;
    }
}

// Closing while a game is open leaves it as "Exit to home" does, so the moment it was left
// goes up; then closing waits for the game's files (QUIT_HOLD_SECONDS at most).
bool HoldQuit() {
    if (!EngineSlot() || !EngineSlot()->SyncsSaves()) {
        return false;
    }
    if (auto* player = dynamic_cast<GamePlayer*>(Svc().stage->Top()); player != nullptr && !RunningGame().empty()) {
        player->Exit();
        return true;
    }
    return EngineSlot()->GameUploadsPending() > 0;
}

void EveryFrame() {
    if (!EngineSlot()) {
        Create();
    }
    const std::string game = RunningGame();
    static std::string previous;
    EngineSlot()->Pump(game);
    ShowSaving(!previous.empty() && game.empty() ? previous : std::string());
    previous = game;
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
DEN_REGISTER_QUIT_HOLD(HoldQuit);

} // namespace den::app
