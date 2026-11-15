// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.achievements: the RetroAchievements plugin's sign-in, settings and pages over the
// app's runtime. The password goes from the script to the runtime once; only the token that
// comes back is kept, in the plugin's den.secrets. Sign-in, sign-out and settings work only
// after the user pressed something on the plugin's page.

#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

constexpr const char* NO_RUNTIME = "this app has no achievements runtime";

std::shared_ptr<AchievementServices> Services(lua_State* L, Runner& runner, int* refused) {
    *refused = 0;
    if (!runner.Config().manifest.access.Has(PERMISSION_ACHIEVEMENTS)) {
        *refused = Refuse(L, "the plugin has no permission for achievements");
        return nullptr;
    }
    if (!runner.Config().achievements) {
        *refused = Refuse(L, NO_RUNTIME);
        return nullptr;
    }
    return runner.Config().achievements;
}

JsonValue UserJson(const AchievementUser& user) {
    return JsonObject({{"name", JsonString(user.name)},
                       {"display_name", JsonString(user.display_name)},
                       {"avatar", JsonString(user.avatar_url)},
                       {"score", JsonNumber(user.score)},
                       {"softcore_score", JsonNumber(user.score_softcore)},
                       {"messages", JsonNumber(user.unread_messages)}});
}

JsonValue GameJson(const AchievementGame& game) {
    JsonValue items = JsonArray();
    for (const AchievementItem& a : game.achievements) {
        items.items.push_back(JsonObject({{"id", JsonNumber(a.id)},
                                          {"title", JsonString(a.title)},
                                          {"description", JsonString(a.description)},
                                          {"badge", JsonString(a.badge_url)},
                                          {"points", JsonNumber(a.points)},
                                          {"unlocked", JsonBool(a.unlocked)},
                                          {"progress", JsonString(a.progress)},
                                          {"percent", JsonNumber(a.percent)},
                                          {"rarity", JsonNumber(a.rarity)}}));
    }
    return JsonObject({{"id", JsonString(game.library_id)},
                       {"name", JsonString(game.name)},
                       {"ra_id", JsonNumber(game.id)},
                       {"title", JsonString(game.title)},
                       {"image", JsonString(game.image_url)},
                       {"total", JsonNumber(game.total)},
                       {"unlocked", JsonNumber(game.unlocked)},
                       {"points_total", JsonNumber(game.points_total)},
                       {"points_unlocked", JsonNumber(game.points_unlocked)},
                       {"hardcore", JsonBool(game.hardcore)},
                       {"mastered", JsonBool(game.mastered)},
                       {"playing", JsonBool(game.playing)},
                       {"note", JsonString(game.note)},
                       {"achievements", std::move(items)}});
}

int Status_(lua_State* L) {
    Runner& runner = Runner::Of(L);
    int refused = 0;
    const auto services = Services(L, runner, &refused);
    if (!services) {
        return refused;
    }
    return runner.Suspend(L, [services]() -> Outcome {
        const auto user = services->User();
        const AchievementOptions options = services->Options();
        JsonValue status = JsonObject({{"signed_in", JsonBool(user.has_value())},
                                       {"signing_in", JsonBool(services->SigningIn())},
                                       {"hardcore", JsonBool(options.hardcore)},
                                       {"sound", JsonBool(options.sound)},
                                       {"screenshot", JsonBool(options.screenshot)},
                                       {"playing", JsonString(services->Playing())}});
        if (user) {
            status.members.emplace_back("user", UserJson(*user));
        }
        return [status = std::move(status)](lua_State* T) {
            PushJson(T, status);
            return 1;
        };
    });
}

int SignIn(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const std::string user = FieldText(L, 1, "user");
    const std::string password = FieldText(L, 1, "password");
    Runner& runner = Runner::Of(L);
    int refused = 0;
    const auto services = Services(L, runner, &refused);
    if (!services) {
        return refused;
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, "sign-in starts only when the user asks for it");
    }
    if (user.empty() || password.empty() || user.size() > 64 || password.size() > 256) {
        return Refuse(L, "sign-in needs the user name and the password");
    }
    return runner.Suspend(L, [services, user, password, &runner]() -> Outcome {
        Result<std::string> token = services->SignIn(user, password);
        return [token = std::move(token), user, &runner](lua_State* T) {
            if (!token) {
                return Refuse(T, token.status().error);
            }
            const Status kept = KeepSecret(
                runner, ACHIEVEMENTS_LOGIN_SECRET,
                JsonObject({{"user", JsonString(user)}, {"token", JsonString(token.value())}}));
            if (!kept) {
                return Refuse(T, kept.error);
            }
            lua_pushboolean(T, 1);
            return 1;
        };
    });
}

int SignOut(lua_State* L) {
    Runner& runner = Runner::Of(L);
    int refused = 0;
    const auto services = Services(L, runner, &refused);
    if (!services) {
        return refused;
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, "sign-out happens only when the user asks for it");
    }
    DropSecret(runner, ACHIEVEMENTS_LOGIN_SECRET);
    services->SignOut();
    lua_pushboolean(L, 1);
    return 1;
}

int Set(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    const auto services = Services(L, runner, &refused);
    if (!services) {
        return refused;
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, "settings change only when the user asks for it");
    }
    AchievementOptions options = services->Options();
    const auto take = [&](const char* key, bool& into) {
        lua_getfield(L, 1, key);
        if (lua_isboolean(L, -1)) {
            into = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
    };
    take("hardcore", options.hardcore);
    take("sound", options.sound);
    take("screenshot", options.screenshot);
    services->SetOptions(options);
    lua_pushboolean(L, 1);
    return 1;
}

int Game(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    const auto services = Services(L, runner, &refused);
    if (!services) {
        return refused;
    }
    return runner.Suspend(L, [services, id]() -> Outcome {
        Result<AchievementGame> game = services->Game(id);
        return [game = std::move(game)](lua_State* T) {
            if (!game) {
                return Refuse(T, game.status().error);
            }
            PushJson(T, GameJson(game.value()));
            return 1;
        };
    });
}

} // namespace

void RegisterAchievements(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "status", &Status_);
    SetFunction(L, runner, "sign_in", &SignIn);
    SetFunction(L, runner, "sign_out", &SignOut);
    SetFunction(L, runner, "set", &Set);
    SetFunction(L, runner, "game", &Game);
    lua_setfield(L, -2, "achievements");
}

} // namespace den::plugins::feature
