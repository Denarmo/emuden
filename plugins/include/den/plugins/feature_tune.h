// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What the tuning calls of feature plugins reach (den.device.info, den.game, den.steam):
// the device's facts, a game's own core options, and Steam's launch options and Proton
// version. The app implements it over its device profile, settings and Steam; tests use a
// fake. Every call may block and is made from a helper thread.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "den/base/json.h"
#include "den/base/result.h"

namespace den::plugins {

struct TuneOption {
    std::string key, label;
    std::string value;               // in effect for the game
    std::vector<std::string> values; // the core's choices
    bool own = false;                // the game's own value (not the console's or global)
};

struct TuneGame {
    std::string id, name, system;
    std::string core, core_name; // the core that plays it
    bool known = false;          // the core's options are known (a game of it ran once)
    std::vector<TuneOption> options;
};

struct SteamApp {
    std::string appid, name;
    bool shortcut = false;
};

struct SteamConfig {
    std::string launch_options, compat_tool, compat_tool_display;
    bool compat_tool_chosen = false; // the user picked it; otherwise Steam chose it ("" undoes to that)
};

struct SteamTool {
    std::string name, display;
};

struct SteamState {
    bool installed = false; // a Steam installation was found
    bool can_change = false; // launch options and Proton can be set now
    std::string note;        // why not, for the page
};

class TuneServices {
public:
    virtual ~TuneServices() = default;
    /// den.device.info(): name, manufacturer, model, soc, os, os_version, cpu_cores, ram_mb,
    /// gpu, form, tier, known, handheld, steamos, screen.
    virtual JsonValue DeviceFacts() = 0;
    virtual std::optional<TuneGame> Game(const std::string& id) = 0;
    /// Sets the game's own core options: a string value, or null to remove the game's own
    /// value. Returns the game's own values before, as an object (null for none), for undo.
    virtual Result<JsonValue> SetGameOptions(const std::string& id, const JsonValue& values) = 0;

    virtual SteamState Steam() = 0;
    virtual std::vector<SteamApp> SteamApps() = 0;
    virtual std::vector<SteamTool> SteamTools() = 0;
    virtual Result<SteamConfig> SteamAppConfig(const std::string& appid) = 0;
    virtual Status SetSteamApp(const std::string& appid, const std::optional<std::string>& launch_options,
                               const std::optional<std::string>& compat_tool) = 0;
};

} // namespace den::plugins
