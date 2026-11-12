// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the engine host: games that run in an engine outside the app (Switch 2). This
// stage is protocol level 0: start the engine for a game and wait for it to exit. The
// implementation behind CreateHost is a fake (src/fake_host.cpp) until the real one
// replaces it.

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "den/base/result.h"

namespace den::store {
class Settings;
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

class Host {
public:
    virtual ~Host() = default;

    /// Whether an engine for the system is set up on this device.
    virtual bool Available(const std::string& system) const = 0;
    /// What the user has to do when it is not (shown as is).
    virtual std::string UnavailableReason(const std::string& system) const = 0;

    /// Starts the engine for a game. Returns at once; the outcome arrives as events.
    virtual Status Start(const LaunchRequest& request) = 0;
    virtual bool Running() const = 0;
    /// Ends the running engine.
    virtual void Stop() = 0;
    /// The events since the last call. Main thread, once per frame.
    virtual std::vector<Event> PollEvents() = 0;
};

struct Config {
    std::filesystem::path data_dir;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Host> CreateHost(const Config& config);

} // namespace den::engine
