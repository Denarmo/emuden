// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// plugin.json of a feature plugin (kind "feature"), its permissions and what the user granted.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "den/base/json.h"
#include "den/base/result.h"
#include "den/plugins/feature_host.h"
#include "den/plugins/manifest.h"

namespace den::plugins {

inline constexpr const char* PERMISSION_HTTP = "http";
inline constexpr const char* PERMISSION_DOWNLOAD = "download";
inline constexpr const char* PERMISSION_OPEN_URL = "open-url";
inline constexpr const char* PERMISSION_BROWSE = "browse";
inline constexpr const char* PERMISSION_FILES_IMPORT = "files.import";
inline constexpr const char* PERMISSION_DOWNLOADS_WATCH = "downloads.watch";
inline constexpr const char* PERMISSION_LIBRARY_READ = "library.read";
inline constexpr const char* PERMISSION_PATCH = "patch";
inline constexpr const char* PERMISSION_LIBRARY_WRITE = "library.write";
/// A sync provider (docs/sync.md): den.sync, plugin.provider, feature.sync.categories.
inline constexpr const char* PERMISSION_SYNC = "sync";
/// den.http.request to the one server the user entered (den.sync.set_server).
inline constexpr const char* PERMISSION_HTTP_USER_HOST = "http.user_host";
/// den.http.request to 127.0.0.1 and localhost on any port, http or https.
inline constexpr const char* PERMISSION_HTTP_LOCAL = "http.local";
/// den.device.info(): the device's model, processor, graphics and memory.
inline constexpr const char* PERMISSION_DEVICE_INFO = "device.info";
/// den.game.settings and den.game.set: the own core options of games of `systems`.
inline constexpr const char* PERMISSION_GAME_SETTINGS = "game.settings";
/// den.steam: the Steam games of this computer, their launch options and Proton version.
inline constexpr const char* PERMISSION_STEAM = "steam";
/// den.mods: files added to and replaced in the folders of Steam games, and Vulkan layers,
/// each after the app's own question (official plugins only). den.download takes 512 MiB.
inline constexpr const char* PERMISSION_GAME_FILES = "game.files";
/// den.achievements: sign in to RetroAchievements and the achievements of the user's games.
inline constexpr const char* PERMISSION_ACHIEVEMENTS = "achievements";
/// den.web.open{any = true}: any https site in the in-app browser, every file downloaded there
/// kept in EmuDEN's Downloads folder (official plugins only).
inline constexpr const char* PERMISSION_BROWSE_ANY = "browse.any";
/// den.downloads: sets up the files of the Downloads folder as games and hacks (official only).
inline constexpr const char* PERMISSION_DOWNLOADS_SETUP = "downloads.setup";

/// The categories a sync provider may offer, in the order the sync page shows them.
inline constexpr const char* SYNC_CATEGORIES[] = {"saves", "states", "config", "games", "bios"};

/// feature.sync.listing: how a provider's list(prefix) behaves. "category" (default): the
/// engine lists each category. "all": any listing returns everything, so a run lists once.
/// "per_folder": as "all", and a system folder ("saves/gba/") is listed quickly, so syncing
/// one game lists only its folders.
inline constexpr const char* SYNC_LISTING_CATEGORY = "category";
inline constexpr const char* SYNC_LISTING_ALL = "all";
inline constexpr const char* SYNC_LISTING_PER_FOLDER = "per_folder";

/// The capability `needs` must list for a permission beyond feature.lua.1 (browse needs
/// feature.web.1); nullptr when none.
const char* PermissionCapability(std::string_view permission);
/// Only EmuDEN's own plugins may hold it (CheckTrust).
bool OfficialOnlyPermission(std::string_view permission);
/// Every permission a manifest may name.
std::vector<std::string> KnownPermissions();

/// What a plugin may reach: the part of a manifest (or of an index entry) the user agrees to.
struct FeatureAccess {
    std::vector<std::string> permissions;
    std::vector<std::string> http_hosts;
    std::vector<std::string> open_hosts;
    std::vector<std::string> browse_hosts; // shown in the in-app browser (den.web.open)
    std::vector<std::string> systems; // system ids, or "*"

    bool Has(std::string_view permission) const;
    bool SeesSystem(std::string_view system) const;
};

/// KIND_FEATURE or KIND_INTERFACE (feature_host.h).
bool IsPackageKind(std::string_view kind);

struct FeatureManifest {
    std::string id, name, publisher, summary, license;
    std::string kind = KIND_FEATURE;
    Version version;
    int api = 0;
    std::string entry; // the script to run, relative to the package; empty for an interface
    std::string icon;
    std::string language, language_name; // a language pack: "es", "Español"
    std::vector<std::string> needs;
    FeatureAccess access;
    std::vector<std::string> file_types; // lower case, without the dot
    std::vector<std::string> sync_categories; // feature.sync.categories, with PERMISSION_SYNC
    std::string sync_listing = SYNC_LISTING_CATEGORY; // feature.sync.listing
    int min_interval_ms = 500;
    int memory_mb = 32;
    int storage_mb = 512;
};

/// Reads plugin.json of kind "feature" or "interface". An interface has no script and may ask
/// for no permission. Fails with the reason for anything refused: an
/// unknown permission, a host with a wildcard, a permission without its hosts or systems.
Result<FeatureManifest> ParseFeatureManifest(std::string_view json);

/// Whether the text is a manifest of a package kind at all (console manifests are not ours).
bool IsFeatureManifest(std::string_view json);

/// The rules of a plugin's source: only the official one may see every system or hold an
/// OfficialOnlyPermission; a third-party plugin's id starts with its source owner's name.
Status CheckTrust(const FeatureManifest& manifest, FeaturePlugin::Trust trust,
                  std::string_view source_owner);

/// One sentence per permission, as the install page shows them.
std::vector<PermissionLine> PermissionLines(const FeatureAccess& access,
                                            const std::vector<std::string>& sync_categories = {});

/// What `wanted` asks for beyond `granted`: new permissions, hosts or systems.
FeatureAccess AccessBeyond(const FeatureAccess& wanted, const FeatureAccess& granted);
bool Empty(const FeatureAccess& access);

/// grant.json.
JsonValue AccessToJson(const FeatureAccess& access);
FeatureAccess AccessFromJson(const JsonValue& value);

/// A host name as manifests may declare it: lower case, dotted, no wildcard, no port.
bool IsHostName(std::string_view host);

} // namespace den::plugins
