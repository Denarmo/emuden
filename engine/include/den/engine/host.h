// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the engine host: games that run in an engine outside the app. Level 0 starts
// the engine for a game and waits for it to exit (Switch 2); levels 1 and 2 speak the
// Emulator Control Protocol (docs/engines.md), and Control() drives such a session.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "den/base/result.h"
#include "den/engine/control.h"

namespace den::store {
class Settings;
}
namespace den::pal {
class Window;
}

namespace den::engine {

/// An engine found on this device, or named by a plugin manifest.
struct Install {
    std::filesystem::path executable; // desktop
    std::string source;               // "setting", "plugin", "desktop" or "manifest"
    int level = 0;                    // the protocol level it is driven at
    std::string core_id;              // the plugin component it comes from (engine apps)
    std::string package;              // Android: the app's package name
    std::string cert_sha256;          // Android: its pinned signing certificate
    std::vector<std::string> args;    // desktop: more arguments after "--ecp stdio"
    /// An engine that is started by someone else (a test, a tool): the host listens on
    /// loopback and calls this with the port and token the engine connects with.
    std::function<void(int port, const std::string& token)> attach;
};

/// The engine's own setup (ECP `configure`, SPEC 6.6): what the player gave EmuDEN, kept by
/// the engine so that it opened on its own skips its first-run setup.
struct EngineSetup {
    std::vector<std::filesystem::path> bios;         // files or folders with the BIOS files
    std::vector<std::filesystem::path> game_folders; // folders with the system's games
    std::vector<std::pair<std::string, std::string>> settings; // by key, as text

    bool Empty() const {
        return bios.empty() && game_folders.empty() && settings.empty();
    }
};

struct LaunchRequest {
    std::string system;  // "switch2"
    std::string game_id; // library entry id, echoed in events
    std::string title;
    std::filesystem::path content; // the game file
    bool fullscreen = true;
    /// The engine to use; empty: the one Find gives for the system.
    std::optional<Install> install;
    /// Level 2: setting values by key (as text), sent with the launch.
    std::vector<std::pair<std::string, std::string>> settings;
    /// A player::Session drives the session (the player screen shows it), so the app covers
    /// it with no page of its own. The host still books its play time.
    bool controlled = false;
    std::string locale = "en";
    /// Sent before the launch when the engine has `configure`.
    std::optional<EngineSetup> setup;
};

struct Event {
    enum class Type {
        Started,       // the engine process is running
        Exited,        // it ended; exit_code says how
        Failed,        // it could not be started or stopped for an error; message says why
        Loading,       // level 1: it loads the game (progress 0..1 when known, message)
        FirstFrame,    // level 1: the game is on screen
        Paused,        // level 2: message is the reason (frontend, menu, focus)
        Resumed,       // level 2
        MenuRequested, // level 2: it paused and handed the menu over (Control::TakeMenuRequest)
    };
    Type type = Type::Started;
    std::string game_id;
    int exit_code = 0;
    double seconds = 0; // how long it ran (Exited)
    double progress = -1;
    std::string message;
};

/// What the host does by itself around a session, so no screen has to.
struct Hooks {
    /// Hidden while a game runs, shown again when the engine exits.
    pal::Window* window = nullptr;
    /// Books the play time when the engine exits (the library's RecordPlay), from the
    /// game's first frame when the engine reports one. Screens must not book engine
    /// sessions a second time.
    std::function<void(const std::string& game_id, std::int64_t started_unix, std::int64_t seconds)>
        record_play;
};

class Host {
public:
    virtual ~Host() = default;

    virtual void Attach(Hooks hooks) = 0;

    /// Whether an engine for the system is set up on this device.
    virtual bool Available(const std::string& system) const = 0;
    /// What the user has to do when it is not (shown as is).
    virtual std::string UnavailableReason(const std::string& system) const = 0;
    /// The engine Start would use.
    virtual std::optional<Install> Find(const std::string& system) const = 0;

    /// Starts the engine for a game. Returns at once; the outcome arrives as events.
    virtual Status Start(const LaunchRequest& request) = 0;
    virtual bool Running() const = 0;
    /// The request of the running session.
    virtual const LaunchRequest* Current() const = 0;
    /// Asks the running engine to close and save; it is ended 20 s later if it has not.
    virtual void Stop() = 0;
    /// Main thread, once per frame: notices the exit, restores the window, books the time.
    /// The app calls it; PollEvents does too.
    virtual void Pump() = 0;
    /// The events since the last call. Main thread.
    virtual std::vector<Event> PollEvents() = 0;
    /// The controls of the running session when its engine speaks the protocol (level 1
    /// and up); null otherwise. Valid until the session ends (Running() is false).
    virtual engine::Control* Control() {
        return nullptr;
    }
    /// Checks an engine app before it is started: installed, and signed with the pinned
    /// certificate. Desktop installs: the executable is there.
    virtual Status Check(const Install& install) const = 0;
};

struct Config {
    std::filesystem::path data_dir;
    store::Settings* settings = nullptr;
    std::string app_version = "0.1.0"; // told to engines in the hello
};

std::unique_ptr<Host> CreateHost(const Config& config);

/// Sets up an engine app outside a session (Android: the CONFIGURE broadcast, SPEC 6.6). Fails
/// where engines are not apps or the installed app is not the pinned one. Main thread.
Status ConfigureApp(const Install& install, const EngineSetup& setup);

// Settings the host reads, per system ("engine.switch2.path").
/// A folder or an executable; empty means auto-detect.
std::string PathKey(const std::string& system);
/// A list replacing the engine's arguments; "{content}" stands for the game file.
std::string ArgsKey(const std::string& system);

} // namespace den::engine
