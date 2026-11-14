// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The controls of a running engine that speaks the Emulator Control Protocol (level 1 and
// 2). den::engine::Host::Control gives it while such a session runs; player::Session's
// engine adapter is built on it. Main thread only.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "den/base/json.h"
#include "den/base/result.h"

namespace den::engine {

/// A PNG the engine sent (a paused frame, a state's thumbnail).
struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> png;
    bool Empty() const {
        return png.empty();
    }
};

/// One setting of the engine's schema (section 6.4).
struct SettingDef {
    std::string key; // "video.internal_scale" for the standard ones
    std::string label;
    std::string help;
    std::string type; // toggle, choice, range, text
    std::vector<std::pair<std::string, std::string>> choices; // value, label
    double min = 0, max = 0, step = 1;
    std::string default_value; // as text: "true", "2", "16:9"
    std::string group;
    std::string applies = "now"; // now, restart
};

struct StateInfo {
    int slot = 0;
    std::int64_t time = 0; // Unix seconds
    Picture thumbnail;
};

struct Perf {
    double fps = 0;
    double target_fps = 0;
    double speed = 0; // 1.0 is full speed
    double frame_ms = 0;
    bool shaders_building = false;
    bool valid = false; // a perf event arrived
};

class Control {
public:
    virtual ~Control() = default;

    /// The level and capabilities the session uses (the engine's hello, cut to EmuDEN's).
    virtual int Level() const = 0;
    virtual bool Has(std::string_view capability) const = 0;
    /// The engine said hello and took the launch.
    virtual bool Ready() const = 0;
    /// It showed its first frame.
    virtual bool Playing() const = 0;
    /// It is paused (by EmuDEN, for the menu, or by itself).
    virtual bool Paused() const = 0;
    virtual Perf GetPerf() const = 0;
    /// The engine's settings and their values now (as text), when it has `settings`.
    virtual std::vector<SettingDef> Schema() const = 0;
    virtual std::vector<std::pair<std::string, std::string>> Values() const = 0;

    /// Requests that answer later; what fails is logged.
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void Reset(bool hard) = 0;
    virtual void SetSpeed(bool fast_forward, double factor) = 0;
    virtual void SetRewind(bool on) = 0;
    virtual void SetVolume(int percent, bool mute) = 0;
    /// Sets the value for this session; the caller keeps it for the next launch.
    virtual void SetSetting(const std::string& key, const std::string& value) = 0;

    /// Requests that wait (pumping the channel) for their answer, a few seconds at most.
    virtual Result<StateInfo> SaveState(int slot) = 0;
    virtual Status LoadState(int slot) = 0;
    virtual Result<std::vector<StateInfo>> ListStates() = 0;

    /// The paused frame of a menu request the engine sent since the last call (it is
    /// paused and handed the menu over); nullopt when there was none.
    virtual std::optional<Picture> TakeMenuRequest() = 0;
};

} // namespace den::engine
