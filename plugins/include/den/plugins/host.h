// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the plugin host: which consoles exist (from the plugin manifests), which core
// plays a system, and installing it from the signed plugin index.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"

namespace den::store {
class Settings;
}

namespace den::plugins {

/// How a console's games are run.
enum class Runner {
    Core,   // a libretro core inside the app (den::player)
    Engine, // an out-of-app engine started by den::engine (Switch 2)
};

/// What the console's own pad calls a RetroPad button ("b" is "Cross" on a PlayStation).
struct ButtonName {
    std::string pad;
    std::string label;
};

struct BiosFile {
    std::string path; // relative to the system folder; a trailing slash means a non-empty folder
    bool optional = false;
};

struct BiosSet {
    std::string label;
    bool any_one = false; // one of the files is enough
    std::vector<BiosFile> files;
    std::vector<std::string> cores; // the cores that need it (empty = every core)
};

/// Where a ROM keeps its own title.
struct HeaderTitle {
    std::uint64_t offset = 0;
    std::uint32_t length = 0;
};

/// How the library finds a console's games (the manifest's content object).
struct ScanRules {
    std::vector<std::string> compound_extensions;    // matched before simple ones ("p8.png")
    std::vector<std::string> folder_aliases;         // folder names besides the id
    std::vector<std::string> folder_only_extensions; // claimed only inside the console's folder
    std::string zip = "unpack";                      // unpack, pass or no
    /// Multi-file games: index files (cue, m3u) and the members they name (bin).
    std::vector<std::string> index_extensions;
    std::vector<std::string> member_extensions;
    bool loose_members = false; // a member without an index is a game
    std::string track_naming;   // "redump": "(Track N)" files belong together
    std::vector<HeaderTitle> header_title;
    bool cover_is_rom = false;
    std::string scanner; // "switch2": the engine scans, not the extensions
};

/// A console, from a plugin manifest's systems[].
struct Console {
    std::string id;         // system id: "gb", "gba", "nds", "psx", "switch2"
    std::string name;       // "Game Boy Advance"
    std::string short_name; // "GBA"
    std::string plugin_id;  // the plugin that provides it
    std::vector<std::string> extensions; // lower case, without the dot
    Runner runner = Runner::Core;
    /// A core console: its core is on disk and matches its checksum. Engine consoles are
    /// always true here; ask den::engine whether the engine can start.
    bool installed = false;
    bool update_available = false; // the index has a newer version than the one on disk
    /// False when no core of the console has a download for this device.
    bool available = true;
    std::string group;         // "nintendo", "sega", "sony", ...
    int order = 0;             // position on the home screen, ascending
    float cover_aspect = 1.0f; // width over height
    bool handheld = false;
    bool right_stick_holds = false;
    ScanRules scan;
    std::vector<ButtonName> buttons;
    std::string thumbnails; // the libretro thumbnail folder ("Nintendo - Game Boy"), or empty
    std::string rdb;        // the libretro database name, or empty
    int rcheevos_console = 0;
    std::vector<BiosSet> bios;
};

/// A setting a plugin manifest declares for a console ("settings"). The Consoles screen
/// builds a row from it and keeps the value as text in the app settings under
/// "console.<system>.<key>" ("true"/"false" for a toggle, the number for a range); whoever
/// starts the game applies it.
struct SettingSpec {
    std::string key;
    std::string label;
    std::string help;
    std::string type; // "toggle", "choice", "range", "text", "path"
    std::vector<std::pair<std::string, std::string>> choices; // value, label
    double min = 0, max = 0, step = 1;
    std::string suffix;
    std::string default_value;
};

/// The games a core is chosen for without asking: those whose ROM holds one of `codes` at
/// `offset` (a GBA game's code at 0xAC). Empty: none.
struct GameMatch {
    std::uint64_t offset = 0;
    std::uint32_t length = 0;
    std::vector<std::string> codes;
    bool Empty() const {
        return length == 0 || codes.empty();
    }
};

/// The core that plays a system on this device.
struct CoreInfo {
    std::string id;     // "mgba"
    std::string name;   // "mGBA"
    std::string system;
    std::filesystem::path library;    // the core file to load; empty until installed
    std::filesystem::path system_dir; // where the core looks for BIOS files
    bool installed = false;
    bool needs_gl = false; // the core renders with OpenGL itself
    /// Option values the manifest recommends, applied before the user's own.
    std::vector<std::pair<std::string, std::string>> option_hints;
    /// The core options that make the picture widescreen (empty = the core cannot).
    std::vector<std::pair<std::string, std::string>> widescreen_options;
    bool widescreen_restart = false;
    GameMatch games; // the games this core plays instead of the console's chosen core
    /// BIOS files the core needs and does not find (empty = ready).
    std::vector<std::string> missing_bios;
};

class Host {
public:
    virtual ~Host() = default;

    /// Every console the installed manifests and the index describe.
    virtual std::vector<Console> Consoles() const = 0;
    virtual std::optional<Console> FindConsole(const std::string& system) const = 0;
    /// The core for a system on this device; nullopt for engine consoles and unknown systems.
    virtual std::optional<CoreInfo> CoreFor(const std::string& system) const = 0;
    virtual bool Installed(const std::string& system) const = 0;
    /// Call right before a core library is loaded: hashes the file again and fails when it
    /// is not the file that was installed or when a source blocked its checksum.
    virtual Status VerifyCore(const std::filesystem::path& library) const = 0;
    /// Every core that can play a system on this device, the chosen one (CoreFor) included.
    virtual std::vector<CoreInfo> CoresFor(const std::string& system) const {
        const std::optional<CoreInfo> core = CoreFor(system);
        return core ? std::vector<CoreInfo>{*core} : std::vector<CoreInfo>{};
    }
    /// Makes `core_id` the core CoreFor returns for the system, and remembers it.
    virtual void SelectCore(const std::string& /*system*/, const std::string& /*core_id*/) {}
    /// The settings the console's manifest declares; empty when it has none.
    virtual std::vector<SettingSpec> SettingsFor(const std::string& /*system*/) const {
        return {};
    }

    /// Downloads, verifies (SHA-256 and size from the signed index) and unpacks what the
    /// system needs. Progress and cancel through the job; the failure text is user-facing.
    virtual JobPtr Install(const std::string& system) = 0;
    virtual JobPtr Remove(const std::string& system) = 0;
    /// Fetches the signed index of every source again.
    virtual JobPtr Refresh() = 0;
    /// Grows whenever consoles or their installed state changed.
    virtual std::uint64_t Revision() const = 0;
};

struct Config {
    std::filesystem::path data_dir; // plugins live under data_dir / "plugins", cores under "cores"
    Jobs* jobs = nullptr;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Host> CreateHost(const Config& config);

} // namespace den::plugins
