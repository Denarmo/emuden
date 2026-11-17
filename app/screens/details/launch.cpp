// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "den/app/boot.h"
#include "den/app/games.h"
#include "den/app/strings.h"
#include "den/app/app.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "den/app/engine_setup.h"
#include "den/app/game_prefs.h"
#include "den/app/launch.h"
#include "den/app/player_hooks.h"

namespace den::app {

namespace {

// Games that run outside the app (Switch 2).
void StartInEngine(const library::Entry& game) {
    Services& svc = Svc();
    if (!svc.engine->Available(game.system)) {
        const std::string reason = svc.engine->UnavailableReason(game.system);
        svc.stage->Confirm(TrF("launch.engine_missing_title", SystemName(game.system)),
                           reason.empty() ? Tr("launch.engine_missing") : reason, Tr("common.ok"), [] {},
                           Tr("common.close"));
        return;
    }
    if (svc.engine->Running()) {
        svc.stage->Toast(Tr("launch.engine_running"));
        return;
    }
    engine::LaunchRequest request;
    request.system = game.system;
    request.game_id = game.id;
    request.title = game.name;
    request.content = PathFromUtf8(game.path);
    request.fullscreen = EngineFullscreenFor(game, svc.window->Fullscreen());
    BootGame(game, BootRunner::Engine, [request, name = game.name, system = game.system] {
        log::Info("launch", "engine: {} ({})", name, system);
        if (const Status started = Svc().engine->Start(request); !started) {
            Svc().stage->Toast(started.error.empty() ? Tr("launch.failed") : started.error, 6.0f);
        }
    });
}

// ROMs: the player screen opens the session for the entry.
void StartInPlayer(const library::Entry& game, const plugins::CoreInfo& core, bool restart, const ui::Params& extra) {
    Services& svc = Svc();
    if (!svc.stage->HasRoute("player")) {
        svc.stage->Toast(Tr("launch.no_player"));
        return;
    }
    log::Info("launch", "player: {} ({})", game.name, game.system);
    ui::Params params = extra;
    params["id"] = game.id;
    if (restart) {
        params["resume"] = "0";
    }
    // An engine app that takes the display is handed the boot screen's picture.
    const bool app = core.runner == plugins::Runner::Engine && !EngineInstallFor(core).package.empty();
    if (BootGame(game, app ? BootRunner::PlayerApp : BootRunner::Player,
                 [params] { Svc().stage->Push("player", params); })) {
        for (const PlayerHooks& hooks : AllPlayerHooks()) {
            if (hooks.asked) {
                hooks.asked(game, core, params);
            }
        }
    }
}

void StartRom(const library::Entry& game, bool restart, const ui::Params& extra) {
    Services& svc = Svc();
    const auto core = CoreForGame(game);
    const std::string console = SystemName(game.system);
    if (!core) {
        svc.stage->Toast(TrF("launch.no_core", console), 5.0f);
        return;
    }
    if (!core->installed || !svc.plugins->Installed(game.system)) {
        const std::string name = core->name.empty() ? core->id : core->name;
        svc.stage->Confirm(Tr("core.needed_title"), TrF("core.needed_message", console, name),
                           Tr("core.download"), [system = game.system, id = game.id, restart] {
                               Svc().stage->Push("install", {{"system", system},
                                                             {"id", id},
                                                             {"restart", restart ? "1" : "0"}});
                           },
                           Tr("common.cancel"), true);
        return;
    }
    if (!core->missing_bios.empty()) {
        std::string files;
        for (const std::string& file : core->missing_bios) {
            files += (files.empty() ? "" : ", ") + file;
        }
        svc.stage->Confirm(Tr("bios.missing_title"),
                           TrF("bios.missing_message", console, files, PathToUtf8(core->system_dir)),
                           Tr("bios.start_anyway"),
                           [game, core = *core, restart, extra] { StartInPlayer(game, core, restart, extra); });
        return;
    }
    StartInPlayer(game, *core, restart, extra);
}

} // namespace

void StartGame(const std::string& game_id, bool restart, const ui::Params& extra) {
    Services& svc = Svc();
    const auto game = svc.library->Find(game_id);
    if (!game) {
        svc.stage->Toast(Tr("launch.gone"));
        return;
    }
    const auto console = svc.plugins->FindConsole(game->system);
    if (!console) {
        svc.stage->Toast(TrF("launch.no_console", SystemName(game->system)), 5.0f);
        return;
    }
    if (console->runner == plugins::Runner::Engine) {
        // One that speaks the protocol is played from the player screen, with its menu.
        if (const auto found = FoundEngineCore(game->system); found && svc.stage->HasRoute("player")) {
            StartInPlayer(*game, *found, restart, extra);
            return;
        }
        StartInEngine(*game);
    } else {
        StartRom(*game, restart, extra);
    }
}

} // namespace den::app
