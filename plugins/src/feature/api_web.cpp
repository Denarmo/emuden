// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// den.web: the in-app browser. The plugin names a page of a site it declared; the user
// browses it, and every patch file the user downloads there is handed to the plugin. The
// plugin never sees the page, and nothing is fetched for it: the screen does the showing
// (route "web", which is the user's own browser and the Downloads folder where the device has
// no web engine), this file only decides what may be opened and which files are taken.

#include <algorithm>
#include <system_error>
#include <utility>

#include "den/base/files.h"
#include "den/base/json.h"
#include "den/base/zip.h"
#include "den/patching/patch.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins {

std::string WebFileAnswer(const WebFile& file) {
    return feature::CompactJson(JsonObject({{"file", JsonString(file.file)},
                                   {"name", JsonString(file.name)},
                                   {"host", JsonString(file.host)}}));
}

} // namespace den::plugins

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t MAX_PATCH = 256ull << 20;

// What a plugin can be handed from a web site: patches, or a zip holding one. Never a ROM
// image, whatever the plugin asks for. (7z cannot be opened here, so it cannot be checked.)
bool Takes(const std::string& extension) {
    return !patching::IsRomImageExtension(extension) &&
           (patching::IsPatchExtension(extension) || extension == "zip");
}

std::vector<std::string> DefaultExtensions() {
    return {"bps", "ips", "ups", "xdelta", "vcdiff", "aps", "ppf", "zip"};
}

bool Has(const std::vector<std::string>& list, const std::string& item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

bool IsPatch(Runner& runner, const fs::path& file) {
    if (const auto& services = runner.Config().services) {
        return services->InspectPatch(file).ok();
    }
    return patching::Inspect(file).ok();
}

void Remove(const fs::path& file) {
    std::error_code ec;
    fs::remove(file, ec);
}

// The patch a zip holds, unpacked beside it. An archive with a ROM image in it is refused
// whole: a patched game is not a patch.
Result<fs::path> PatchInZip(const fs::path& archive) {
    const auto members = ListZip(archive);
    if (!members) {
        return Status::Fail("the archive cannot be read");
    }
    const ZipEntry* patch = nullptr;
    for (const ZipEntry& member : *members) {
        const std::string extension = ExtensionOf(PathFromUtf8(member.name));
        if (patching::IsRomImageExtension(extension)) {
            return Status::Fail("the archive holds a game, not only a patch");
        }
        if (patch == nullptr && patching::IsPatchExtension(extension) && IsSafeRelativePath(member.name) &&
            CanExtractZipEntry(member) && !member.symlink && member.size <= MAX_PATCH) {
            patch = &member;
        }
    }
    if (patch == nullptr) {
        return Status::Fail("there is no patch file in the archive");
    }
    const fs::path out = archive.parent_path() / PathFromUtf8("unpacked-" + PathToUtf8(archive.stem())) /
                         PathFromUtf8(patch->name).filename();
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    if (const Status unpacked = ExtractZipEntry(archive, *patch, out); !unpacked.ok) {
        return unpacked;
    }
    return out;
}

// Checks a file the screen handed over and moves it into the plugin's inbox.
Result<WebVisit::File> TakeFile(Runner& runner, const WebFile& arrived) {
    const fs::path file = PathFromUtf8(arrived.file);
    const std::string extension = ExtensionOf(PathFromUtf8(arrived.name.empty() ? arrived.file : arrived.name));
    const WebVisit& visit = runner.Web();
    Result<WebVisit::File> result = Status::Fail("");
    fs::path patch = file;
    fs::path unpacked_dir;
    if (patching::IsRomImageExtension(extension) || patching::IsRomImageExtension(ExtensionOf(file))) {
        result = Status::Fail("games are never taken from a web site, only patches");
    } else if (!Takes(extension) || !Has(visit.extensions, extension)) {
        result = Status::Fail("the plugin does not take this kind of file");
    } else {
        Status ready;
        if (extension == "zip") {
            auto inside = PatchInZip(file);
            if (inside) {
                patch = inside.take();
                unpacked_dir = patch.parent_path();
            } else {
                ready = inside.status();
            }
        }
        if (ready.ok && !IsPatch(runner, patch)) {
            ready = Status::Fail("the file is not a patch");
        }
        if (!ready.ok) {
            result = ready;
        } else if (auto copied = CopyIntoInbox(runner.Config().data_dir, patch, {},
                                               std::min(MAX_PATCH, runner.StorageLeft()));
                   copied) {
            result = WebVisit::File{copied.take(), PathToUtf8(patch.filename()), arrived.host};
        } else {
            result = copied.status();
        }
    }
    // The download was the plugin's to take; nothing of it stays in the app's inbox.
    Remove(file);
    if (!unpacked_dir.empty()) {
        std::error_code ec;
        fs::remove_all(unpacked_dir, ec);
    }
    return result;
}

int PushFile(lua_State* L, const WebVisit::File& file) {
    lua_newtable(L);
    lua_pushstring(L, file.path.c_str());
    lua_setfield(L, -2, "path");
    lua_pushstring(L, file.name.c_str());
    lua_setfield(L, -2, "name");
    lua_pushstring(L, file.host.c_str());
    lua_setfield(L, -2, "source_host");
    return 1;
}

// An answer of the screen to the open page: a file arrived, or the page was closed.
void OnAnswer(Runner& runner, bool accepted, const std::string& value) {
    WebVisit& visit = runner.Web();
    if (!accepted) {
        visit.open = false;
        if (const std::uint64_t waiting = std::exchange(visit.waiting, 0)) {
            runner.Wake(waiting, [](lua_State* L) { return Refuse(L, "closed"); });
        }
        return;
    }
    const auto parsed = ParseJson(value);
    if (!parsed || parsed->kind != JsonValue::Kind::Object) {
        return;
    }
    const WebFile arrived{parsed->String("file"), parsed->String("name"), parsed->String("host")};
    Result<WebVisit::File> taken = TakeFile(runner, arrived);
    if (!taken) {
        runner.AddLog("den.web: " + arrived.name + " refused: " + taken.error());
        Ask toast;
        toast.kind = AskKind::Toast;
        toast.message = CutText(arrived.name + " was not taken: " + taken.error());
        runner.PostAsk(std::move(toast));
        return;
    }
    if (const std::uint64_t waiting = std::exchange(visit.waiting, 0)) {
        runner.Wake(waiting, [file = taken.take()](lua_State* L) { return PushFile(L, file); });
    } else if (visit.ready.size() < 16) {
        visit.ready.push_back(taken.take());
    }
}

// den.web.available(): whether pages show inside the app, and how den.web.open works here.
int Available(lua_State* L) {
    const auto& available = Runner::Of(L).Config().web_available;
    const bool in_app = available && available();
    lua_pushboolean(L, in_app);
    lua_pushstring(L, in_app ? "in-app" : "browser");
    return 2;
}

// den.web.open{url, hosts?, title?, extensions?}: shows the page and waits for the next file
// the user downloads there. Called again while the page is open, it waits for the next one.
int Open(lua_State* L) {
    Runner& runner = Runner::Of(L);
    const FeatureManifest& manifest = runner.Config().manifest;
    if (!manifest.access.Has(PERMISSION_BROWSE)) {
        return Refuse(L, "the plugin has no permission to show websites");
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    WebVisit& visit = runner.Web();
    if (visit.waiting != 0) {
        return Refuse(L, "den.web.open is already waiting for a file");
    }
    if (!visit.open) {
        const std::string url = FieldText(L, 1, "url");
        std::vector<std::string> hosts = FieldList(L, 1, "hosts");
        if (hosts.empty()) {
            hosts = manifest.access.browse_hosts;
        }
        for (const std::string& host : hosts) {
            if (!Has(manifest.access.browse_hosts, host)) {
                return Refuse(L, "the plugin did not declare the host " + host);
            }
        }
        if (const Status allowed = CheckUrl(url, hosts); !allowed.ok) {
            return Refuse(L, allowed.error);
        }
        std::vector<std::string> asked = FieldList(L, 1, "extensions");
        if (asked.empty()) {
            asked = manifest.file_types.empty() ? DefaultExtensions() : manifest.file_types;
        }
        std::vector<std::string> extensions;
        for (const std::string& extension : asked) {
            if (Takes(extension) && !Has(extensions, extension)) {
                extensions.push_back(extension);
            }
        }
        if (extensions.empty()) {
            return Refuse(L, "only patch files can be taken from a web site, never a game");
        }
        if (runner.CoroutineOf(L) == 0) {
            return runner.Park(L); // raises: not one of the plugin's own functions
        }
        Ask ask;
        ask.kind = AskKind::OpenWeb;
        ask.value = url;
        ask.title = CutText(FieldText(L, 1, "title"));
        ask.hosts = std::move(hosts);
        ask.extensions = extensions;
        visit.extensions = std::move(extensions);
        visit.ready.clear();
        visit.open = true;
        runner.PostStream(std::move(ask), [&runner](bool accepted, const std::string& value) {
            OnAnswer(runner, accepted, value);
        });
    }
    if (!visit.ready.empty()) {
        const WebVisit::File file = std::move(visit.ready.front());
        visit.ready.pop_front();
        return PushFile(L, file);
    }
    visit.waiting = runner.CoroutineOf(L);
    return runner.Park(L);
}

// den.web.close(): takes the page away, for a plugin that has what it came for.
int Close(lua_State* L) {
    Runner& runner = Runner::Of(L);
    if (runner.Web().open) {
        Ask ask;
        ask.kind = AskKind::CloseWeb;
        runner.PostAsk(std::move(ask));
    }
    return 0;
}

} // namespace

void RegisterWeb(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "available", &Available);
    SetFunction(L, runner, "open", &Open);
    SetFunction(L, runner, "close", &Close);
    lua_setfield(L, -2, "web");
}

} // namespace den::plugins::feature
