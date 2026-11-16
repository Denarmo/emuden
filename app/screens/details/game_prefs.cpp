// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <fstream>

#include "den/app/app.h"
#include "den/app/registry.h"
#include "den/base/files.h"
#include "core_options.h"
#include "den/base/json.h"
#include "den/app/game_prefs.h"

namespace den::app {

const system::Key<int> SCREEN_SIZE_KEY{
    "player.screen_size", 0, {.label = "Screen size", .section = "Player", .per_console = true, .per_game = true}};
const system::Key<std::string> GAME_CORE_KEY{"player.core", "", {.label = "Core", .section = "Player", .per_game = true}};
const system::Key<std::string> ENGINE_SCREEN_KEY{
    "engine.screen",
    "app",
    {.label = "Screen",
     .section = "Engine",
     .options = {"app", "fullscreen", "window"},
     .per_console = true,
     .per_game = true}};

namespace {

// The game is one a core lists by the code its ROM holds (a GBA game's at 0xAC). Only loose
// files: a zipped ROM keeps the console's core.
bool CoreListsGame(const plugins::GameMatch& games, const library::Entry& game) {
    if (games.Empty() || !game.archive_member.empty()) {
        return false;
    }
    std::ifstream in(PathFromUtf8(game.path), std::ios::binary);
    std::string code(games.length, ' ');
    if (!in.seekg(static_cast<std::streamoff>(games.offset)) ||
        !in.read(code.data(), static_cast<std::streamsize>(code.size()))) {
        return false;
    }
    return std::find(games.codes.begin(), games.codes.end(), code) != games.codes.end();
}

std::filesystem::path OptionsFile(const std::string& core_id) {
    return Svc().data_dir / "cache" / "core_options" / KnownOptionsFileName(core_id);
}

std::string TextOf(const JsonValue& value) {
    return value.kind == JsonValue::Kind::String ? value.string : std::string();
}

void NoteOptions(const std::string& core_id, const std::vector<player::CoreOption>& options) {
    // The value in effect is the global one here: per-game values are applied afterwards.
    std::vector<KnownOption> known;
    for (const player::CoreOption& option : options) {
        known.push_back({option.key, option.label, option.value, option.values});
    }
    const std::string text = WriteKnownOptions(known);
    const std::filesystem::path file = OptionsFile(core_id);
    if (ReadTextFile(file).value_or("") != text) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        WriteFileAtomic(file, text);
    }
}

// Once per session: notes what options the core has, then puts the game's and the console's
// own values over the global ones without storing them as global.
void ApplyGameOptions() {
    static std::string handled;
    Services& svc = Svc();
    if (!svc.player || !system::HasStore()) {
        return;
    }
    const player::Snapshot snapshot = svc.player->GetSnapshot();
    if (snapshot.state != player::State::Running && snapshot.state != player::State::Paused) {
        handled.clear();
        return;
    }
    if (snapshot.game_id == handled) {
        return;
    }
    handled = snapshot.game_id;
    const auto game = svc.library->Find(snapshot.game_id);
    const auto core = game ? CoreForGame(*game) : std::nullopt;
    if (!core) {
        return;
    }
    const std::vector<player::CoreOption> options = svc.player->Options();
    NoteOptions(core->id, options);
    const system::Scope scope = ScopeOf(*game);
    for (const player::CoreOption& option : options) {
        const std::string key = OptionKey(core->id, option.key);
        if (system::Store().Source(key, scope) < system::Layer::Console) {
            continue;
        }
        const std::string value = TextOf(system::Store().Resolve(key, scope));
        if (value.empty() || value == option.value ||
            std::find(option.values.begin(), option.values.end(), value) == option.values.end()) {
            continue;
        }
        const std::string global = GlobalOptionKey(snapshot.core_name, option.key);
        const bool had = svc.settings->Has(global);
        const std::string before = svc.settings->GetString(global);
        svc.player->SetOption(option.key, value);
        if (had) {
            svc.settings->SetString(global, before);
        } else {
            svc.settings->Remove(global);
        }
    }
}

} // namespace

system::Scope ScopeOf(const library::Entry& game) {
    return system::Scope::Game(game.system, game.id);
}

std::optional<plugins::CoreInfo> CoreForGame(const library::Entry& game, const std::string& forced) {
    Services& svc = Svc();
    std::optional<plugins::CoreInfo> core = svc.plugins->CoreFor(game.system);
    const std::string wanted = !forced.empty()       ? forced
                               : system::HasStore() ? system::Store().Get(GAME_CORE_KEY, ScopeOf(game))
                                                    : std::string();
    for (plugins::CoreInfo& other : svc.plugins->CoresFor(game.system)) {
        if (!other.Ready()) {
            continue;
        }
        if (wanted.empty() ? CoreListsGame(other.games, game) : other.id == wanted) {
            core = std::move(other);
            break;
        }
    }
    if (!system::HasStore()) {
        return core;
    }
    const system::Scope scope = ScopeOf(game);
    if (core) {
        // Before the core loads, for options that are read only then.
        for (const KnownOption& option : KnownOptions(core->id)) {
            const std::string key = OptionKey(core->id, option.key);
            if (system::Store().Source(key, scope) >= system::Layer::Console) {
                core->option_hints.emplace_back(option.key, TextOf(system::Store().Resolve(key, scope)));
            }
        }
    }
    return core;
}

bool GameHasOwnCore(const library::Entry& game) {
    if (system::HasStore() && !system::Store().Get(GAME_CORE_KEY, ScopeOf(game)).empty()) {
        return true;
    }
    const std::vector<plugins::CoreInfo> cores = Svc().plugins->CoresFor(game.system);
    return std::any_of(cores.begin(), cores.end(), [&game](const plugins::CoreInfo& core) {
        return core.Ready() && CoreListsGame(core.games, game);
    });
}

int ScreenSizeFor(const library::Entry& game) {
    return system::HasStore() ? system::Store().Get(SCREEN_SIZE_KEY, ScopeOf(game))
                              : static_cast<int>(Svc().settings->GetNumber(SCREEN_SIZE_KEY.name(), 0));
}

bool EngineFullscreenFor(const library::Entry& game, bool app_fullscreen) {
    const std::string screen =
        system::HasStore() ? system::Store().Get(ENGINE_SCREEN_KEY, ScopeOf(game)) : std::string("app");
    return screen == "fullscreen" || (screen != "window" && app_fullscreen);
}

std::vector<KnownOption> KnownOptions(const std::string& core_id) {
    const auto text = ReadTextFile(OptionsFile(core_id));
    return text ? ParseKnownOptions(*text) : std::vector<KnownOption>{};
}

DEN_REGISTER_FRAME(ApplyGameOptions);

} // namespace den::app
