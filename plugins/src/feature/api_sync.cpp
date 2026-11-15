// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.sync, for a sync provider (permission sync): the engine's status, Sync now, the
// categories, the folder store, the user's server (http.user_host) and game downloads.

#include <system_error>

#include "den/base/files.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

SyncLink* LinkOf(lua_State* L) {
    return Runner::Of(L).Config().sync.get();
}

int NoEngine(lua_State* L) {
    return Refuse(L, "sync is not available in this app");
}

int StatusFn(lua_State* L) {
    SyncLink* link = LinkOf(L);
    if (link == nullptr) {
        return NoEngine(L);
    }
    PushJson(L, link->StatusOf(Runner::Of(L).Config().manifest.id));
    return 1;
}

int Now(lua_State* L) {
    SyncLink* link = LinkOf(L);
    if (link == nullptr) {
        return NoEngine(L);
    }
    if (const Status started = link->SyncNow(Runner::Of(L).Config().manifest.id); !started.ok) {
        return Refuse(L, started.error);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int Categories(lua_State* L) {
    SyncLink* link = LinkOf(L);
    if (link == nullptr) {
        return NoEngine(L);
    }
    const std::vector<std::string> categories = link->EnabledCategories(Runner::Of(L).Config().manifest.id);
    lua_createtable(L, static_cast<int>(categories.size()), 0);
    for (std::size_t i = 0; i < categories.size(); ++i) {
        lua_pushstring(L, categories[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int Folder(lua_State* L) {
    SyncLink* link = LinkOf(L);
    const fs::path folder = link ? link->Folder(Runner::Of(L).Config().manifest.id) : fs::path();
    if (folder.empty()) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, PathToUtf8(folder).c_str());
    }
    return 1;
}

// After the user chose or confirmed a folder: checks it, creates it, makes it the store.
int TakeFolder(lua_State* T, Runner& runner, const fs::path& folder) {
    SyncLink* link = runner.Config().sync.get();
    if (const Status fine = link->CheckFolder(folder); !fine.ok) {
        return Refuse(T, fine.error);
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (const Status used = link->UseFolder(runner.Config().manifest.id, folder); !used.ok) {
        return Refuse(T, used.error);
    }
    lua_pushstring(T, PathToUtf8(folder).c_str());
    return 1;
}

// use_folder(path?): nil opens the folder picker; "~/..." is under the user's home (the shared
// storage on Android); the user confirms a folder the plugin names.
int UseFolder(lua_State* L) {
    SyncLink* link = LinkOf(L);
    if (link == nullptr) {
        return NoEngine(L);
    }
    Runner& runner = Runner::Of(L);
    const std::string wanted = luaL_optstring(L, 1, "");
    Ask ask;
    if (wanted.empty()) {
        ask.kind = AskKind::PickFile;
        ask.value = "folder";
        ask.title = "Choose the folder to sync";
        ask.message = PathToUtf8(link->Home());
        return runner.SuspendForAsk(L, std::move(ask), [&runner](lua_State* T, bool accepted,
                                                                const std::string& value) {
            if (!accepted || value.empty()) {
                return Refuse(T, "no folder was chosen");
            }
            return TakeFolder(T, runner, PathFromUtf8(value));
        });
    }
    fs::path folder;
    if (wanted == "~" || wanted.starts_with("~/")) {
        const std::string rest = wanted.size() > 2 ? wanted.substr(2) : std::string();
        if (!rest.empty() && !IsSafeRelativePath(rest)) {
            return Refuse(L, "that is not a folder under your home");
        }
        folder = rest.empty() ? link->Home() : link->Home() / PathFromUtf8(rest);
    } else {
        folder = PathFromUtf8(wanted);
        if (!folder.is_absolute()) {
            return Refuse(L, "the folder is an absolute path or starts with ~/");
        }
    }
    folder = folder.lexically_normal();
    if (const Status fine = link->CheckFolder(folder); !fine.ok) {
        return Refuse(L, fine.error);
    }
    ask.kind = AskKind::Confirm;
    ask.title = "Keep the sync files here?";
    ask.message = runner.Config().manifest.name + " keeps your synced files as plain files in " +
                  PathToUtf8(folder) + ".";
    ask.yes = "Use this folder";
    return runner.SuspendForAsk(L, std::move(ask), [&runner, folder](lua_State* T, bool accepted,
                                                                    const std::string&) {
        if (!accepted) {
            return Refuse(T, "declined");
        }
        return TakeFolder(T, runner, folder);
    });
}

int SetServer(lua_State* L) {
    const std::string url = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_HTTP_USER_HOST)) {
        return Refuse(L, "the plugin has no permission to connect to a server you enter (http.user_host)");
    }
    const UrlParts parts = ParseUrl(url);
    if (!parts.valid || (parts.scheme != "http" && parts.scheme != "https")) {
        return Refuse(L, "that is not a web address");
    }
    std::string origin = parts.scheme + "://" + parts.host;
    if (parts.port != 0 && parts.port != (parts.scheme == "https" ? 443 : 80)) {
        origin += ":" + std::to_string(parts.port);
    }
    if (origin == runner.UserServer()) {
        lua_pushboolean(L, 1);
        return 1;
    }
    Ask ask;
    ask.kind = AskKind::Confirm;
    ask.title = "Connect to " + parts.host + "?";
    ask.message = runner.Config().manifest.name + " will send your synced files to " + origin + ".";
    if (parts.scheme == "http") {
        ask.message += " This server is reached over plain http: others on the same network could "
                       "read what is sent. Use it on your home network only.";
    }
    ask.yes = "Connect";
    return runner.SuspendForAsk(L, std::move(ask), [&runner, origin](lua_State* T, bool accepted,
                                                                    const std::string&) {
        if (!accepted) {
            return Refuse(T, "declined");
        }
        if (!runner.SetUserServer(origin)) {
            return Refuse(T, "the server could not be remembered");
        }
        lua_pushboolean(T, 1);
        return 1;
    });
}

int DownloadGame(lua_State* L) {
    SyncLink* link = LinkOf(L);
    if (link == nullptr) {
        return NoEngine(L);
    }
    std::vector<std::string> paths;
    if (lua_type(L, 1) == LUA_TSTRING) {
        paths.push_back(lua_tostring(L, 1));
    } else {
        luaL_checktype(L, 1, LUA_TTABLE);
        for (lua_Integer i = 1; i <= 256; ++i) {
            lua_rawgeti(L, 1, i);
            if (lua_type(L, -1) != LUA_TSTRING) {
                lua_pop(L, 1);
                break;
            }
            paths.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    if (paths.empty()) {
        return Refuse(L, "name the game's files");
    }
    Runner& runner = Runner::Of(L);
    return runner.Suspend(L, [&runner, paths]() -> Outcome {
        const Status done = runner.Config().sync->DownloadGame(runner.Config().manifest.id, paths);
        return [done](lua_State* T) {
            if (!done.ok) {
                return Refuse(T, done.error);
            }
            lua_pushboolean(T, 1);
            return 1;
        };
    });
}

} // namespace

void RegisterSync(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "status", &StatusFn);
    SetFunction(L, runner, "now", &Now);
    SetFunction(L, runner, "enabled_categories", &Categories);
    SetFunction(L, runner, "folder", &Folder);
    SetFunction(L, runner, "use_folder", &UseFolder);
    SetFunction(L, runner, "set_server", &SetServer);
    SetFunction(L, runner, "download_game", &DownloadGame);
    lua_setfield(L, -2, "sync");
}

} // namespace den::plugins::feature
