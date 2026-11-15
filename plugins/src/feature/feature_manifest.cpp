// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "den/plugins/feature_manifest.h"
#include "json_util.h"

namespace den::plugins {

namespace {

constexpr std::size_t MAX_HOSTS = 8;

const char* const PERMISSIONS[] = {
    PERMISSION_HTTP,         PERMISSION_DOWNLOAD,        PERMISSION_OPEN_URL,
    PERMISSION_BROWSE,       PERMISSION_FILES_IMPORT,    PERMISSION_DOWNLOADS_WATCH,
    PERMISSION_LIBRARY_READ, PERMISSION_PATCH,           PERMISSION_LIBRARY_WRITE,
    PERMISSION_SYNC,         PERMISSION_HTTP_USER_HOST,  PERMISSION_HTTP_LOCAL,
    PERMISSION_DEVICE_INFO,  PERMISSION_GAME_SETTINGS,   PERMISSION_STEAM,
    PERMISSION_ACHIEVEMENTS,
    PERMISSION_BROWSE_ANY,   PERMISSION_DOWNLOADS_SETUP,
};

const char* CategoryName(std::string_view category) {
    if (category == "saves") {
        return "saves";
    }
    if (category == "states") {
        return "save states";
    }
    if (category == "config") {
        return "settings";
    }
    if (category == "games") {
        return "games";
    }
    return "BIOS files";
}

bool Contains(const std::vector<std::string>& list, std::string_view item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

bool IsPlainId(std::string_view id, std::size_t max) {
    return !id.empty() && id.size() <= max && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}

bool IsFeatureId(std::string_view id) {
    if (id.size() < 3 || id.size() > 64 || id.find('.') == std::string_view::npos) {
        return false;
    }
    bool start = true;
    for (const char c : id) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (start ? !alnum : !(alnum || c == '-' || c == '.')) {
            return false;
        }
        start = c == '.';
    }
    return !start;
}

std::string Join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

// Reads a list of host names; fails on anything that is not one.
Status ReadHosts(const JsonValue* list, const char* what, std::vector<std::string>& out) {
    if (list == nullptr) {
        return {};
    }
    if (list->kind != JsonValue::Kind::Array || list->items.size() > MAX_HOSTS) {
        return Status::Fail(std::string(what) + " must name at most 8 hosts");
    }
    for (const JsonValue& item : list->items) {
        if (item.kind != JsonValue::Kind::String || !IsHostName(item.string)) {
            return Status::Fail(std::string(what) + " has a name that is not an exact host");
        }
        if (!Contains(out, item.string)) {
            out.push_back(item.string);
        }
    }
    return {};
}

std::vector<std::string> Beyond(const std::vector<std::string>& wanted,
                                const std::vector<std::string>& granted) {
    std::vector<std::string> out;
    for (const std::string& item : wanted) {
        if (!Contains(granted, item)) {
            out.push_back(item);
        }
    }
    return out;
}

} // namespace

bool FeatureAccess::Has(std::string_view permission) const {
    return Contains(permissions, permission);
}

bool FeatureAccess::SeesSystem(std::string_view system) const {
    return Contains(systems, "*") || Contains(systems, system);
}

bool IsHostName(std::string_view host) {
    if (host.size() < 3 || host.size() > 253 || host.find('.') == std::string_view::npos) {
        return false;
    }
    bool start = true;
    for (const char c : host) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (start ? !alnum : !(alnum || c == '-' || c == '.')) {
            return false;
        }
        start = c == '.';
    }
    return !start && host.back() != '-';
}

bool IsFeatureManifest(std::string_view text) {
    if (text.size() > MAX_MANIFEST_SIZE) {
        return false;
    }
    const std::optional<JsonValue> root = ParseJson(text);
    return root && root->kind == JsonValue::Kind::Object && IsPackageKind(root->String("kind"));
}

bool IsPackageKind(std::string_view kind) {
    return kind == KIND_FEATURE || kind == KIND_INTERFACE;
}

Result<FeatureManifest> ParseFeatureManifest(std::string_view text) {
    if (text.size() > MAX_MANIFEST_SIZE) {
        return Status::Fail("the manifest is too large");
    }
    const std::optional<JsonValue> root = ParseJson(text);
    if (!root || root->kind != JsonValue::Kind::Object) {
        return Status::Fail("the manifest is not JSON");
    }
    FeatureManifest manifest;
    manifest.id = root->String("id");
    manifest.name = root->String("name");
    manifest.publisher = root->String("publisher");
    manifest.summary = root->String("summary");
    manifest.license = root->String("license");
    manifest.api = static_cast<int>(json::Number(*root, "api", 0));
    if (json::Number(*root, "manifest_version", 1) != 1 || manifest.api > API_VERSION) {
        return Status::Fail("Needs a newer EmuDEN");
    }
    const auto version = Version::Parse(root->String("version"));
    if (!IsFeatureId(manifest.id) || manifest.name.empty() || !version || manifest.api < 1) {
        return Status::Fail("the manifest has no valid id, name, version or api");
    }
    manifest.version = *version;
    manifest.kind = root->String("kind");
    if (!IsPackageKind(manifest.kind)) {
        return Status::Fail(manifest.id + " is not a feature plugin");
    }
    const std::vector<std::string> needs = json::Strings(*root, "needs", false);
    if (!Contains(needs, "kind." + manifest.kind)) {
        return Status::Fail(manifest.id + " does not ask for kind." + manifest.kind);
    }
    const auto& capabilities = HostCapabilities();
    for (const std::string& need : needs) {
        if (!Contains(capabilities, need)) {
            return Status::Fail("Needs a newer EmuDEN (" + need + ")");
        }
    }
    if (manifest.kind == KIND_INTERFACE) {
        // Data only: what the app draws is the app's code, so there is nothing to grant.
        const JsonValue* look = root->Find("interface");
        if (look == nullptr || look->kind != JsonValue::Kind::Object) {
            return Status::Fail(manifest.id + " has no interface object");
        }
        if (root->Find("feature") != nullptr) {
            return Status::Fail("an interface plugin cannot ask for permissions");
        }
        manifest.icon = look->String("icon");
        if (!manifest.icon.empty() &&
            (!IsSafeRelativePath(manifest.icon) || !manifest.icon.ends_with(".png"))) {
            return Status::Fail("the icon is not a PNG of the package");
        }
        return manifest;
    }
    const JsonValue* feature = root->Find("feature");
    if (feature == nullptr || feature->kind != JsonValue::Kind::Object) {
        return Status::Fail(manifest.id + " has no feature object");
    }
    manifest.entry = feature->String("entry");
    if (!IsSafeRelativePath(manifest.entry) || !manifest.entry.ends_with(".lua")) {
        return Status::Fail("the entry is not a script of the package");
    }
    manifest.icon = feature->String("icon");
    if (!manifest.icon.empty() &&
        (!IsSafeRelativePath(manifest.icon) || !manifest.icon.ends_with(".png"))) {
        return Status::Fail("the icon is not a PNG of the package");
    }

    FeatureAccess& access = manifest.access;
    const JsonValue* permissions = feature->Find("permissions");
    if (permissions == nullptr || permissions->kind != JsonValue::Kind::Array) {
        return Status::Fail(manifest.id + " does not list its permissions");
    }
    for (const JsonValue& item : permissions->items) {
        const bool known =
            item.kind == JsonValue::Kind::String &&
            std::any_of(std::begin(PERMISSIONS), std::end(PERMISSIONS),
                        [&](const char* name) { return item.string == name; });
        if (!known) {
            return Status::Fail("the permission \"" + item.string + "\" is not known");
        }
        if (!access.Has(item.string)) {
            access.permissions.push_back(item.string);
        }
    }
    const JsonValue* http = feature->Find("http");
    if (Status hosts = ReadHosts(http ? http->Find("hosts") : nullptr, "http.hosts",
                                 access.http_hosts);
        !hosts.ok) {
        return hosts;
    }
    if (Status hosts = ReadHosts(feature->Find("open_hosts"), "open_hosts", access.open_hosts);
        !hosts.ok) {
        return hosts;
    }
    const bool network = access.Has(PERMISSION_HTTP) || access.Has(PERMISSION_DOWNLOAD);
    if (network && access.http_hosts.empty()) {
        return Status::Fail("http and download need http.hosts");
    }
    if (!network) {
        access.http_hosts.clear(); // hosts without the permission grant nothing
    }
    if (access.Has(PERMISSION_OPEN_URL) && access.open_hosts.empty()) {
        return Status::Fail("open-url needs open_hosts");
    }
    if (!access.Has(PERMISSION_OPEN_URL)) {
        access.open_hosts.clear();
    }
    if (Status hosts = ReadHosts(feature->Find("browse_hosts"), "browse_hosts", access.browse_hosts);
        !hosts.ok) {
        return hosts;
    }
    if (access.Has(PERMISSION_BROWSE) && access.browse_hosts.empty()) {
        return Status::Fail("browse needs browse_hosts");
    }
    if (!access.Has(PERMISSION_BROWSE)) {
        access.browse_hosts.clear();
    }
    if (http != nullptr) {
        // A sync provider talks to its own storage API, many small requests per game: it may
        // ask for a shorter pause than a plugin that reads public sites.
        const int least = access.Has(PERMISSION_SYNC) ? 100 : 500;
        manifest.min_interval_ms = std::clamp(
            static_cast<int>(json::Number(*http, "min_interval_ms", 500)), least, 600'000);
    }

    for (const std::string& system : json::Strings(*feature, "systems", false)) {
        if (system != "*" && !IsPlainId(system, 32)) {
            return Status::Fail("systems has a name that is not a system id");
        }
        if (!Contains(access.systems, system)) {
            access.systems.push_back(system);
        }
    }
    const bool library = access.Has(PERMISSION_LIBRARY_READ) || access.Has(PERMISSION_PATCH) ||
                         access.Has(PERMISSION_LIBRARY_WRITE) || access.Has(PERMISSION_GAME_SETTINGS);
    if (library && access.systems.empty()) {
        return Status::Fail("the library permissions need systems");
    }
    if (!library) {
        access.systems.clear();
    }

    for (const std::string& type : json::Strings(*feature, "file_types", true)) {
        if (!IsPlainId(type, 12) || type.find('-') != std::string::npos) {
            return Status::Fail("file_types has a name that is not an extension");
        }
        manifest.file_types.push_back(type);
    }
    const JsonValue* sync = feature->Find("sync");
    if (access.Has(PERMISSION_SYNC)) {
        const JsonValue* categories = sync ? sync->Find("categories") : nullptr;
        if (categories == nullptr || categories->kind != JsonValue::Kind::Array ||
            categories->items.empty()) {
            return Status::Fail("sync needs feature.sync.categories");
        }
        for (const JsonValue& item : categories->items) {
            const bool known =
                item.kind == JsonValue::Kind::String &&
                std::any_of(std::begin(SYNC_CATEGORIES), std::end(SYNC_CATEGORIES),
                            [&](const char* name) { return item.string == name; });
            if (!known) {
                return Status::Fail("feature.sync.categories has an unknown category");
            }
            if (Contains(manifest.sync_categories, item.string)) {
                return Status::Fail("feature.sync.categories names a category twice");
            }
            manifest.sync_categories.push_back(item.string);
        }
    } else if (sync != nullptr) {
        return Status::Fail("feature.sync needs the sync permission");
    }
    if (access.Has(PERMISSION_DOWNLOADS_WATCH) && manifest.file_types.empty()) {
        return Status::Fail("downloads.watch needs file_types");
    }
    if (const JsonValue* limits = feature->Find("limits")) {
        const int memory = static_cast<int>(json::Number(*limits, "memory_mb", 32));
        const int storage = static_cast<int>(json::Number(*limits, "storage_mb", 512));
        if (memory < 8 || memory > 64 || storage < 1 || storage > 4096) {
            return Status::Fail("the limits are outside what a plugin may ask for");
        }
        manifest.memory_mb = memory;
        manifest.storage_mb = storage;
    }
    return manifest;
}

Status CheckTrust(const FeatureManifest& manifest, FeaturePlugin::Trust trust,
                  std::string_view source_owner) {
    if (trust == FeaturePlugin::Trust::Official) {
        if (!manifest.id.starts_with("emuden.")) {
            return Status::Fail("An official plugin's id starts with emuden.");
        }
        return {};
    }
    if (Contains(manifest.access.systems, "*")) {
        return Status::Fail("Only an official plugin may see every console.");
    }
    if (trust == FeaturePlugin::Trust::ThirdParty) {
        const std::string prefix = ToLower(source_owner) + ".";
        if (prefix.size() < 2 || !manifest.id.starts_with(prefix) ||
            manifest.id.starts_with("emuden.")) {
            return Status::Fail("The plugin's id does not belong to its source.");
        }
        if (manifest.access.Has(PERMISSION_DOWNLOADS_WATCH)) {
            return Status::Fail("A plugin of this source may not watch the Downloads folder.");
        }
        if (manifest.access.Has(PERMISSION_BROWSE_ANY) || manifest.access.Has(PERMISSION_DOWNLOADS_SETUP)) {
            return Status::Fail("Only an official plugin may show any website or set up downloads.");
        }
    }
    return {};
}

std::vector<PermissionLine> PermissionLines(const FeatureAccess& access,
                                            const std::vector<std::string>& sync_categories) {
    std::vector<PermissionLine> lines;
    for (const char* permission : PERMISSIONS) {
        if (!access.Has(permission)) {
            continue;
        }
        const std::string_view name = permission;
        std::string text;
        if (name == PERMISSION_HTTP) {
            text = "Connect to: " + Join(access.http_hosts);
        } else if (name == PERMISSION_DOWNLOAD) {
            text = "Download files from: " + Join(access.http_hosts);
        } else if (name == PERMISSION_OPEN_URL) {
            text = "Open pages of " + Join(access.open_hosts) + " in your browser";
        } else if (name == PERMISSION_BROWSE) {
            text = "Shows these websites inside EmuDEN: " + Join(access.browse_hosts);
        } else if (name == PERMISSION_FILES_IMPORT) {
            text = "Read files you choose";
        } else if (name == PERMISSION_DOWNLOADS_WATCH) {
            text = "See patch files in your Downloads folder";
        } else if (name == PERMISSION_LIBRARY_READ) {
            text = Contains(access.systems, "*")
                       ? "See which games you have"
                       : "See which games of these consoles you have: " + Join(access.systems);
        } else if (name == PERMISSION_PATCH) {
            text = "Create patched copies of your games";
        } else if (name == PERMISSION_SYNC) {
            std::vector<std::string> names;
            for (const std::string& category : sync_categories) {
                names.push_back(CategoryName(category));
            }
            text = names.empty() ? "Keep your files in step between your devices"
                                 : "Keep these in step between your devices: " + Join(names);
        } else if (name == PERMISSION_HTTP_USER_HOST) {
            text = "Connect to the server you enter";
        } else if (name == PERMISSION_HTTP_LOCAL) {
            text = "Connect to apps on this device";
        } else if (name == PERMISSION_DEVICE_INFO) {
            text = "See this device's model, processor, graphics and memory";
        } else if (name == PERMISSION_GAME_SETTINGS) {
            text = Contains(access.systems, "*")
                       ? "Change the settings of single games"
                       : "Change the settings of single games of these consoles: " + Join(access.systems);
        } else if (name == PERMISSION_BROWSE_ANY) {
            text = "Show any website inside EmuDEN and keep what you download there in EmuDEN's Downloads folder";
        } else if (name == PERMISSION_DOWNLOADS_SETUP) {
            text = "Add the games and patches you download to your library";
        } else if (name == PERMISSION_STEAM) {
            text = "See your Steam games and change their launch options and Proton version";
        } else if (name == PERMISSION_ACHIEVEMENTS) {
            text = "Sign in to RetroAchievements and track achievements while you play";
        } else {
            text = "Change the hacks it installed";
        }
        lines.push_back({permission, std::move(text)});
    }
    return lines;
}

FeatureAccess AccessBeyond(const FeatureAccess& wanted, const FeatureAccess& granted) {
    FeatureAccess out;
    out.permissions = Beyond(wanted.permissions, granted.permissions);
    out.http_hosts = Beyond(wanted.http_hosts, granted.http_hosts);
    out.open_hosts = Beyond(wanted.open_hosts, granted.open_hosts);
    out.browse_hosts = Beyond(wanted.browse_hosts, granted.browse_hosts);
    if (!Contains(granted.systems, "*")) {
        out.systems = Beyond(wanted.systems, granted.systems);
    }
    // New hosts or systems are shown under the permission they widen.
    const auto widen = [&](bool changed, std::initializer_list<const char*> names) {
        for (const char* name : names) {
            if (changed && wanted.Has(name) && !out.Has(name)) {
                out.permissions.push_back(name);
            }
        }
    };
    widen(!out.http_hosts.empty(), {PERMISSION_HTTP, PERMISSION_DOWNLOAD});
    widen(!out.open_hosts.empty(), {PERMISSION_OPEN_URL});
    widen(!out.browse_hosts.empty(), {PERMISSION_BROWSE});
    widen(!out.systems.empty(), {PERMISSION_LIBRARY_READ});
    return out;
}

bool Empty(const FeatureAccess& access) {
    return access.permissions.empty() && access.http_hosts.empty() &&
           access.open_hosts.empty() && access.browse_hosts.empty() && access.systems.empty();
}

JsonValue AccessToJson(const FeatureAccess& access) {
    return JsonObject({
        {"permissions", json::StringArray(access.permissions)},
        {"http_hosts", json::StringArray(access.http_hosts)},
        {"open_hosts", json::StringArray(access.open_hosts)},
        {"browse_hosts", json::StringArray(access.browse_hosts)},
        {"systems", json::StringArray(access.systems)},
    });
}

FeatureAccess AccessFromJson(const JsonValue& value) {
    FeatureAccess access;
    access.permissions = json::Strings(value, "permissions", false);
    access.http_hosts = json::Strings(value, "http_hosts", false);
    access.open_hosts = json::Strings(value, "open_hosts", false);
    access.browse_hosts = json::Strings(value, "browse_hosts", false);
    access.systems = json::Strings(value, "systems", false);
    return access;
}

} // namespace den::plugins
