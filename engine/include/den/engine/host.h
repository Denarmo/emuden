// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the engine host: games that run in an engine outside the app (Switch 2). This
// stage is protocol level 0 of contracts/engine-contract.md: start the engine for a game
// and wait for it to exit. Higher levels add events and requests here; what exists stays.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::store {
class Settings;
}
namespace den::pal {
class Window;
}

namespace den::engine {

struct LaunchRequest {
    std::string system;  // "switch2"
    std::string game_id; // library entry id, echoed in events
    std::string title;
    std::filesystem::path content; // the game file
    bool fullscreen = true;
};

struct Event {
    enum class Type {
        Started, // the engine process is running
        Exited,  // it ended; exit_code says how
        Failed,  // it could not be started; message says why (user-facing)
    };
    Type type = Type::Started;
    std::string game_id;
    int exit_code = 0;
    double seconds = 0; // how long it ran (Exited)
    std::string message;
};

/// An engine found on this device.
struct Install {
    std::filesystem::path executable;
    std::string source; // "setting", "plugin" or "desktop"
    int level = 0;      // the protocol level it is driven at
};

/// What the host does by itself around a session, so no screen has to.
struct Hooks {
    /// Hidden while a game runs, shown again when the engine exits.
    pal::Window* window = nullptr;
    /// Books the play time when the engine exits (the library's RecordPlay). Screens must
    /// not book engine sessions a second time.
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
    /// Asks the running engine to close; it is ended after a few seconds if it does not.
    virtual void Stop() = 0;
    /// Main thread, once per frame: notices the exit, restores the window, books the time.
    /// The app calls it; PollEvents does too.
    virtual void Pump() = 0;
    /// The events since the last call. Main thread.
    virtual std::vector<Event> PollEvents() = 0;
};

struct Config {
    std::filesystem::path data_dir;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Host> CreateHost(const Config& config);

// Settings the host reads, per system ("engine.switch2.path").
/// A folder or an executable; empty means auto-detect.
std::string PathKey(const std::string& system);
/// A list replacing the engine's arguments; "{content}" stands for the game file.
std::string ArgsKey(const std::string& system);

} // namespace den::engine
