// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// plugin.json: a plugin is data. This reads the parts of a console plugin the app uses.

#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "den/base/result.h"
#include "den/plugins/host.h"
#include "den/plugins/translations.h"

namespace den::plugins {

inline constexpr int API_VERSION = 1;
inline constexpr std::size_t MAX_MANIFEST_SIZE = 1u << 20;

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    auto operator<=>(const Version&) const = default;
    std::string Text() const;
    /// "MAJOR.MINOR.PATCH", nothing else.
    static std::optional<Version> Parse(std::string_view text);
};

/// "*", or space-separated comparators (=, >, >=, <, <=) that must all hold.
bool VersionInRange(const Version& version, std::string_view range);

/// One pinned file of a component, for one platform key.
struct Download {
    std::string url; // https only
    std::string sha256;
    std::uint64_t size = 0;
    std::string archive; // "zip" or "none"
    std::string entry;   // the member to unpack from a zip; "Name/" is a whole folder of it
    /// Developer folder only: a file on this device in place of `url` (checksum and size
    /// optional), so nothing has to be hosted while a plugin is made.
    std::string file;
};

/// Component types this app reads; others are left out of the manifest.
inline constexpr const char* TYPE_CORE = "libretro-core";
inline constexpr const char* TYPE_ENGINE = "engine";
/// A standalone emulator app that speaks the Emulator Control Protocol: one of a console's
/// cores like a libretro core, run by den::engine instead of inside the app.
inline constexpr const char* TYPE_ENGINE_APP = "engine-app";
/// Files a core needs in the system folder (PPSSPP's assets): a zip whose folder `entry`
/// is unpacked there when the core is installed.
inline constexpr const char* TYPE_SYSTEM_FILES = "system-files";

/// The parts of an engine-app component beyond its desktop downloads.
struct EngineAppSpec {
    // The protocol descriptor (section 2 of the spec).
    std::string ecp_id; // the engine's own stable id
    int level = 1;      // 1 or 2
    std::vector<std::string> capabilities;
    std::vector<std::string> needs; // bios, keys, firmware: what the player provides in the engine
    /// Desktop: the executable inside the unpacked download, by platform key.
    std::map<std::string, std::string, std::less<>> executables;
    // Android.
    std::string package;
    std::string cert_sha256; // lower-case hex, no colons
    std::int64_t min_version_code = 0;
    std::optional<Download> apk; // archive "none"
    std::string store_url;       // https:// or market://, when there is no APK to pin
};

struct Component {
    std::string id; // for a libretro core its library base name ("mgba")
    std::string type;
    std::string name;
    std::string version;
    bool opengl = false;
    std::vector<std::pair<std::string, std::string>> forced_core_options;
    /// The core options that make the picture widescreen; off is each option's default.
    std::vector<std::pair<std::string, std::string>> widescreen_options;
    bool widescreen_restart = false; // they take effect when the game starts
    GameMatch games;
    std::map<std::string, Download, std::less<>> downloads; // by platform key or "any"
    std::vector<std::string> cores; // system files: the cores they are for (empty = all)
    EngineAppSpec engine_app;       // type engine-app; type engine: its executables, its Android app
    /// "language": the core option that sets the language of a game, and the option value of
    /// each app language code ("es" -> "Spanish"; "*" for any other). The app gives it the
    /// app's language before the manifest's other hints and the user's own options.
    std::string language_option;
    std::map<std::string, std::string, std::less<>> language_values;
    /// option_hints: values applied before forced_core_options and the user's own (a layout
    /// the player's presentation.screens know).
    std::vector<std::pair<std::string, std::string>> option_hints;
    std::vector<ScreenShape> screens; // presentation.screens; empty: the system's
    NetplaySpec netplay;
    std::string options_name;
    JsonValue tuning; // an object, or null

    const Download* DownloadFor(std::string_view platform_key) const;
    /// The option value for an app language ("es-MX": "es-MX", else "es", else "*"); empty
    /// when the core has no language option or no value for it.
    std::string LanguageValue(std::string_view app_language) const;
    /// It can be installed on the platform: a download (an engine app also needs its
    /// executable there), or on Android a package with an APK or a store page.
    bool OffersPlatform(std::string_view platform_key) const;
    /// The engine app's executable for a desktop platform; empty when none.
    std::string ExecutableFor(std::string_view platform_key) const;
};

/// Android platform keys ("android-arm64").
bool IsAndroidKey(std::string_view platform_key);

struct SystemDef {
    Console console;                // installed and available are filled by the host
    std::vector<std::string> cores; // component ids, best first
    std::vector<std::string> platforms; // platform keys it is offered on; empty: all
    std::string default_core; // the core chosen until the user picks one; empty: the first
};

struct ManifestSetting {
    SettingSpec spec;
    std::vector<std::string> systems;   // empty: every system of the plugin
    std::vector<std::string> platforms; // empty: every platform
    std::string when_core;              // shown only while this core plays the console
    /// apply.core_option: the core option the value is given to, through `map` when the
    /// value is one of its keys. Empty: the app does not apply this setting itself.
    std::string core_option;
    std::vector<std::pair<std::string, std::string>> option_map;

    bool Applies(std::string_view system, std::string_view platform_key, std::string_view core_id) const;
    /// The core option value for the setting's stored value (`value`).
    std::string OptionValue(const std::string& value) const;
};

/// depends[]: another console plugin that must be there, in a version range.
struct Dependency {
    std::string id;
    std::string range = "*";
};

/// The platform keys a manifest lists: empty or "any" is every platform.
bool ListsPlatform(const std::vector<std::string>& platforms, std::string_view platform_key);

struct Manifest {
    std::string id;
    std::string name;
    Version version;
    std::string kind;
    std::string engine; // libretro, switch2, external, pcstore
    std::string publisher;
    std::string summary;
    int api = 0;
    std::vector<std::string> needs;
    std::vector<SystemDef> systems;
    std::vector<Component> components;
    std::vector<ManifestSetting> settings; // those with a label of their own
    std::vector<std::string> platforms;    // empty: every platform
    std::vector<Dependency> depends;
    Translations translations; // its own texts in other languages

    const Component* FindComponent(std::string_view component_id) const;
    /// The cores of the system with a download for the platform, best first.
    std::vector<const Component*> CoresFor(const SystemDef& system,
                                           std::string_view platform_key) const;
    /// The built-in engine (type engine) the plugin offers as a download on the platform (an
    /// app on Android), or null: the player brings it.
    const Component* EngineDownloadFor(std::string_view platform_key) const;
    /// The system files that belong to a core and have a download for the platform.
    std::vector<const Component*> SystemFilesFor(std::string_view core_id,
                                                 std::string_view platform_key) const;
};

/// Reads plugin.json. Fails with the reason for anything this app cannot use: a bad id or
/// version, a newer format, an unmet need, a download that is not pinned. `developer`: a
/// manifest of the developer folder, whose downloads may name a local file.
Result<Manifest> ParseManifest(std::string_view json, bool developer = false);

/// The capability strings a manifest's `needs` may name.
const std::vector<std::string>& HostCapabilities();

/// A relative path with forward slashes, no "..", no drive, no leading slash.
bool IsSafeRelativePath(std::string_view path);
bool IsSha256Hex(std::string_view text);

/// A plugin manifest compiled into the app (assets/plugins/<id>/plugin.json).
struct BundledManifest {
    const char* id;
    const unsigned char* data;
    std::size_t size;
};
const BundledManifest* BundledManifests(std::size_t& count);

} // namespace den::plugins
