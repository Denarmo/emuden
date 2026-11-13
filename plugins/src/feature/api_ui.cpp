// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.ui and den.app: the script's page stack and what only the screen can do.

#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

int Set(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    PageModel page = PageFromJson(ToJson(L, 1));
    Runner::Of(L).ChangePages([&](std::vector<PageModel>& pages) {
        if (pages.empty()) {
            pages.push_back(std::move(page));
        } else {
            pages.back() = std::move(page);
        }
        return true;
    });
    return 0;
}

int Push(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    PageModel page = PageFromJson(ToJson(L, 1));
    bool pushed = false;
    Runner::Of(L).ChangePages([&](std::vector<PageModel>& pages) {
        if (pages.size() < MAX_PAGES) {
            pages.push_back(std::move(page));
            pushed = true;
        }
        return pushed;
    });
    if (!pushed) {
        return Refuse(L, "the plugin already shows 8 pages on top of each other");
    }
    lua_pushboolean(L, 1);
    return 1;
}

// Back to the page below; from the plugin's first page, out of the plugin.
int Pop(lua_State* L) {
    Runner& runner = Runner::Of(L);
    if (!runner.Back()) {
        Ask ask;
        ask.kind = AskKind::Pop;
        runner.PostAsk(std::move(ask));
    }
    return 0;
}

int Patch(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const JsonValue fields = ToJson(L, 2);
    bool found = false;
    Runner::Of(L).ChangePages([&](std::vector<PageModel>& pages) {
        for (auto page = pages.rbegin(); page != pages.rend() && !found; ++page) {
            found = PatchElement(*page, id, fields);
        }
        return found;
    });
    lua_pushboolean(L, found ? 1 : 0);
    return 1;
}

int Toast(lua_State* L) {
    std::size_t size = 0;
    const char* text = luaL_tolstring(L, 1, &size);
    Ask ask;
    ask.kind = AskKind::Toast;
    ask.message = CutText(std::string(text, size));
    Runner::Of(L).PostAsk(std::move(ask));
    return 0;
}

int Confirm(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Ask ask;
    ask.kind = AskKind::Confirm;
    ask.title = CutText(FieldText(L, 1, "title"));
    ask.message = CutText(FieldText(L, 1, "message"));
    ask.yes = CutText(FieldText(L, 1, "yes"));
    return Runner::Of(L).SuspendForAsk(L, std::move(ask),
                                       [](lua_State* T, bool accepted, const std::string&) {
                                           lua_pushboolean(T, accepted ? 1 : 0);
                                           return 1;
                                       });
}

int OpenGame(lua_State* L) {
    Ask ask;
    ask.kind = AskKind::OpenGame;
    ask.value = luaL_checkstring(L, 1);
    Runner::Of(L).PostAsk(std::move(ask));
    return 0;
}

} // namespace

void RegisterUi(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "set", &Set);
    SetFunction(L, runner, "push", &Push);
    SetFunction(L, runner, "pop", &Pop);
    SetFunction(L, runner, "patch", &Patch);
    SetFunction(L, runner, "toast", &Toast);
    SetFunction(L, runner, "confirm", &Confirm);
    lua_setfield(L, -2, "ui");
    lua_newtable(L);
    SetFunction(L, runner, "open_game", &OpenGame);
    lua_setfield(L, -2, "app");
}

} // namespace den::plugins::feature
