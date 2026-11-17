// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.mods (docs/game-mods-api.md): a plugin with game.files looks into the folder of a
// Steam game, unpacks what it downloaded, and has the app copy files into the game's folder
// or set up a Vulkan layer. The changing calls work only after the user pressed something
// on the plugin's page and accepted the app's own question, which lists every file.

#include <algorithm>
#include <array>

#include "den/base/files.h"
#include "feature/mods.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr const char* NO_MODS = "this app cannot do that";
constexpr const char* NO_PERMISSION = "the plugin has no permission to change game files";
constexpr const char* NOT_ASKED = "games are changed only when the user asks for it";
// The file list of a question, in bytes; what does not fit is counted.
constexpr std::size_t MAX_LISTED = 3000;

// Two texts of a table and an optional third.
using TextRows = std::vector<std::array<std::string, 3>>;

// The store of the plugin's records, when it may use den.mods here.
bool Allowed(lua_State* L, Runner& runner, int& refused) {
    if (!runner.Config().manifest.access.Has(PERMISSION_GAME_FILES)) {
        refused = Refuse(L, NO_PERMISSION);
        return false;
    }
    if (!runner.Config().mods || runner.Config().mods_dir.empty()) {
        refused = Refuse(L, NO_MODS);
        return false;
    }
    return true;
}

std::string TextAt(lua_State* L, int index) {
    std::size_t size = 0;
    const char* data = lua_type(L, index) == LUA_TSTRING ? lua_tolstring(L, index, &size) : nullptr;
    return data != nullptr ? std::string(data, size) : std::string();
}

// An app id given as text or as a number.
std::string AppIdField(lua_State* L, int table) {
    lua_getfield(L, table, "appid");
    std::string appid = TextAt(L, -1);
    if (lua_isinteger(L, -1)) {
        appid = std::to_string(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    return appid;
}

// The texts of a list field, at most `max`. False for anything else.
bool Texts(lua_State* L, int table, const char* key, std::size_t max, std::vector<std::string>& out) {
    lua_getfield(L, table, key);
    const bool list = lua_istable(L, -1);
    const lua_Unsigned count = list ? lua_rawlen(L, -1) : 0;
    bool fine = list && count >= 1 && count <= max;
    for (lua_Unsigned i = 1; fine && i <= count; ++i) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
        fine = lua_type(L, -1) == LUA_TSTRING;
        out.push_back(TextAt(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return fine;
}

// A list field of tables with two texts each, by name ({from = .., to = ..}) or in order,
// and the text `third` names when it is there.
bool Rows(lua_State* L, int table, const char* key, const char* first, const char* second, const char* third,
          std::size_t max, TextRows& out) {
    lua_getfield(L, table, key);
    const bool list = lua_istable(L, -1);
    const lua_Unsigned count = list ? lua_rawlen(L, -1) : 0;
    bool fine = list && count >= 1 && count <= max;
    for (lua_Unsigned i = 1; fine && i <= count; ++i) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
        fine = lua_istable(L, -1);
        std::array<std::string, 3> texts;
        for (int part = 0; fine && part < 2; ++part) {
            if (lua_getfield(L, -1, part == 0 ? first : second) == LUA_TNIL) {
                lua_pop(L, 1);
                lua_rawgeti(L, -1, part + 1);
            }
            fine = lua_type(L, -1) == LUA_TSTRING;
            texts[part] = TextAt(L, -1);
            lua_pop(L, 1);
        }
        if (fine && third != nullptr) {
            fine = lua_getfield(L, -1, third) == LUA_TNIL || lua_type(L, -1) == LUA_TSTRING;
            texts[2] = TextAt(L, -1);
            lua_pop(L, 1);
        }
        out.push_back(std::move(texts));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return fine;
}

// A file of the plugin's folder that den.mods may read: under one of `folders`.
Status Source(const Runner& runner, const std::string& path, std::initializer_list<std::string_view> folders,
              fs::path& out) {
    const bool inside = std::any_of(folders.begin(), folders.end(),
                                    [&](std::string_view folder) { return path.starts_with(folder); });
    if (!inside) {
        return Status::Fail(path + " is not under " + std::string(*folders.begin()) +
                            (folders.size() > 1 ? " or " + std::string(*(folders.begin() + 1)) : std::string()));
    }
    return runner.Resolve(path, false, out);
}

// A name the app's question shows: one short line.
bool IsShownName(const std::string& name) {
    return !name.empty() && name.size() <= 80 &&
           std::none_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

std::int64_t Now(const Runner& runner) {
    return runner.Config().now ? runner.Config().now() : 0;
}

int PushTrue(lua_State* T) {
    lua_pushboolean(T, 1);
    return 1;
}

int Find(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    if (!Allowed(L, runner, refused)) {
        return refused;
    }
    const std::string appid = AppIdField(L, 1);
    std::vector<std::string> names;
    if (!Texts(L, 1, "names", mods::MAX_NAMES, names)) {
        return Refuse(L, "find takes 1 to 16 file names");
    }
    if (const Status fine = mods::CheckNames(names); !fine.ok) {
        return Refuse(L, fine.error);
    }
    return runner.Suspend(L, [services = runner.Config().mods, appid, names, &runner]() -> Outcome {
        const Result<ModGame> game = services->SteamGame(appid);
        if (!game) {
            return [error = game.error()](lua_State* T) { return Refuse(T, error); };
        }
        JsonValue files = JsonArray();
        for (const mods::Found& found : mods::Find(game.value().dir, names, &runner.Cancel())) {
            JsonValue file = JsonObject({{"path", JsonString(found.path)},
                                         {"size", JsonNumber(static_cast<double>(found.size))}});
            if (!found.sha256.empty()) {
                file.members.emplace_back("sha256", JsonString(found.sha256));
            }
            files.items.push_back(std::move(file));
        }
        return [files = std::move(files)](lua_State* T) {
            PushJson(T, files);
            return 1;
        };
    });
}

int Unpack(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_GAME_FILES)) {
        return Refuse(L, NO_PERMISSION);
    }
    const std::string file = FieldText(L, 1, "file");
    std::vector<std::string> entries;
    if (!Texts(L, 1, "entries", mods::MAX_ENTRIES, entries)) {
        return Refuse(L, "unpack takes 1 to 32 entries");
    }
    fs::path archive;
    if (const Status found = Source(runner, file, {"downloads/"}, archive); !found.ok) {
        return Refuse(L, found.error);
    }
    const std::string stem = mods::ArchiveStem(PathToUtf8(archive.filename()));
    if (!mods::IsPlainName(stem)) {
        return Refuse(L, "the archive's name cannot name a folder");
    }
    const std::string folder = "cache/mods/" + stem + "/";
    const fs::path out_dir = runner.Config().data_dir / "cache" / "mods" / PathFromUtf8(stem);
    const std::uint64_t room = runner.StorageLeft();
    return runner.Suspend(L, [archive, entries, out_dir, folder, room, &runner]() -> Outcome {
        const Status done =
            mods::Unpack(archive, entries, out_dir, room, [&runner] { return runner.Cancel().load(); });
        if (!done.ok) {
            return [error = done.error](lua_State* T) { return Refuse(T, error); };
        }
        JsonValue paths = JsonObject();
        for (const std::string& entry : entries) {
            paths.members.emplace_back(entry, JsonString(folder + entry));
        }
        return [paths = std::move(paths)](lua_State* T) {
            PushJson(T, paths);
            return 1;
        };
    });
}

// den.mods.apply{appid, name, files = { {from, to} }}: asks, then copies.
int Apply(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    if (!Allowed(L, runner, refused)) {
        return refused;
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, NOT_ASKED);
    }
    const std::string appid = AppIdField(L, 1);
    const std::string name = FieldText(L, 1, "name");
    TextRows rows;
    if (!IsShownName(name) || !Rows(L, 1, "files", "from", "to", "keep_as", mods::MAX_FILES, rows)) {
        return Refuse(L, "apply needs an appid, a name and 1 to 64 files, each with from and to");
    }
    std::vector<mods::Order> files;
    for (const auto& [from, to, keep_as] : rows) {
        mods::Order order;
        order.to = to;
        order.keep_as = keep_as;
        if (const Status found = Source(runner, from, {"cache/", "downloads/"}, order.from); !found.ok) {
            return Refuse(L, found.error);
        }
        files.push_back(std::move(order));
    }
    // Looked up on this thread: the question names the game and says what each file does.
    const Result<ModGame> game = runner.Config().mods->SteamGame(appid);
    if (!game) {
        return Refuse(L, game.error());
    }
    const mods::Store store(runner.Config().mods_dir);
    const Result<std::vector<mods::Planned>> plan = store.Plan(appid, game.value().dir, files);
    if (!plan) {
        return Refuse(L, plan.error());
    }
    std::string lines;
    std::size_t listed = 0;
    for (const mods::Planned& file : plan.value()) {
        if (lines.size() + file.to.size() > MAX_LISTED) {
            break;
        }
        lines += std::string(file.replaces ? "Replaces: " : "New: ") + file.to +
                 (file.keep.empty() ? "" : " (the game's file is kept as " + file.keep + ")") + "\n";
        ++listed;
    }
    if (listed < plan.value().size()) {
        lines += "and " + std::to_string(plan.value().size() - listed) + " more files\n";
    }
    if (store.Name(appid)) {
        lines += "What was set up before is undone first.\n";
    }
    Ask ask;
    ask.kind = AskKind::Confirm;
    ask.title = CutText("Set up " + name + " for " + game.value().name + "?");
    ask.message = CutText(lines + "Asked by " + runner.Config().manifest.name +
                          ". EmuDEN keeps the game's own files and can put them back.");
    ask.yes = "Set up";
    const fs::path dir = game.value().dir;
    return runner.SuspendForAsk(
        L, std::move(ask), [&runner, appid, name, dir, files](lua_State* T, bool accepted, const std::string&) {
            if (!accepted) {
                return Refuse(T, "cancelled");
            }
            return runner.ContinueWith(T, [&runner, appid, name, dir, files]() -> Outcome {
                mods::Store store(runner.Config().mods_dir);
                const Status done = store.Apply(appid, name, dir, files, Now(runner),
                                                [&runner] { return runner.Cancel().load(); });
                if (!done.ok) {
                    return [error = done.error](lua_State* R) { return Refuse(R, error); };
                }
                return &PushTrue;
            });
        });
}

int State(lua_State* L) {
    const std::string appid = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    if (!Allowed(L, runner, refused)) {
        return refused;
    }
    return runner.Suspend(L, [dir = runner.Config().mods_dir, appid]() -> Outcome {
        const std::optional<mods::Applied> applied = mods::Store(dir).State(appid);
        if (!applied) {
            return [](lua_State* T) {
                lua_pushnil(T);
                return 1;
            };
        }
        JsonValue files = JsonArray();
        for (const mods::FileState& file : applied->files) {
            files.items.push_back(JsonObject({{"to", JsonString(file.to)}, {"intact", JsonBool(file.intact)}}));
        }
        JsonValue out = JsonObject({{"name", JsonString(applied->name)},
                                    {"time", JsonNumber(static_cast<double>(applied->time))},
                                    {"complete", JsonBool(applied->complete)},
                                    {"files", std::move(files)}});
        return [out = std::move(out)](lua_State* T) {
            PushJson(T, out);
            return 1;
        };
    });
}

// den.mods.remove(appid): asks, then undoes the apply. Also gives the files left alone.
int Remove(lua_State* L) {
    const std::string appid = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    if (!Allowed(L, runner, refused)) {
        return refused;
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, NOT_ASKED);
    }
    const std::optional<std::string> name = mods::Store(runner.Config().mods_dir).Name(appid);
    if (!name) {
        lua_pushboolean(L, 1);
        lua_newtable(L);
        return 2;
    }
    const Result<ModGame> game = runner.Config().mods->SteamGame(appid);
    Ask ask;
    ask.kind = AskKind::Confirm;
    ask.title = CutText("Remove " + *name + " from " + (game ? game.value().name : "Steam app " + appid) + "?");
    ask.message = CutText("The files it added are deleted and the game's own files are put back. A file that "
                          "changed since (a game update) is left alone. Asked by " +
                          runner.Config().manifest.name + ".");
    ask.yes = "Remove";
    return runner.SuspendForAsk(L, std::move(ask), [&runner, appid](lua_State* T, bool accepted, const std::string&) {
        if (!accepted) {
            return Refuse(T, "cancelled");
        }
        return runner.ContinueWith(T, [dir = runner.Config().mods_dir, appid]() -> Outcome {
            const Result<std::vector<std::string>> left = mods::Store(dir).Remove(appid);
            if (!left) {
                return [error = left.error()](lua_State* R) { return Refuse(R, error); };
            }
            JsonValue files = JsonArray();
            for (const std::string& file : left.value()) {
                files.items.push_back(JsonString(file));
            }
            return [files = std::move(files)](lua_State* R) {
                lua_pushboolean(R, 1);
                PushJson(R, files);
                return 2;
            };
        });
    });
}

// den.mods.layer{name, layers = { {manifest, library} }}: asks, then sets the layers up.
int Layer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    if (!Allowed(L, runner, refused)) {
        return refused;
    }
    if (runner.Config().platform != "linux") {
        return Refuse(L, "Vulkan layers are set up on Linux only");
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, NOT_ASKED);
    }
    const std::string name = FieldText(L, 1, "name");
    TextRows rows;
    if (!Rows(L, 1, "layers", "manifest", "library", nullptr, mods::MAX_LAYERS, rows)) {
        return Refuse(L, "layer needs a name and 1 to 8 layers, each with manifest and library");
    }
    std::vector<mods::LayerOrder> layers;
    for (const auto& [manifest, library, unused] : rows) {
        mods::LayerOrder order;
        if (Status found = Source(runner, manifest, {"cache/"}, order.manifest); !found.ok) {
            return Refuse(L, found.error);
        }
        if (Status found = Source(runner, library, {"cache/"}, order.library); !found.ok) {
            return Refuse(L, found.error);
        }
        layers.push_back(std::move(order));
    }
    const fs::path manifest_dir = runner.Config().mods->LayerManifestDir();
    if (manifest_dir.empty()) {
        return Refuse(L, "this computer has no folder for Vulkan layers");
    }
    const std::string plugin = runner.Config().manifest.id;
    const Result<std::vector<std::string>> plan =
        mods::Store(runner.Config().mods_dir).PlanLayer(name, layers, plugin);
    if (!plan) {
        return Refuse(L, plan.error());
    }
    std::string lines;
    for (const std::string& manifest : plan.value()) {
        lines += "New: " + PathToUtf8(manifest_dir / PathFromUtf8(manifest)) + "\n";
    }
    Ask ask;
    ask.kind = AskKind::Confirm;
    ask.title = CutText("Set up the Vulkan layer " + name + "?");
    ask.message = CutText(lines + "Games that draw with Vulkan load a layer listed there. Asked by " +
                          runner.Config().manifest.name + ". EmuDEN can remove it again.");
    ask.yes = "Set up";
    return runner.SuspendForAsk(
        L, std::move(ask),
        [&runner, name, layers, manifest_dir, plugin](lua_State* T, bool accepted, const std::string&) {
            if (!accepted) {
                return Refuse(T, "cancelled");
            }
            return runner.ContinueWith(T, [&runner, name, layers, manifest_dir, plugin]() -> Outcome {
                mods::Store store(runner.Config().mods_dir);
                const Status done = store.ApplyLayer(name, layers, manifest_dir, plugin, Now(runner));
                if (!done.ok) {
                    return [error = done.error](lua_State* R) { return Refuse(R, error); };
                }
                return &PushTrue;
            });
        });
}

int LayerState(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    if (!Allowed(L, runner, refused)) {
        return refused;
    }
    return runner.Suspend(L, [dir = runner.Config().mods_dir, name]() -> Outcome {
        const std::optional<mods::LayerState> state = mods::Store(dir).Layer(name);
        if (!state) {
            return [](lua_State* T) {
                lua_pushnil(T);
                return 1;
            };
        }
        JsonValue manifests = JsonArray();
        for (const std::string& manifest : state->manifests) {
            manifests.items.push_back(JsonString(manifest));
        }
        JsonValue out = JsonObject({{"time", JsonNumber(static_cast<double>(state->time))},
                                    {"manifests", std::move(manifests)},
                                    {"intact", JsonBool(state->intact)}});
        return [out = std::move(out)](lua_State* T) {
            PushJson(T, out);
            return 1;
        };
    });
}

int RemoveLayer(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    Runner& runner = Runner::Of(L);
    int refused = 0;
    if (!Allowed(L, runner, refused)) {
        return refused;
    }
    if (!runner.UserTriggered(L)) {
        return Refuse(L, NOT_ASKED);
    }
    if (!mods::IsLayerName(name) || !IsRegularFile(runner.Config().mods_dir / "layers" / name / "record.json")) {
        return PushTrue(L);
    }
    Ask ask;
    ask.kind = AskKind::Confirm;
    ask.title = CutText("Remove the Vulkan layer " + name + "?");
    ask.message = CutText("Its files are deleted. Asked by " + runner.Config().manifest.name + ".");
    ask.yes = "Remove";
    return runner.SuspendForAsk(L, std::move(ask), [&runner, name](lua_State* T, bool accepted, const std::string&) {
        if (!accepted) {
            return Refuse(T, "cancelled");
        }
        return runner.ContinueWith(T, [dir = runner.Config().mods_dir, name]() -> Outcome {
            const Status done = mods::Store(dir).RemoveLayer(name);
            if (!done.ok) {
                return [error = done.error](lua_State* R) { return Refuse(R, error); };
            }
            return &PushTrue;
        });
    });
}

} // namespace

void RegisterMods(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "find", &Find);
    SetFunction(L, runner, "unpack", &Unpack);
    SetFunction(L, runner, "apply", &Apply);
    SetFunction(L, runner, "state", &State);
    SetFunction(L, runner, "remove", &Remove);
    SetFunction(L, runner, "layer", &Layer);
    SetFunction(L, runner, "layer_state", &LayerState);
    SetFunction(L, runner, "remove_layer", &RemoveLayer);
    lua_setfield(L, -2, "mods");
}

} // namespace den::plugins::feature
