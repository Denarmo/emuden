// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An engine in the test's own process: it connects to the host's loopback listener and
// answers the Emulator Control Protocol the way a level-2 engine does, from a script of
// defaults the test can change. Used by the engine and the player tests.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "den/base/json.h"
#include "den/platform/stream.h"

namespace den::test {

// A 1x1 PNG.
inline constexpr const char* TINY_PNG =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";

class FakeEngine {
public:
    std::vector<std::string> capabilities = {"pause", "menu", "states", "state_thumbnails", "reset",
                                             "fast_forward", "settings", "perf", "volume", "cheats"};
    int level = 2;
    std::string token_override; // sent instead of the right token when set
    bool answer_launch = true;
    std::string launch_error; // the launch fails with this code when set

    ~FakeEngine() {
        Close();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /// The host's attach hook: connects from a thread of its own.
    void Connect(int port, const std::string& token) {
        thread_ = std::thread([this, port, token] { Run(port, token); });
    }

    /// A state the engine has from an earlier session.
    void Seed(int slot) {
        saved_.insert(slot);
    }

    /// An event from the engine (menu_request, paused, ...).
    void SendEvent(JsonValue message) {
        Write(message);
    }

    /// Every message the frontend sent, oldest first.
    std::vector<JsonValue> Received() const {
        std::lock_guard lock(mutex_);
        return received_;
    }
    bool Got(const std::string& type) const {
        for (const JsonValue& message : Received()) {
            if (message.String("type") == type) {
                return true;
            }
        }
        return false;
    }
    /// The last message of a type the frontend sent; a null value when none.
    JsonValue Last(const std::string& type) const {
        JsonValue out;
        for (const JsonValue& message : Received()) {
            if (message.String("type") == type) {
                out = message;
            }
        }
        return out;
    }
    /// Waits for a message of the type to be answered (its reply and events written), up to `seconds`.
    bool WaitFor(const std::string& type, double seconds = 5) {
        std::unique_lock lock(mutex_);
        return seen_.wait_for(lock, std::chrono::duration<double>(seconds), [&] {
            for (const JsonValue& message : answered_) {
                if (message.String("type") == type) {
                    return true;
                }
            }
            return false;
        });
    }
    bool Connected() const {
        return connected_;
    }
    bool Ended() const {
        return ended_;
    }
    void Close() {
        std::lock_guard lock(write_lock_);
        if (stream_) {
            stream_->Close();
        }
    }

private:
    static JsonValue Obj(std::vector<std::pair<std::string, JsonValue>> members) {
        return JsonObject(std::move(members));
    }
    static JsonValue Picture() {
        return Obj({{"png", JsonString(TINY_PNG)}, {"width", JsonNumber(1)}, {"height", JsonNumber(1)}});
    }

    bool Write(const JsonValue& message) {
        std::lock_guard lock(write_lock_);
        if (!stream_) {
            return false;
        }
        const std::string text = WriteJson(message);
        const auto size = static_cast<std::uint32_t>(text.size());
        const std::uint8_t header[4] = {static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8),
                                        static_cast<std::uint8_t>(size >> 16), static_cast<std::uint8_t>(size >> 24)};
        return stream_->Write(header, 4) && stream_->Write(text.data(), text.size());
    }

    std::optional<JsonValue> Read() {
        std::uint8_t header[4];
        if (!stream_->Read(header, 4)) {
            return std::nullopt;
        }
        const std::uint32_t size = header[0] | (header[1] << 8) | (header[2] << 16) | (static_cast<std::uint32_t>(header[3]) << 24);
        std::string text(size, '\0');
        if (!stream_->Read(text.data(), size)) {
            return std::nullopt;
        }
        return ParseJson(text);
    }

    void Reply(const JsonValue& request, bool ok, std::vector<std::pair<std::string, JsonValue>> fields = {},
                const std::string& code = {}) {
        std::vector<std::pair<std::string, JsonValue>> members = {
            {"type", JsonString("result")}, {"id", *request.Find("id")}, {"ok", JsonBool(ok)}};
        if (!ok) {
            members.emplace_back("error", Obj({{"code", JsonString(code)}}));
        }
        for (auto& field : fields) {
            members.push_back(std::move(field));
        }
        Write(Obj(std::move(members)));
    }

    void Emit(const std::string& type, std::vector<std::pair<std::string, JsonValue>> fields = {}) {
        fields.insert(fields.begin(), {"type", JsonString(type)});
        Write(Obj(std::move(fields)));
    }

    void Answer(const JsonValue& message) {
        const std::string type = message.String("type");
        if (message.Find("id") == nullptr) {
            return; // the frontend's hello
        }
        if (type == "get_schema") {
            std::vector<JsonValue> settings;
            settings.push_back(Obj({{"key", JsonString("video.internal_scale")}, {"label", JsonString("Resolution")},
                                    {"type", JsonString("range")}, {"min", JsonNumber(1)}, {"max", JsonNumber(4)},
                                    {"step", JsonNumber(1)}, {"default", JsonNumber(1)}}));
            settings.push_back(Obj({{"key", JsonString("video.widescreen_patches")}, {"label", JsonString("Widescreen")},
                                    {"type", JsonString("toggle")}, {"default", JsonBool(false)},
                                    {"applies", JsonString("restart")}}));
            settings.push_back(Obj({{"key", JsonString("video.aspect")}, {"label", JsonString("Aspect")},
                                    {"type", JsonString("choice")},
                                    {"choices", JsonArray({JsonArray({JsonString("auto"), JsonString("Auto")}),
                                                           JsonArray({JsonString("16:9"), JsonString("16:9")})})},
                                    {"default", JsonString("auto")}}));
            Reply(message, true, {{"settings", JsonArray(std::move(settings))}});
        } else if (type == "launch") {
            if (!answer_launch) {
                return;
            }
            if (!launch_error.empty()) {
                Reply(message, false, {}, launch_error);
                return;
            }
            Reply(message, true);
            Emit("loading", {{"progress", JsonNumber(0.5)}});
            Emit("first_frame");
            Emit("running");
            Emit("perf", {{"fps", JsonNumber(59.9)}, {"target_fps", JsonNumber(60)}, {"speed", JsonNumber(1)},
                           {"frame_ms", JsonNumber(16.7)}});
        } else if (type == "pause") {
            Reply(message, true);
            Emit("paused", {{"reason", JsonString("frontend")}});
        } else if (type == "resume") {
            Reply(message, true);
            Emit("resumed");
        } else if (type == "save_state") {
            const int slot = static_cast<int>(message.Find("slot")->number);
            saved_.insert(slot);
            Reply(message, true, {{"slot", JsonNumber(slot)}, {"time", JsonNumber(1700000000)},
                                   {"thumbnail", Picture()}});
        } else if (type == "load_state") {
            const int slot = static_cast<int>(message.Find("slot")->number);
            if (saved_.count(slot) != 0) {
                Reply(message, true);
            } else {
                Reply(message, false, {}, "slot_empty");
            }
        } else if (type == "list_states") {
            std::vector<JsonValue> states;
            for (const int slot : saved_) {
                states.push_back(Obj({{"slot", JsonNumber(slot)}, {"time", JsonNumber(1700000000)},
                                      {"thumbnail", Picture()}}));
            }
            Reply(message, true, {{"states", JsonArray(std::move(states))}});
        } else if (type == "set_setting") {
            Reply(message, true, {{"applied", JsonString("now")}});
        } else if (type == "stop") {
            Reply(message, true);
            Emit("stopped", {{"reason", JsonString("frontend")}});
            Close();
        } else if (type == "set_speed" || type == "set_volume" || type == "reset" || type == "set_rewind") {
            Reply(message, true);
        } else {
            Reply(message, false, {}, "unsupported");
        }
    }

    void Run(int port, const std::string& token) {
        auto stream = pal::ConnectLoopback(port);
        if (!stream) {
            ended_ = true;
            return;
        }
        {
            std::lock_guard lock(write_lock_);
            stream_ = stream.take();
        }
        connected_ = true;
        Write(Obj({{"type", JsonString("auth")}, {"token", JsonString(token_override.empty() ? token : token_override)}}));
        std::vector<JsonValue> caps;
        for (const std::string& capability : capabilities) {
            caps.push_back(JsonString(capability));
        }
        Write(Obj({{"type", JsonString("hello")}, {"protocol", JsonNumber(1)}, {"ecp", JsonNumber(1)},
                   {"id", JsonString("io.github.example.fake")}, {"name", JsonString("Fake")},
                   {"version", JsonString("1.0.0")}, {"level", JsonNumber(level)},
                   {"systems", JsonArray({JsonString("gba")})}, {"capabilities", JsonArray(caps)},
                   {"settings", JsonString("schema")}}));
        while (std::optional<JsonValue> message = Read()) {
            {
                std::lock_guard lock(mutex_);
                received_.push_back(*message);
            }
            Answer(*message);
            {
                std::lock_guard lock(mutex_);
                answered_.push_back(*message);
            }
            seen_.notify_all();
        }
        Close();
        ended_ = true;
    }

    std::thread thread_;
    std::mutex write_lock_;
    std::unique_ptr<pal::Stream> stream_;
    mutable std::mutex mutex_;
    std::condition_variable seen_;
    std::vector<JsonValue> received_;
    std::vector<JsonValue> answered_;
    std::set<int> saved_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> ended_{false};
};

} // namespace den::test
