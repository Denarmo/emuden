// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One Emulator Control Protocol session over a stream, whatever carries it (a child's
// pipes, a loopback connection). A thread of its own reads the frames and does the opening
// exchange (auth, hello, schema, launch), so the engine is answered even while the app's
// loop is slow; the main thread sends requests and turns the events into engine::Events.

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "den/engine/control.h"
#include "den/engine/host.h"
#include "ecp/wire.h"

namespace den::engine::ecp {

/// How the stream comes.
struct Opening {
    /// Given (a child's standard input and output); not owned, and the caller keeps it alive
    /// until the channel is gone.
    pal::Stream* stream = nullptr;
    /// Or accepted from this listener within `connect_seconds`; its first frame must be the
    /// auth frame with `token`.
    std::unique_ptr<pal::Listener> listener;
    std::string token;
    double connect_seconds = 10;
};

/// What the launch request says (section 5).
struct LaunchInfo {
    std::string content; // the path, or "intent"
    std::string system;
    std::string game_id;
    std::string title;
    std::string locale = "en";
    bool fullscreen = true;
    std::vector<std::pair<std::string, std::string>> settings; // by key, as text
    std::vector<std::string> menu_chord;
};

/// The capabilities EmuDEN drives; an engine's others are left out of the answer.
const std::vector<std::string>& FrontendCapabilities();

class Channel final : public Control {
public:
    Channel(Opening opening, LaunchInfo launch, std::string frontend_version);
    /// Closes the stream and waits for the reader (for pipes: the child must have ended or
    /// let go of its output by then).
    ~Channel() override;

    /// Main thread, every frame: the events since the last call.
    std::vector<Event> Drain(const std::string& game_id);
    /// The engine said hello (and was answered).
    bool HelloDone() const {
        return hello_done_;
    }
    /// The stream ended.
    bool Closed() const {
        return closed_;
    }
    /// It sent `stopped`; `StopError` is the error text when it stopped for one.
    bool Stopped() const {
        return stopped_;
    }
    const std::string& StopError() const {
        return stop_error_;
    }
    /// The monotonic time of the first frame (pal::NowSeconds), 0 before.
    double FirstFrameAt() const {
        return first_frame_at_;
    }
    /// Sends `stop` when the engine is ready for requests.
    bool RequestStop();
    /// Ends the stream now.
    void Close();
    /// Called on the main thread with false when the engine hands the menu over (the app
    /// comes to the front) and with true when the game goes on (the engine does).
    std::function<void(bool engine_in_front)> on_front;

    int Level() const override;
    bool Has(std::string_view capability) const override;
    bool Ready() const override;
    bool Playing() const override;
    bool Paused() const override;
    Perf GetPerf() const override;
    std::vector<SettingDef> Schema() const override;
    std::vector<std::pair<std::string, std::string>> Values() const override;
    void Pause() override;
    void Resume() override;
    void Reset(bool hard) override;
    void SetSpeed(bool fast_forward, double factor) override;
    void SetRewind(bool on) override;
    void SetVolume(int percent, bool mute) override;
    void SetSetting(const std::string& key, const std::string& value) override;
    Result<StateInfo> SaveState(int slot) override;
    Status LoadState(int slot) override;
    Result<std::vector<StateInfo>> ListStates() override;
    std::optional<Picture> TakeMenuRequest() override;

    /// How long a waiting request waits for its answer.
    static constexpr double CALL_SECONDS = 8.0;

private:
    void Run();
    bool Open();
    bool Handshake();
    void Fail(std::string message, std::string detail);
    std::int64_t Send(const std::string& type, std::vector<std::pair<std::string, JsonValue>> fields);
    std::optional<JsonValue> Call(const std::string& type, std::vector<std::pair<std::string, JsonValue>> fields);
    bool WriteNow(const JsonValue& message);
    void Handle(const JsonValue& message, std::vector<Event>& out, const std::string& game_id);
    const SettingDef* FindSetting(const std::string& key) const;

    Opening opening_;
    LaunchInfo launch_;
    std::string frontend_version_;
    std::unique_ptr<pal::Stream> owned_; // the accepted connection
    std::atomic<pal::Stream*> stream_{nullptr};

    mutable std::mutex mutex_;
    std::condition_variable answered_;
    std::deque<JsonValue> events_;             // reader -> main thread
    std::map<std::int64_t, JsonValue> results_; // answers of waiting requests
    std::set<std::int64_t> waiting_;
    std::int64_t next_id_ = 1;
    std::int64_t launch_id_ = 0;
    int level_ = 0;
    std::set<std::string, std::less<>> capabilities_;
    std::vector<SettingDef> schema_;
    std::vector<std::pair<std::string, std::string>> values_;

    std::atomic<bool> hello_done_{false};
    std::atomic<bool> closed_{false};
    std::atomic<bool> closing_{false};
    bool ready_ = false;
    bool playing_ = false;
    bool paused_ = false;
    bool stopped_ = false;
    std::string stop_error_;
    double first_frame_at_ = 0;
    Perf perf_;
    std::optional<Picture> menu_;
    std::thread reader_;
};

} // namespace den::engine::ecp
