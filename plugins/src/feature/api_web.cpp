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
#include "den/base/archive.h"
#include "den/patching/patch.h"
#include "feature/runner.h"
#include "feature/ui_model.h"

namespace den::plugins {

std::string WebFileAnswer(const WebFile& file) {
    return feature::CompactJson(JsonObject({{"file", JsonString(file.file)},
                                   {"name", JsonString(file.name)},
                                   {"host", JsonString(file.host)},
                                   {"title", JsonString(file.title)}}));
}

} // namespace den::plugins

namespace den::plugins::feature {

namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t MAX_PATCH = 256ull << 20;
// Where den.web.open{any = true} starts when the plugin names no page: a search page.
constexpr const char* HOME = "https://duckduckgo.com/";

// What a plugin can be handed from a web site: patches, or a zip or 7z holding one. Never a
// ROM image, whatever the plugin asks for. (rar cannot be opened here, so it cannot be
// checked and is never taken.)
bool Takes(const std::string& extension) {
    return !patching::IsRomImageExtension(extension) &&
           (patching::IsPatchExtension(extension) || IsArchiveExtension(extension));
}

std::vector<std::string> DefaultExtensions() {
    return {"bps", "ips", "ups", "xdelta", "vcdiff", "aps", "ppf", "zip", "7z"};
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

// The patch a zip or a 7z holds, unpacked beside it. An archive with a ROM image in it is refused
// whole: a patched game is not a patch.
Result<fs::path> PatchInZip(const fs::path& archive) {
    const auto members = ListArchive(archive);
    if (!members) {
        return Status::Fail("the archive cannot be read");
    }
    const ZipEntry* patch = nullptr;
    for (const ZipEntry& member : *members) {
        const std::string extension = ExtensionOf(PathFromUtf8(member.name));
        if (patching::IsRomImageExtension(extension)) {
            return Status::Fail("EmuDEN only installs patch files; this archive holds a full game file");
        }
        if (patch == nullptr && patching::IsPatchExtension(extension) && IsSafeRelativePath(member.name) &&
            CanExtractArchiveEntry(member) && !member.symlink && member.size <= MAX_PATCH &&
            member.block_size <= MAX_PATCH) {
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
    if (const Status unpacked = ExtractArchiveEntry(archive, *patch, out); !unpacked.ok) {
        return unpacked;
    }
    return out;
}

// A download of a visit to any site: the screen saved it in the Downloads folder, where it
// stays; the script gets its name there (for den.downloads).
Result<WebVisit::File> TakeAnyFile(Runner& runner, const WebFile& arrived) {
    const fs::path file = PathFromUtf8(arrived.file);
    const auto& services = runner.Config().services;
    const fs::path folder = services ? services->DownloadsFolder() : fs::path();
    if (folder.empty() || !IsRegularFile(file) || file.parent_path().lexically_normal() != folder.lexically_normal()) {
        return Status::Fail("the file is not in the Downloads folder");
    }
    return WebVisit::File{PathToUtf8(file.filename()), arrived.name, arrived.host, arrived.title};
}

// Checks a file the screen handed over and moves it into the plugin's inbox.
Result<WebVisit::File> TakeFile(Runner& runner, const WebFile& arrived) {
    if (runner.Web().any) {
        return TakeAnyFile(runner, arrived);
    }
    const fs::path file = PathFromUtf8(arrived.file);
    const std::string extension = ExtensionOf(PathFromUtf8(arrived.name.empty() ? arrived.file : arrived.name));
    const WebVisit& visit = runner.Web();
    Result<WebVisit::File> result = Status::Fail("");
    fs::path patch = file;
    fs::path unpacked_dir;
    if (patching::IsRomImageExtension(extension) || patching::IsRomImageExtension(ExtensionOf(file))) {
        result = Status::Fail("EmuDEN only installs patch files; this is a full game file");
    } else if (!Takes(extension) || !Has(visit.extensions, extension)) {
        result = Status::Fail("the plugin does not take this kind of file");
    } else {
        Status ready;
        if (IsArchiveExtension(extension)) {
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
            result = WebVisit::File{copied.take(), PathToUtf8(patch.filename()), arrived.host, arrived.title};
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

// A plugin of the developer folder may show a test site of this machine over plain http.
bool LocalTestSite(Runner& runner, const std::string& url, const std::vector<std::string>& hosts) {
    const UrlParts parts = ParseUrl(url);
    return runner.Config().developer && parts.valid && parts.scheme == "http" && parts.host == "127.0.0.1" &&
           Has(hosts, parts.host);
}

int PushFile(lua_State* L, const WebVisit::File& file) {
    lua_newtable(L);
    lua_pushstring(L, file.path.c_str());
    lua_setfield(L, -2, "path");
    lua_pushstring(L, file.name.c_str());
    lua_setfield(L, -2, "name");
    lua_pushstring(L, file.host.c_str());
    lua_setfield(L, -2, "source_host");
    lua_pushstring(L, file.title.c_str());
    lua_setfield(L, -2, "page_title");
    if (Runner::Of(L).Web().any) {
        lua_pushstring(L, file.path.c_str());
        lua_setfield(L, -2, "download");
    }
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
    const WebFile arrived{parsed->String("file"), parsed->String("name"), parsed->String("host"),
                          parsed->String("title")};
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

bool FieldFlag(lua_State* L, int table, const char* key) {
    lua_getfield(L, table, key);
    const bool value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

// den.web.open{any = true, url?, title?}: the browser for any site (browse.any). Every file the
// user downloads there is kept in the Downloads folder.
int OpenAny(lua_State* L, Runner& runner, WebVisit& visit) {
    if (!runner.Config().manifest.access.Has(PERMISSION_BROWSE_ANY)) {
        return Refuse(L, "the plugin has no permission to show any website");
    }
    const auto& services = runner.Config().services;
    if (!services || services->DownloadsFolder().empty()) {
        return Refuse(L, "this version of the app has no Downloads folder");
    }
    std::string url = FieldText(L, 1, "url");
    if (url.empty()) {
        url = HOME;
    }
    const UrlParts parts = ParseUrl(url);
    if (!parts.valid || parts.scheme != "https" || parts.host.empty()) {
        return Refuse(L, "the page must be an https address");
    }
    if (runner.CoroutineOf(L) == 0) {
        return runner.Park(L); // raises: not one of the plugin's own functions
    }
    Ask ask;
    ask.kind = AskKind::OpenWeb;
    ask.value = url;
    ask.title = CutText(FieldText(L, 1, "title"));
    ask.hosts = {"*"};
    ask.extensions = {"*"};
    ask.message = "browser";
    visit.extensions = {"*"};
    visit.ready.clear();
    visit.open = true;
    visit.any = true;
    runner.PostStream(std::move(ask), [&runner](bool accepted, const std::string& value) {
        OnAnswer(runner, accepted, value);
    });
    visit.waiting = runner.CoroutineOf(L);
    return runner.Park(L);
}

// den.web.open{url, hosts?, title?, extensions?}: shows the page and waits for the next file
// the user downloads there. Called again while the page is open, it waits for the next one.
int Open(lua_State* L) {
    Runner& runner = Runner::Of(L);
    const FeatureManifest& manifest = runner.Config().manifest;
    luaL_checktype(L, 1, LUA_TTABLE);
    WebVisit& visit = runner.Web();
    if (visit.waiting != 0) {
        return Refuse(L, "den.web.open is already waiting for a file");
    }
    if (!visit.open && FieldFlag(L, 1, "any")) {
        return OpenAny(L, runner, visit);
    }
    if (!visit.any && !manifest.access.Has(PERMISSION_BROWSE)) {
        return Refuse(L, "the plugin has no permission to show websites");
    }
    if (!visit.open) {
        visit.any = false;
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
        if (const Status allowed = CheckUrl(url, hosts); !allowed.ok && !LocalTestSite(runner, url, hosts)) {
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

// den.web.bookmarks(): {title, url} of the browser page's bookmarks (browse.any).
int Bookmarks(lua_State* L) {
    Runner& runner = Runner::Of(L);
    if (!runner.Config().manifest.access.Has(PERMISSION_BROWSE_ANY)) {
        return Refuse(L, "the plugin has no permission to show any website");
    }
    const auto text = ReadTextFile(runner.Config().bookmarks_file);
    const auto json = text ? ParseJson(*text) : std::nullopt;
    lua_newtable(L);
    int index = 0;
    if (json && json->kind == JsonValue::Kind::Array) {
        for (const JsonValue& item : json->items) {
            const std::string url = item.String("url");
            if (url.empty() || index >= 200) {
                continue;
            }
            lua_createtable(L, 0, 2);
            lua_pushstring(L, CutText(item.String("title")).c_str());
            lua_setfield(L, -2, "title");
            lua_pushstring(L, CutText(url).c_str());
            lua_setfield(L, -2, "url");
            lua_rawseti(L, -2, ++index);
        }
    }
    return 1;
}

} // namespace

void RegisterWeb(lua_State* L, Runner& runner) {
    lua_newtable(L);
    SetFunction(L, runner, "available", &Available);
    SetFunction(L, runner, "open", &Open);
    SetFunction(L, runner, "close", &Close);
    SetFunction(L, runner, "bookmarks", &Bookmarks);
    lua_setfield(L, -2, "web");
}

} // namespace den::plugins::feature
