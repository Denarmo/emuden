// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Steam installation of this computer, read from its files: the installed games, the
// non-Steam shortcuts, each app's launch options and compatibility tool, and the Proton
// versions there are. Changing launch options and the compatibility tool goes through the
// running Steam client (SteamClient over its local debugging port, which Decky Loader and
// similar tools turn on), because Steam rewrites its own files while it runs. Without that
// port nothing is written.

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "den/base/result.h"

namespace den::library::steam {

/// One node of Valve's KeyValues text format (libraryfolders.vdf, appmanifest_*.acf).
struct Vdf {
    std::string key, value;
    std::vector<Vdf> children; // a block; empty for a value
    const Vdf* Find(std::string_view name) const; // case-insensitive, like Steam
    std::string Value(std::string_view name) const;
};
/// The root's children; nullopt for text that is not KeyValues.
std::optional<Vdf> ParseVdf(std::string_view text);

struct Shortcut {
    std::string appid; // unsigned decimal, as Steam names the shortcut
    std::string name, exe, launch_options;
};
/// shortcuts.vdf (binary KeyValues).
std::vector<Shortcut> ParseShortcuts(std::string_view bytes);

struct App {
    std::string appid, name;
    bool shortcut = false;
    bool tool = false; // Proton, a Steam Linux Runtime, redistributables
};

struct AppConfig {
    std::string launch_options;
    std::string compat_tool;         // internal name ("proton_9", "GE-Proton9-20"); empty = default
    std::string compat_tool_display; // when the client told it
};

struct Tool {
    std::string name, display;
    bool installed = false;
};

/// The Steam folder: EMUDEN_STEAM_DIR, else the usual places of the platform's Steam.
std::optional<std::filesystem::path> FindRoot();

/// The internal name Steam gives an official Proton by its display name ("Proton 9.0" ->
/// "proton_9", "Proton 6.3" -> "proton_63", "Proton Experimental" -> "proton_experimental").
std::string ProtonToolName(std::string_view display);

/// Launch options a plugin may set: environment assignments, a few known wrappers and their
/// flags, `%command%` and the game's own arguments, with no shell syntax at all. Steam runs
/// launch options through a shell, so anything else is refused.
Status CheckLaunchOptions(std::string_view text);

class Install {
public:
    explicit Install(std::filesystem::path root);
    const std::filesystem::path& Root() const {
        return root_;
    }
    /// Installed games (every library folder) and the shortcuts of every user, by name.
    std::vector<App> Apps() const;
    /// The compatibility tools: official Protons installed as apps and the custom ones in
    /// compatibilitytools.d.
    std::vector<Tool> Tools() const;
    /// From localconfig.vdf and config.vdf (or shortcuts.vdf): as Steam last wrote them.
    AppConfig ConfigFromFiles(const std::string& appid) const;

private:
    std::filesystem::path root_;
};

/// The running Steam client, reached over 127.0.0.1:<port> (Chrome DevTools protocol).
class Client {
public:
    /// `evaluate` runs JavaScript in Steam's SharedJSContext and returns the result as text;
    /// tests pass a fake. The default one speaks the DevTools protocol on the port.
    using Evaluate = std::function<Result<std::string>(const std::string& script)>;
    explicit Client(Evaluate evaluate);
    static Client Local(int port = 8080);
    /// Whether the debugging port is on (the marker file exists) for this Steam folder.
    static bool Enabled(const std::filesystem::path& root);

    Result<AppConfig> Config(const std::string& appid);
    /// Either may be empty-optional to leave it. `compat_tool` "" goes back to Steam's choice.
    Status Set(const std::string& appid, const std::optional<std::string>& launch_options,
               const std::optional<std::string>& compat_tool);

private:
    Evaluate evaluate_;
};

/// A JavaScript string literal of the text.
std::string JsString(std::string_view text);

} // namespace den::library::steam
