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
#include "den/base/json.h"
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

/// One screen of a console inside the core's picture (presentation.screens): its part of the
/// picture, 0..1 with row 0 at the top, and whether presses on it reach the game.
struct ScreenPart {
    float x = 0, y = 0, w = 1, h = 1;
    bool touch = false;
};

/// The screens a core's picture holds when its width over height is that of `frame_width`
/// over `frame_height` (the DS's 256 x 384: the upper screen over the touch screen).
struct ScreenShape {
    int frame_width = 0, frame_height = 0;
    std::vector<ScreenPart> screens;
};

/// What a game of the console looks like on the standard Home's shelf and boot screen
/// (presentation.media). An empty shape: the app's own table, then a plain cartridge.
struct MediaShape {
    std::string shape; // cartridge, card, case, jewel
    std::string style; // the mould (game_moulds.cpp: "pocket", "snes", "hucard"); empty: plain
    float aspect = 0;  // width over height; 0: the shape's own
    bool disc = false;
    bool umd = false;
};

/// A console, from a plugin manifest's systems[].
struct Console {
    std::string id;         // system id: "gb", "gba", "nds", "psx", "switch2"
    std::string name;       // "Game Boy Advance"
    std::string short_name; // "GBA"
    std::string plugin_id;  // the plugin that provides it
    std::string source;     // the added source the plugin came from ("jdoe/consoles"); empty: EmuDEN's own
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
    std::string launchbox;  // the LaunchBox Games Database platform, or empty
    std::vector<std::string> cover_by_id; // cover addresses with "{id}" (library::SystemDef)
    int rcheevos_console = 0; // 0 also when the manifest's capabilities say no achievements
    bool netplay = true;      // capabilities.netplay: its games may be played together
    bool save_states = true;  // capabilities.save_states (the resume state too)
    bool rewind = true;       // capabilities.rewind
    bool fast_forward = true; // capabilities.fast_forward
    bool cheats = true;       // capabilities.cheats
    std::vector<BiosSet> bios;
    bool developer = false;   // its plugin is in the developer folder, not signed
    /// presentation.screens: the screens of its cores' pictures (a core may have its own).
    std::vector<ScreenShape> screens;
    /// presentation.layout: "nintendo" (A on the right, B below it), "other", or empty when
    /// the manifest does not say (the player then has its own list).
    std::string pad_layout;
    /// presentation.digital_only: the console has no stick, so the left stick is the D-pad too.
    std::optional<bool> digital_only;
    MediaShape media;
    /// content.sync: its games' saves and play time are synced (false for PC games, whose
    /// stores keep their own).
    bool sync = true;
    /// The manifest's sync: which of them sync when the games do (sync is true).
    bool sync_saves = true;
    bool sync_states = true;
    bool sync_settings = true;
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
    std::string section; // the heading it is shown under; empty: "Settings"
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

/// How a core plays a game with another device (a component's netplay).
struct NetplaySpec {
    std::string mode;       // "link" (each device its own game, linked) or "lockstep"; empty: none given
    unsigned subsystem = 0; // non-zero: the game is loaded twice through this libretro subsystem
    std::string description; // what playing together with it does, for the Together page
    /// What each side loads differently: the memory its own battery save is kept in (0: the
    /// usual save RAM; with a subsystem, the other side's is the peer's) and option values
    /// for the session only.
    struct Side {
        unsigned save_memory = 0;
        std::vector<std::pair<std::string, std::string>> options;
    };
    Side host, guest;
};

/// An engine app that is one of a console's cores (Runner::Engine on CoreInfo).
struct EngineAppInfo {
    int level = 1;
    std::vector<std::string> capabilities; // from the manifest; the engine's hello decides
    std::vector<std::string> needs;        // bios, keys, firmware: set up inside the engine
    std::filesystem::path executable;      // desktop, once installed
    std::string package;                   // Android
    std::string cert_sha256;               // Android: the pinned signing certificate
    std::string store_url;                 // Android: where to get it when there is no APK
    bool downloadable = false;             // EmuDEN can download and install it itself
};

/// The core that plays a system on this device.
struct CoreInfo {
    std::string id;     // "mgba"
    std::string name;   // "mGBA"
    std::string system;
    /// Core: a libretro core loaded into the app. Engine: an engine app (`engine`).
    Runner runner = Runner::Core;
    EngineAppInfo engine;
    std::filesystem::path library;    // the core file to load (an engine app's executable); empty until installed
    std::filesystem::path system_dir; // where the core looks for BIOS files
    bool installed = false;
    bool needs_gl = false; // the core renders with OpenGL itself
    /// Option values the manifest recommends, applied before the user's own.
    std::vector<std::pair<std::string, std::string>> option_hints;
    /// The core options that make the picture widescreen (empty = the core cannot).
    std::vector<std::pair<std::string, std::string>> widescreen_options;
    bool widescreen_restart = false;
    GameMatch games; // the games this core plays instead of the console's chosen core
    /// The screens of its picture: the core's own presentation.screens, else the console's.
    std::vector<ScreenShape> screens;
    NetplaySpec netplay;
    /// The name the core reports, whose letters and digits key its stored option values
    /// ("dolphin-emu"); the component id when the manifest gives none.
    std::string options_name;
    /// tuning: core option values by device (den/system/device.h ParseTuning reads it).
    JsonValue tuning;
    /// BIOS files the core needs and does not find (empty = ready).
    std::vector<std::string> missing_bios;

    /// It is installed and can be started (an Android engine app has no library file).
    bool Ready() const {
        return installed && (runner == Runner::Engine || !library.empty());
    }
};

/// A plugin source, as the Sources page shows it.
struct SourceInfo {
    std::string address; // "github:jdoe/consoles@main", "https://x.org/emu/"
    std::string label;   // "jdoe/consoles"
    std::string fingerprint; // of the pinned key, "3fa9 12c4 77e0 b5d1"
    bool official = false;   // built in; cannot be removed or switched off
    bool enabled = true;
    std::int64_t checked = 0; // last good check, Unix seconds; 0 = never
    std::string error;        // what the last check found wrong, user-facing
    std::size_t offered = 0;  // plugins it offers that are used
    std::size_t installed = 0; // plugins on this device that came from it
    std::vector<std::string> notes; // plugins of it that are refused or not used, and why
};

/// A source checked before the user agrees to add it.
struct SourceProbe {
    std::string address, label;
    std::string ns;  // its plugins' ids start with ns + "."
    std::string key; // the key it publishes (keys/source.pub), base64
    std::string fingerprint;
    bool pinned = false; // the user gave the fingerprint and it matches: nothing to ask
    std::string name;    // what its plugin list calls itself
    std::size_t plugins = 0;
};

/// A console plugin read from a file (an .emuplugin package, or a folder or file holding
/// plugin.json) before the user agrees to install it.
struct PackageProbe {
    std::filesystem::path path;
    std::string id, name, version, publisher, summary;
    std::vector<std::string> systems;  // its consoles' ids
    std::vector<std::string> consoles; // and names
    /// Signed with EmuDEN's key (plugin.json.sig): installed as one of EmuDEN's own plugins.
    /// Otherwise it is a third party's, unchecked by anyone, and held to their rules.
    bool official = false;
    bool engine = false;     // its games run in an engine program the user may have to point at
    std::string replaces;    // the version of it on this device, if any
    std::string manifest;    // the plugin.json read
    std::string signature;   // and its plugin.json.sig, if any
};

/// A console plugin an earlier EmuDEN had built in or offered that this one does not bring: it
/// stays where it was installed (a copy is on the device) or the library has games of it.
struct CarryOver {
    std::string plugin_id;
    std::string name;
    std::vector<std::string> systems;
    bool installed = false; // its manifest is on the device
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
    /// As Install, with `core_id` (one of the system's cores) in place of the chosen core,
    /// which stays chosen: a core needed for one purpose, such as playing together.
    virtual JobPtr InstallCore(const std::string& system, const std::string& /*core_id*/) {
        return Install(system);
    }
    virtual JobPtr Remove(const std::string& system) = 0;
    /// Fetches the signed index of every source again.
    virtual JobPtr Refresh() = 0;
    /// Reads the console plugins of the developer folder again (setting "plugins.developer").
    virtual void ReloadDeveloper() {}
    /// <data>/plugins-dev: one folder per plugin, console and feature plugins alike.
    virtual std::filesystem::path DeveloperDir() const {
        return {};
    }
    /// Grows whenever consoles or their installed state changed.
    virtual std::uint64_t Revision() const = 0;

    /// The official source first, then the added ones in the order they were added.
    virtual std::vector<SourceInfo> Sources() const {
        return {};
    }
    /// Reads what the user typed (owner/repo, an https address, "#fingerprint" to pin), fetches
    /// the source's key and its signed list and checks them; `out` is filled when it is done.
    virtual JobPtr ProbeSource(const std::string& /*text*/, std::shared_ptr<SourceProbe> /*out*/) {
        return nullptr;
    }
    /// Adds a probed source with its key pinned. Check for updates afterwards to see its plugins.
    virtual Status AddSource(const SourceProbe& /*probe*/) {
        return Status::Fail("This version cannot add plugin sources.");
    }
    /// A switched-off source is not checked and offers nothing; what came from it stays.
    virtual Status SetSourceEnabled(const std::string& /*address*/, bool /*enabled*/) {
        return Status::Fail("This version cannot change plugin sources.");
    }
    /// Removes the source and everything installed from it (its consoles' plugins and cores,
    /// its feature plugins). Games, saves, BIOS files and the plugins' own data stay.
    virtual JobPtr RemoveSource(const std::string& /*address*/) {
        return nullptr;
    }

    /// Reads a console plugin from a file or folder; fails (user-facing) when it is broken or
    /// may not be installed.
    virtual Result<PackageProbe> ProbePackage(const std::filesystem::path& /*path*/) const {
        return Status::Fail("This version cannot install plugins from a file.");
    }
    /// Installs a probed plugin. A console that runs in an engine may need its program next.
    virtual Status InstallPackage(const PackageProbe& /*probe*/) {
        return Status::Fail("This version cannot install plugins from a file.");
    }
    /// The console plugins this device had before an update that the app no longer brings.
    virtual std::vector<CarryOver> CarryOvers() const {
        return {};
    }
    /// Keeps one (brought up to date from its signed package beside the official list, when
    /// the list no longer offers it) or lets it go.
    virtual JobPtr SettleCarryOver(const std::string& /*plugin_id*/, bool /*keep*/) {
        return nullptr;
    }
};

/// Where a console plugin no longer offered is kept beside the official list, signed:
/// manual/<id>.emuplugin.
std::string ManualPackagePath(const std::string& plugin_id);

/// The setting that turns developer mode on: the developer folder's plugins are used, unsigned.
inline constexpr const char* DEVELOPER_KEY = "plugins.developer";

struct Config {
    std::filesystem::path data_dir; // plugins live under data_dir / "plugins", cores under "cores"
    Jobs* jobs = nullptr;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Host> CreateHost(const Config& config);

} // namespace den::plugins
