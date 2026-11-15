// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.downloads: EmuDEN's Downloads folder and the setup of its files as games and hacks
// (library/include/den/library/downloads.h). Only for a plugin with downloads.setup, which
// only an official plugin may have. The script names files by their name in the folder; it
// never reads them.

#include <algorithm>
#include <chrono>

#include "den/base/files.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr const char* NO_FOLDER = "this version of the app has no Downloads folder";

void SetText(lua_State* L, const char* key, const std::string& value) {
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}

void PushGames(lua_State* L, const char* key, const std::vector<GameFacts>& games) {
    lua_createtable(L, static_cast<int>(games.size()), 0);
    for (std::size_t i = 0; i < games.size(); ++i) {
        lua_createtable(L, 0, 4);
        SetText(L, "id", games[i].id);
        SetText(L, "name", games[i].name);
        SetText(L, "system", games[i].system);
        SetText(L, "cover", "game:" + games[i].id);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, key);
}

void PushFacts(lua_State* L, const DownloadFacts& facts) {
    lua_createtable(L, 0, 10);
    SetText(L, "id", facts.id);
    SetText(L, "outcome", facts.outcome);
    SetText(L, "message", facts.message);
    SetText(L, "download", facts.download);
    SetText(L, "console", facts.console);
    lua_pushinteger(L, static_cast<lua_Integer>(facts.time));
    lua_setfield(L, -2, "time");
    lua_pushboolean(L, facts.replaced);
    lua_setfield(L, -2, "replaced");
    lua_pushboolean(L, facts.undone);
    lua_setfield(L, -2, "undone");
    PushGames(L, "games", facts.games);
    PushGames(L, "choices", facts.choices);
}

std::shared_ptr<FeatureServices> Services(Runner& runner) {
    const auto& services = runner.Config().services;
    return services && !services->DownloadsFolder().empty() ? services : nullptr;
}

// A file of the Downloads folder by its name: a plain name, no folder, nothing hidden.
Result<fs::path> FileNamed(FeatureServices& services, const std::string& name) {
    const fs::path file = PathFromUtf8(name);
    if (name.empty() || name.front() == '.' || file.has_parent_path() || file.is_absolute() ||
        name.find_first_of("/\\:") != std::string::npos) {
        return Status::Fail("that is not the name of a file in Downloads");
    }
    const fs::path path = services.DownloadsFolder() / file;
    if (!IsRegularFile(path)) {
        return Status::Fail(name + " is not in Downloads");
    }
    return path;
}

// den.downloads.folder(): where the folder is, to show the user.
int Folder(lua_State* L) {
    const auto services = Services(Runner::Of(L));
    if (!services) {
        return Refuse(L, NO_FOLDER);
    }
    const std::string path = PathToUtf8(services->DownloadsFolder());
    lua_pushlstring(L, path.data(), path.size());
    return 1;
}

// den.downloads.files(): {name, size, modified} of the files in the folder, newest first.
int Files(lua_State* L) {
    const auto services = Services(Runner::Of(L));
    if (!services) {
        return Refuse(L, NO_FOLDER);
    }
    struct Item {
        std::string name;
        std::uint64_t size = 0;
        std::int64_t modified = 0;
    };
    std::vector<Item> items;
    for (const fs::path& path : ListDirectory(services->DownloadsFolder())) {
        const std::string name = PathToUtf8(path.filename());
        if (name.empty() || name.front() == '.' || !IsRegularFile(path)) {
            continue;
        }
        std::error_code ec;
        Item item{name, fs::file_size(path, ec), 0};
        const auto written = fs::last_write_time(path, ec);
        if (!ec) {
            const auto since =
                std::chrono::duration_cast<std::chrono::seconds>(written - fs::file_time_type::clock::now());
            item.modified = Runner::Of(L).Config().now() + since.count();
        }
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.modified > b.modified; });
    lua_createtable(L, static_cast<int>(items.size()), 0);
    for (std::size_t i = 0; i < items.size() && i < 200; ++i) {
        lua_createtable(L, 0, 3);
        SetText(L, "name", items[i].name);
        lua_pushinteger(L, static_cast<lua_Integer>(items[i].size));
        lua_setfield(L, -2, "size");
        lua_pushinteger(L, static_cast<lua_Integer>(items[i].modified));
        lua_setfield(L, -2, "modified");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// den.downloads.setup{file, page_title?, source_host?, base?} (wait): sets the file up.
int SetUp(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    const auto services = Services(runner);
    if (!services) {
        return Refuse(L, NO_FOLDER);
    }
    auto file = FileNamed(*services, FieldText(L, 1, "file"));
    if (!file) {
        return Refuse(L, file.error());
    }
    DownloadOrder order;
    order.file = file.take();
    order.page_title = CutText(FieldText(L, 1, "page_title"));
    order.host = CutText(FieldText(L, 1, "source_host"));
    order.base_id = FieldText(L, 1, "base");
    order.plugin = runner.Config().manifest.id;
    return runner.Suspend(L, [services, order, &runner]() -> Outcome {
        Result<DownloadFacts> done = services->SetUpDownload(order, runner.Cancel());
        return [done = std::move(done)](lua_State* T) {
            if (!done) {
                return Refuse(T, done.error());
            }
            PushFacts(T, done.value());
            return 1;
        };
    });
}

// den.downloads.history(): the setups, newest first.
int History(lua_State* L) {
    const auto services = Services(Runner::Of(L));
    if (!services) {
        return Refuse(L, NO_FOLDER);
    }
    const std::vector<DownloadFacts> history = services->DownloadHistory();
    lua_createtable(L, static_cast<int>(history.size()), 0);
    for (std::size_t i = 0; i < history.size(); ++i) {
        PushFacts(L, history[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// den.downloads.undo(id) (wait): takes a setup back.
int Undo(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    const auto services = Services(runner);
    if (!services) {
        return Refuse(L, NO_FOLDER);
    }
    return runner.Suspend(L, [services, id]() -> Outcome {
        const Status undone = services->UndoDownload(id);
        return [undone](lua_State* T) {
            if (!undone.ok) {
                return Refuse(T, undone.error);
            }
            lua_pushboolean(T, 1);
            return 1;
        };
    });
}

} // namespace

void RegisterDownloads(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "folder", &Folder);
    SetFunction(L, runner, "files", &Files);
    SetFunction(L, runner, "setup", &SetUp);
    SetFunction(L, runner, "history", &History);
    SetFunction(L, runner, "undo", &Undo);
    lua_setfield(L, -2, "downloads");
}

} // namespace den::plugins::feature
