// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Facade of the ROM player: one libretro core with one game, running inside the app's
// window. The implementation behind CreateSession is a fake (src/fake_session.cpp) until the
// real player replaces it. libretro.h (from upstream libretro) is in third_party/libretro.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "den/base/jobs.h"
#include "den/base/result.h"
#include "den/gfx/canvas.h"
#include "den/input/input.h"

namespace den::store {
class Settings;
}

namespace den::player {

struct OpenRequest {
    std::filesystem::path core;       // plugins::CoreInfo::library
    std::filesystem::path content;    // the ROM
    std::filesystem::path system_dir; // BIOS files
    std::string game_id;              // library entry id; names the save and state files
    std::string system;               // system id
    /// Option values from the plugin manifest, applied before the user's own.
    std::vector<std::pair<std::string, std::string>> option_hints;
};

enum class State { Closed, Running, Paused, Failed };

/// What the screens show about a session. A copy; cheap to take every frame.
struct Snapshot {
    State state = State::Closed;
    std::string error; // user-facing, when Failed
    std::string core_name;
    std::string game_id;
    int frame_width = 0, frame_height = 0;
    float aspect = 4.0f / 3.0f;
    double core_fps = 60.0;     // what the core asks for
    double measured_fps = 0.0;  // what it gets
    std::uint64_t frames = 0;
    bool fast_forward = false;
    bool rewinding = false;
    bool can_save_states = false;
    bool can_rewind = false;
};

struct CoreOption {
    std::string key;
    std::string label;
    std::string value;
    std::vector<std::string> values;
};

constexpr int STATE_SLOTS = 10;

/// Main thread only. The app's player screen drives it: Open, then per frame SetPad,
/// Advance and Draw, and Close when the screen leaves.
class Session {
public:
    virtual ~Session() = default;

    /// Loads the core and the game and starts running. On failure the snapshot's state is
    /// Failed and the Status carries the same user-facing text.
    virtual Status Open(const OpenRequest& request) = 0;
    /// Writes the battery save and unloads the game and the core.
    virtual void Close() = 0;

    virtual void Pause() = 0;
    virtual void Resume() = 0;
    /// The pad for a port (0 = player one), by position. Call before Advance.
    virtual void SetPad(int port, const input::PadState& pad) = 0;
    /// Runs as many core frames as `seconds` of real time ask for (none while paused) and
    /// queues their audio.
    virtual void Advance(double seconds) = 0;
    /// Draws the latest frame into `area` of the frame the UI is drawing (letterboxed to the
    /// core's aspect ratio). The session may call canvas.Flush(), use its own GL, and must
    /// leave the canvas usable (canvas.RestoreState()).
    virtual void Draw(gfx::Canvas& canvas, const gfx::Rect& area) = 0;

    virtual Status SaveState(int slot) = 0;
    virtual Status LoadState(int slot) = 0;
    virtual bool HasState(int slot) const = 0;
    virtual void SetFastForward(bool on) = 0;
    /// While on, Advance steps backwards through the rewind buffer.
    virtual void SetRewind(bool on) = 0;
    virtual void Reset() = 0;

    virtual std::vector<CoreOption> Options() const = 0;
    virtual void SetOption(const std::string& key, const std::string& value) = 0;

    virtual Snapshot GetSnapshot() const = 0;
};

struct Config {
    std::filesystem::path data_dir; // saves, states and core options live under it
    Jobs* jobs = nullptr;
    gfx::Device* device = nullptr;
    store::Settings* settings = nullptr;
};

std::unique_ptr<Session> CreateSession(const Config& config);

} // namespace den::player
