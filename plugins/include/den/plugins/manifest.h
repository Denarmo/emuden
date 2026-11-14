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
};

/// Component types this app reads; others are left out of the manifest.
inline constexpr const char* TYPE_CORE = "libretro-core";
inline constexpr const char* TYPE_ENGINE = "engine";
/// Files a core needs in the system folder (PPSSPP's assets): a zip whose folder `entry`
/// is unpacked there when the core is installed.
inline constexpr const char* TYPE_SYSTEM_FILES = "system-files";

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

    const Download* DownloadFor(std::string_view platform_key) const;
};

struct SystemDef {
    Console console;                // installed and available are filled by the host
    std::vector<std::string> cores; // component ids, best first
};

struct ManifestSetting {
    SettingSpec spec;
    std::vector<std::string> systems; // empty: every system of the plugin
};

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

    const Component* FindComponent(std::string_view component_id) const;
    /// The cores of the system with a download for the platform, best first.
    std::vector<const Component*> CoresFor(const SystemDef& system,
                                           std::string_view platform_key) const;
    /// The system files that belong to a core and have a download for the platform.
    std::vector<const Component*> SystemFilesFor(std::string_view core_id,
                                                 std::string_view platform_key) const;
};

/// Reads plugin.json. Fails with the reason for anything this app cannot use: a bad id or
/// version, a newer format, an unmet need, a download that is not pinned.
Result<Manifest> ParseManifest(std::string_view json);

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
