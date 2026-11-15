// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.device.info, den.game (a game's own core options) and den.steam (launch options and
// Proton version of Steam games). Changes are made only after the user pressed something on
// the plugin's page and accepted the app's own question, which lists every change.

#include <algorithm>

#include "den/library/steam.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

constexpr const char* NO_TUNE = "this app cannot do that";

std::shared_ptr<TuneServices> Tune(Runner& runner) {
    return runner.Config().tune;
}

int DeviceInfo(lua_State* L) {
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_DEVICE_INFO)) {
        return Refuse(L, "the plugin has no permission to see the device");
    }
    const auto tune = Tune(runner);
    if (!tune) {
        return Refuse(L, NO_TUNE);
    }
    return runner.Suspend(L, [tune]() -> Outcome {
        JsonValue facts = tune->DeviceFacts();
        return [facts = std::move(facts)](lua_State* T) {
            PushJson(T, facts);
            return 1;
        };
    });
}

void PushGame(lua_State* L, const TuneGame& game) {
    JsonValue options = JsonArray();
    for (const TuneOption& option : game.options) {
        JsonValue values = JsonArray();
        for (const std::string& value : option.values) {
            values.items.push_back(JsonString(value));
        }
        options.items.push_back(JsonObject({{"key", JsonString(option.key)},
                                            {"label", JsonString(option.label)},
                                            {"value", JsonString(option.value)},
                                            {"values", std::move(values)},
                                            {"own", JsonBool(option.own)}}));
    }
    PushJson(L, JsonObject({{"id", JsonString(game.id)},
                            {"name", JsonString(game.name)},
                            {"system", JsonString(game.system)},
                            {"core", JsonString(game.core)},
                            {"core_name", JsonString(game.core_name)},
                            {"known", JsonBool(game.known)},
                            {"options", std::move(options)}}));
}

// The game, when the plugin may change it.
Result<TuneGame> SeenGame(Runner& runner, TuneServices& tune, const std::string& id) {
    std::optional<TuneGame> game = tune.Game(id);
    if (!game) {
        return Status::Fail("there is no such game");
    }
    if (!runner.Config().manifest.access.SeesSystem(game->system)) {
        return Status::Fail("the plugin did not declare the console " + game->system);
    }
    return std::move(*game);
}

int GameSettings(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_GAME_SETTINGS)) {
        return Refuse(L, "the plugin has no permission to see game settings");
    }
    const auto tune = Tune(runner);
    if (!tune) {
        return Refuse(L, NO_TUNE);
    }
    return runner.Suspend(L, [tune, id, &runner]() -> Outcome {
        Result<TuneGame> game = SeenGame(runner, *tune, id);
        return [game = std::move(game)](lua_State* T) {
            if (!game) {
                return Refuse(T, game.status().error);
            }
            PushGame(T, game.value());
            return 1;
        };
    });
}

std::string Shown(const std::string& value) {
    return value.empty() ? "(none)" : value;
}

// den.game.set(id, {key = value or den.json.null, ...}): asks, then sets.
int GameSet(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    JsonValue values = ToJson(L, 2);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_GAME_SETTINGS)) {
        return Refuse(L, "the plugin has no permission to change game settings");
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, "settings are changed only when the user asks for it");
    }
    if (values.kind == JsonValue::Kind::Array && values.items.empty()) {
        values = JsonObject();
    }
    if (values.kind != JsonValue::Kind::Object || values.members.empty() || values.members.size() > 64) {
        return Refuse(L, "set needs the options to change, by key");
    }
    for (const auto& [key, value] : values.members) {
        if (key.size() > 128 || (value.kind != JsonValue::Kind::String && value.kind != JsonValue::Kind::Null) ||
            value.string.size() > 200) {
            return Refuse(L, "an option's value is text, or den.json.null to remove it");
        }
    }
    const auto tune = Tune(runner);
    if (!tune) {
        return Refuse(L, NO_TUNE);
    }
    // The game is looked up on this thread: the question needs its name and options.
    Result<TuneGame> game = SeenGame(runner, *tune, id);
    if (!game) {
        return Refuse(L, game.status().error);
    }
    std::string lines;
    for (const auto& [key, value] : values.members) {
        const auto option = std::find_if(game.value().options.begin(), game.value().options.end(),
                                         [&](const TuneOption& o) { return o.key == key; });
        const std::string label = option != game.value().options.end() && !option->label.empty() ? option->label : key;
        const std::string before = option != game.value().options.end() ? option->value : std::string();
        lines += label + ": " + Shown(before) + " -> " +
                 (value.kind == JsonValue::Kind::Null ? std::string("console setting") : Shown(value.string)) + "\n";
    }
    Ask ask;
    ask.kind = AskKind::Confirm;
    ask.title = CutText("Change the settings of " + game.value().name + "?");
    ask.message = CutText(lines + "Asked by " + runner.Config().manifest.name + ". Game settings can reset this.");
    ask.yes = "Change";
    return runner.SuspendForAsk(L, std::move(ask),
                                [tune, id, values](lua_State* T, bool accepted, const std::string&) {
                                    if (!accepted) {
                                        return Refuse(T, "cancelled");
                                    }
                                    const Result<JsonValue> before = tune->SetGameOptions(id, values);
                                    if (!before) {
                                        return Refuse(T, before.status().error);
                                    }
                                    PushJson(T, before.value());
                                    return 1;
                                });
}

int SteamStatus(lua_State* L) {
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_STEAM)) {
        return Refuse(L, "the plugin has no permission to see Steam");
    }
    const auto tune = Tune(runner);
    if (!tune) {
        return Refuse(L, NO_TUNE);
    }
    return runner.Suspend(L, [tune]() -> Outcome {
        const SteamState state = tune->Steam();
        JsonValue apps = JsonArray();
        JsonValue tools = JsonArray();
        if (state.installed) {
            for (const SteamApp& app : tune->SteamApps()) {
                apps.items.push_back(JsonObject({{"appid", JsonString(app.appid)},
                                                 {"name", JsonString(app.name)},
                                                 {"shortcut", JsonBool(app.shortcut)}}));
            }
            for (const SteamTool& tool : tune->SteamTools()) {
                tools.items.push_back(
                    JsonObject({{"name", JsonString(tool.name)}, {"display", JsonString(tool.display)}}));
            }
        }
        JsonValue out = JsonObject({{"installed", JsonBool(state.installed)},
                                    {"can_change", JsonBool(state.can_change)},
                                    {"note", JsonString(state.note)},
                                    {"apps", std::move(apps)},
                                    {"tools", std::move(tools)}});
        return [out = std::move(out)](lua_State* T) {
            PushJson(T, out);
            return 1;
        };
    });
}

int SteamAppConfig(lua_State* L) {
    const std::string appid = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_STEAM)) {
        return Refuse(L, "the plugin has no permission to see Steam");
    }
    const auto tune = Tune(runner);
    if (!tune) {
        return Refuse(L, NO_TUNE);
    }
    return runner.Suspend(L, [tune, appid]() -> Outcome {
        Result<SteamConfig> config = tune->SteamAppConfig(appid);
        return [config = std::move(config)](lua_State* T) {
            if (!config) {
                return Refuse(T, config.status().error);
            }
            PushJson(T, JsonObject({{"launch_options", JsonString(config.value().launch_options)},
                                    {"compat_tool", JsonString(config.value().compat_tool)},
                                    {"compat_tool_display", JsonString(config.value().compat_tool_display)}}));
            return 1;
        };
    });
}

// den.steam.set{appid, name?, launch_options?, compat_tool?}: asks, then sets.
int SteamSet(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const std::string appid = FieldText(L, 1, "appid");
    const std::string name = FieldText(L, 1, "name");
    std::optional<std::string> launch, tool;
    lua_getfield(L, 1, "launch_options");
    if (lua_isstring(L, -1)) {
        launch = lua_tostring(L, -1);
    }
    lua_getfield(L, 1, "compat_tool");
    if (lua_isstring(L, -1)) {
        tool = lua_tostring(L, -1);
    }
    lua_pop(L, 2);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_STEAM)) {
        return Refuse(L, "the plugin has no permission to change Steam games");
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, "Steam games are changed only when the user asks for it");
    }
    if (appid.empty() || (!launch && !tool)) {
        return Refuse(L, "set needs an appid and launch_options or compat_tool");
    }
    if (launch) {
        if (const Status safe = library::steam::CheckLaunchOptions(*launch); !safe.ok) {
            return Refuse(L, safe.error);
        }
    }
    const auto tune = Tune(runner);
    if (!tune) {
        return Refuse(L, NO_TUNE);
    }
    std::string lines;
    if (launch) {
        lines += "Launch options: " + Shown(*launch) + "\n";
    }
    if (tool) {
        lines += "Compatibility tool: " + (tool->empty() ? std::string("Steam's default") : *tool) + "\n";
    }
    Ask ask;
    ask.kind = AskKind::Confirm;
    ask.title = CutText("Change " + (name.empty() ? "app " + appid : name) + " in Steam?");
    ask.message = CutText(lines + "Asked by " + runner.Config().manifest.name + ". Steam's Properties can change it back.");
    ask.yes = "Change";
    return runner.SuspendForAsk(L, std::move(ask),
                                [tune, appid, launch, tool](lua_State* T, bool accepted, const std::string&) {
                                    if (!accepted) {
                                        return Refuse(T, "cancelled");
                                    }
                                    const Status done = tune->SetSteamApp(appid, launch, tool);
                                    if (!done.ok) {
                                        return Refuse(T, done.error);
                                    }
                                    lua_pushboolean(T, 1);
                                    return 1;
                                });
}

} // namespace

void RegisterTune(lua_State* L, Runner& runner) {
    lua_getfield(L, -1, "device");
    SetFunction(L, runner, "info", &DeviceInfo);
    lua_pop(L, 1);
    lua_newtable(L);
    SetFunction(L, runner, "settings", &GameSettings);
    SetFunction(L, runner, "set", &GameSet);
    lua_setfield(L, -2, "game");
    lua_newtable(L);
    SetFunction(L, runner, "status", &SteamStatus);
    SetFunction(L, runner, "config", &SteamAppConfig);
    SetFunction(L, runner, "set", &SteamSet);
    lua_setfield(L, -2, "steam");
}

} // namespace den::plugins::feature
