// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.http.get, den.download, den.open_url.

#include <algorithm>
#include <chrono>
#include <memory>
#include <system_error>

#include "den/base/files.h"
#include "den/base/sha256.h"
#include "feature/runner.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

// A name for a file of the plugin's folder: one path component, nothing hidden.
bool IsFileName(std::string_view name) {
    return !name.empty() && name.size() <= 120 && name.front() != '.' &&
           name.find('/') == std::string_view::npos && IsSafeRelativePath(name);
}

int HttpGet(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_HTTP)) {
        return Refuse(L, "the plugin has no permission to connect (http)");
    }
    const std::string url = FieldText(L, 1, "url");
    if (const Status allowed = runner.Http().Allowed(url); !allowed.ok) {
        return Refuse(L, allowed.error);
    }
    std::vector<std::pair<std::string, std::string>> headers;
    lua_getfield(L, 1, "headers");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING &&
                headers.size() < 16) {
                headers.emplace_back(lua_tostring(L, -2), lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "cache_seconds");
    const int cache_seconds = static_cast<int>(
        std::clamp<lua_Integer>(luaL_optinteger(L, -1, 3600), 0, 30 * 24 * 3600));
    lua_pop(L, 1);
    return runner.Suspend(L, [&runner, url, headers, cache_seconds]() -> Outcome {
        HttpAnswer answer = runner.Http().Get(url, headers, cache_seconds);
        return [answer = std::move(answer)](lua_State* T) {
            if (!answer.error.empty()) {
                return Refuse(T, answer.error);
            }
            lua_createtable(T, 0, 4);
            lua_pushinteger(T, answer.status);
            lua_setfield(T, -2, "status");
            lua_pushlstring(T, answer.body.data(), answer.body.size());
            lua_setfield(T, -2, "body");
            lua_newtable(T); // answer headers are not available from the platform layer yet
            lua_setfield(T, -2, "headers");
            lua_pushboolean(T, answer.cached ? 1 : 0);
            lua_setfield(T, -2, "cached");
            return 1;
        };
    });
}

int Download(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_DOWNLOAD)) {
        return Refuse(L, "the plugin has no permission to download files");
    }
    const std::string url = FieldText(L, 1, "url");
    const std::string name = FieldText(L, 1, "name");
    const std::string sha256 = ToLower(FieldText(L, 1, "sha256"));
    if (const Status allowed = runner.Http().Allowed(url); !allowed.ok) {
        return Refuse(L, allowed.error);
    }
    if (!IsFileName(name)) {
        return Refuse(L, "name must be a plain file name");
    }
    if (!sha256.empty() && !IsSha256Hex(sha256)) {
        return Refuse(L, "sha256 is not a SHA-256 checksum");
    }
    const std::uint64_t room = std::min<std::uint64_t>(MAX_DOWNLOAD, runner.StorageLeft());
    if (room == 0) {
        return Refuse(L, "the plugin's folder is full");
    }
    int progress_ref = LUA_NOREF;
    lua_getfield(L, 1, "on_progress");
    if (lua_isfunction(L, -1)) {
        progress_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }
    const fs::path destination = runner.Config().data_dir / "downloads" / PathFromUtf8(name);
    return runner.Suspend(L, [&runner, url, name, sha256, room, progress_ref,
                              destination]() -> Outcome {
        std::error_code ec;
        fs::create_directories(destination.parent_path(), ec);
        auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        Status status = runner.Http().Download(url, destination, room, [&](float fraction) {
            const auto now = std::chrono::steady_clock::now();
            if (progress_ref != LUA_NOREF && now - last >= std::chrono::milliseconds(100)) {
                last = now;
                runner.CallProgress(progress_ref, fraction);
            }
        });
        if (status.ok && !sha256.empty() && Sha256File(destination) != sha256) {
            status = Status::Fail("the file is not the one that was expected");
        }
        if (!status.ok) {
            fs::remove(destination, ec);
        }
        if (progress_ref != LUA_NOREF) {
            runner.ReleaseRef(progress_ref);
        }
        return [status, name](lua_State* T) {
            if (!status.ok) {
                return Refuse(T, status.error);
            }
            lua_pushstring(T, ("downloads/" + name).c_str());
            return 1;
        };
    });
}

int OpenUrl(lua_State* L) {
    const std::string url = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    const FeatureAccess& access = runner.Config().manifest.access;
    if (!access.Has(PERMISSION_OPEN_URL)) {
        return Refuse(L, "the plugin has no permission to open pages");
    }
    if (const Status allowed = CheckUrl(url, access.open_hosts); !allowed.ok) {
        return Refuse(L, allowed.error);
    }
    Ask ask;
    ask.kind = AskKind::OpenUrl;
    ask.value = url;
    runner.PostAsk(std::move(ask));
    lua_pushboolean(L, 1);
    return 1;
}

} // namespace

void RegisterNetwork(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "get", &HttpGet);
    lua_setfield(L, -2, "http");
    SetFunction(L, runner, "download", &Download);
    SetFunction(L, runner, "open_url", &OpenUrl);
}

} // namespace den::plugins::feature
