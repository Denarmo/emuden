// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../home/games.h"
#include "../home/strings.h"
#include "den/app/app.h"
#include "den/base/files.h"
#include "den/base/log.h"
#include "launch.h"

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
    request.fullscreen = svc.window->Fullscreen();
    log::Info("launch", "engine: {} ({})", game.name, game.system);
    if (const Status started = svc.engine->Start(request); !started) {
        svc.stage->Toast(started.error.empty() ? Tr("launch.failed") : started.error, 6.0f);
    }
}

// ROMs: the player screen opens the session for the entry.
void StartInPlayer(const library::Entry& game, bool restart) {
    Services& svc = Svc();
    if (!svc.stage->HasRoute("player")) {
        svc.stage->Toast(Tr("launch.no_player"));
        return;
    }
    log::Info("launch", "player: {} ({})", game.name, game.system);
    ui::Params params{{"id", game.id}};
    if (restart) {
        params["resume"] = "0";
    }
    svc.stage->Push("player", std::move(params));
}

void StartRom(const library::Entry& game, bool restart) {
    Services& svc = Svc();
    const auto core = svc.plugins->CoreFor(game.system);
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
                           });
        return;
    }
    if (!core->missing_bios.empty()) {
        std::string files;
        for (const std::string& file : core->missing_bios) {
            files += (files.empty() ? "" : ", ") + file;
        }
        svc.stage->Confirm(Tr("bios.missing_title"),
                           TrF("bios.missing_message", console, files, PathToUtf8(core->system_dir)),
                           Tr("bios.start_anyway"), [game, restart] { StartInPlayer(game, restart); });
        return;
    }
    StartInPlayer(game, restart);
}

} // namespace

void StartGame(const std::string& game_id, bool restart) {
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
        StartInEngine(*game);
    } else {
        StartRom(*game, restart);
    }
}

} // namespace den::app
