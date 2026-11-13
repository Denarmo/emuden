// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.files: the plugin's own folder, files the user picks and the Downloads folder.

#include <algorithm>
#include <chrono>
#include <system_error>

#include "den/base/files.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t MAX_READ = 4ull << 20;
constexpr std::size_t MAX_INBOX = 50;

// The file's own name, made safe to live in the plugin's folder.
std::string SafeName(const fs::path& file) {
    std::string name = PathToUtf8(file.filename());
    for (char& c : name) {
        if (static_cast<unsigned char>(c) < 0x20 || std::string_view("/\\:*?\"<>|").find(c) !=
                                                        std::string_view::npos) {
            c = '_';
        }
    }
    while (!name.empty() && name.front() == '.') {
        name.erase(0, 1);
    }
    if (name.size() > 120) {
        const std::string extension = ExtensionOf(file);
        std::size_t end = 120 - std::min<std::size_t>(extension.size() + 1, 20);
        while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80) {
            --end;
        }
        name = name.substr(0, end) + "." + extension;
    }
    return name.empty() ? "file" : name;
}

Result<std::string> CopyToInbox(Runner& runner, const fs::path& file,
                                const std::vector<std::string>& extensions) {
    return CopyIntoInbox(runner.Config().data_dir, file, extensions, runner.StorageLeft());
}

int PushCopied(lua_State* L, const Result<std::string>& copied) {
    if (!copied) {
        return Refuse(L, copied.error());
    }
    lua_pushstring(L, copied.value().c_str());
    return 1;
}

int Import(lua_State* L) {
    Runner& runner = Runner::Of(L);
    const FeatureManifest& manifest = runner.Config().manifest;
    if (!manifest.access.Has(PERMISSION_FILES_IMPORT)) {
        return Refuse(L, "the plugin has no permission to read files you choose");
    }
    Ask ask;
    ask.kind = AskKind::PickFile;
    if (lua_istable(L, 1)) {
        ask.title = CutText(FieldText(L, 1, "title"));
        ask.extensions = FieldList(L, 1, "extensions");
    }
    if (ask.extensions.empty()) {
        ask.extensions = manifest.file_types;
    }
    const std::vector<std::string> extensions = ask.extensions;
    return runner.SuspendForAsk(
        L, std::move(ask),
        [&runner, extensions](lua_State* T, bool accepted, const std::string& value) {
            if (!accepted || value.empty()) {
                lua_pushnil(T);
                return 1;
            }
            return PushCopied(T, CopyToInbox(runner, PathFromUtf8(value), extensions));
        });
}

// The patch files in the device's Downloads folder, newest first.
int Inbox(lua_State* L) {
    Runner& runner = Runner::Of(L);
    const RunnerConfig& config = runner.Config();
    if (!config.manifest.access.Has(PERMISSION_DOWNLOADS_WATCH)) {
        return Refuse(L, "the plugin has no permission to see your Downloads folder");
    }
    struct Found {
        fs::path path;
        std::uint64_t size = 0;
        std::int64_t modified = 0;
    };
    std::vector<Found> files;
    std::error_code ec;
    for (const fs::path& path : ListDirectory(config.downloads_dir)) {
        const std::string extension = ExtensionOf(path);
        if (!fs::is_regular_file(path, ec) ||
            std::find(config.manifest.file_types.begin(), config.manifest.file_types.end(),
                      extension) == config.manifest.file_types.end()) {
            continue;
        }
        const auto written = fs::last_write_time(path, ec);
        const auto since = std::chrono::duration_cast<std::chrono::seconds>(
            written - fs::file_time_type::clock::now());
        files.push_back({path, fs::file_size(path, ec), config.now() + since.count()});
    }
    std::sort(files.begin(), files.end(),
              [](const Found& a, const Found& b) { return a.modified > b.modified; });
    if (files.size() > MAX_INBOX) {
        files.resize(MAX_INBOX);
    }
    auto& tokens = runner.InboxTokens();
    tokens.clear();
    lua_createtable(L, static_cast<int>(files.size()), 0);
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::string token = "d" + std::to_string(i + 1);
        tokens[token] = files[i].path;
        lua_createtable(L, 0, 4);
        lua_pushstring(L, PathToUtf8(files[i].path.filename()).c_str());
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, static_cast<lua_Integer>(files[i].size));
        lua_setfield(L, -2, "size");
        lua_pushinteger(L, static_cast<lua_Integer>(files[i].modified));
        lua_setfield(L, -2, "modified");
        lua_pushstring(L, token.c_str());
        lua_setfield(L, -2, "token");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int Take(lua_State* L) {
    const std::string token = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_DOWNLOADS_WATCH)) {
        return Refuse(L, "the plugin has no permission to see your Downloads folder");
    }
    const auto found = runner.InboxTokens().find(token);
    if (found == runner.InboxTokens().end()) {
        return Refuse(L, "that file is no longer listed; call den.files.inbox again");
    }
    return PushCopied(L, CopyToInbox(runner, found->second, runner.Config().manifest.file_types));
}

int List(lua_State* L) {
    const std::string dir = luaL_optstring(L, 1, "");
    fs::path path;
    if (const Status found = Runner::Of(L).Resolve(dir, true, path); !found.ok) {
        return Refuse(L, found.error);
    }
    lua_newtable(L);
    lua_Integer index = 1;
    std::error_code ec;
    for (const fs::path& entry : ListDirectory(path)) {
        const bool folder = fs::is_directory(entry, ec);
        lua_createtable(L, 0, 3);
        lua_pushstring(L, PathToUtf8(entry.filename()).c_str());
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, folder ? 1 : 0);
        lua_setfield(L, -2, "dir");
        lua_pushinteger(L, folder ? 0 : static_cast<lua_Integer>(fs::file_size(entry, ec)));
        lua_setfield(L, -2, "size");
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int Exists(lua_State* L) {
    fs::path path;
    std::error_code ec;
    const bool there = Runner::Of(L).Resolve(luaL_checkstring(L, 1), true, path).ok &&
                       fs::exists(path, ec);
    lua_pushboolean(L, there ? 1 : 0);
    return 1;
}

int Read(lua_State* L) {
    fs::path path;
    if (const Status found = Runner::Of(L).Resolve(luaL_checkstring(L, 1), true, path);
        !found.ok) {
        return Refuse(L, found.error);
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) > MAX_READ) {
        return Refuse(L, "there is no such file of at most 4 MiB");
    }
    const auto text = ReadTextFile(path);
    if (!text) {
        return Refuse(L, "the file could not be read");
    }
    lua_pushlstring(L, text->data(), text->size());
    return 1;
}

// Only what the plugin itself brought in: downloads, inbox and cache.
int Remove(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    fs::path path;
    if (const Status found = Runner::Of(L).Resolve(name, false, path); !found.ok) {
        return Refuse(L, found.error);
    }
    const bool own = name.starts_with("downloads/") || name.starts_with("inbox/") ||
                     name.starts_with("cache/");
    std::error_code ec;
    if (!own || !fs::is_regular_file(path, ec)) {
        return Refuse(L, "only files in downloads, inbox and cache can be removed");
    }
    lua_pushboolean(L, fs::remove(path, ec) ? 1 : 0);
    return 1;
}

} // namespace

std::string ExtensionOf(const fs::path& file) {
    std::string extension = ToLower(PathToUtf8(file.extension()));
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(0, 1);
    }
    return extension;
}

std::vector<std::string> FieldList(lua_State* L, int table, const char* key) {
    std::vector<std::string> out;
    lua_getfield(L, table, key);
    if (lua_istable(L, -1)) {
        const lua_Unsigned count = std::min<lua_Unsigned>(lua_rawlen(L, -1), 32);
        for (lua_Unsigned i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            if (lua_type(L, -1) == LUA_TSTRING) {
                out.push_back(ToLower(lua_tostring(L, -1)));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return out;
}

Result<std::string> CopyIntoInbox(const fs::path& data_dir, const fs::path& file,
                                  const std::vector<std::string>& extensions,
                                  std::uint64_t room) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return Status::Fail("that file cannot be read");
    }
    if (!extensions.empty() &&
        std::find(extensions.begin(), extensions.end(), ExtensionOf(file)) == extensions.end()) {
        return Status::Fail("the plugin does not take this kind of file");
    }
    if (fs::file_size(file, ec) > room) {
        return Status::Fail("the plugin's folder is full");
    }
    const fs::path inbox = data_dir / "inbox";
    fs::create_directories(inbox, ec);
    const std::string name = SafeName(file);
    fs::copy_file(file, inbox / PathFromUtf8(name), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return Status::Fail("the file could not be copied");
    }
    return "inbox/" + name;
}

void RegisterFiles(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "import", &Import);
    SetFunction(L, runner, "inbox", &Inbox);
    SetFunction(L, runner, "take", &Take);
    SetFunction(L, runner, "list", &List);
    SetFunction(L, runner, "exists", &Exists);
    SetFunction(L, runner, "read", &Read);
    SetFunction(L, runner, "remove", &Remove);
    lua_setfield(L, -2, "files");
}

} // namespace den::plugins::feature
