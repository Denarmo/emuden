// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cmath>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/engine/host.h"
#include "den/platform/pal.h"
#include "den/store/settings.h"
#include "host/locate.h"
#include "protocol/link.h"

namespace den::engine {

namespace {

// The app's window stays up this long, so an engine that fails at once never makes it blink.
constexpr double HIDE_AFTER_SECONDS = 1.5;
// How long a stop may take before the engine is ended: it writes its saves on the way out.
constexpr double STOP_GRACE_SECONDS = 20.0;
// The same when the engine could not be asked (no window yet): nothing to wait for.
constexpr double UNASKED_GRACE_SECONDS = 5.0;
constexpr double UNASKED_CLOSE_GRACE_SECONDS = 1.5;
// An engine may ask "really close?" first: that is answered for this long after the stop.
constexpr double CONFIRM_SECONDS = 6.0;
constexpr double CONFIRM_EVERY_SECONDS = 0.5;

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

class RealHost final : public Host {
public:
    explicit RealHost(const Config& config) : config_(config) {}

    ~RealHost() override {
        if (link_) {
            // The app is closing: give the engine the time to save, then end it. A stop
            // that is already under way keeps its deadline.
            if (stop_deadline_ == 0) {
                AskToStop(UNASKED_CLOSE_GRACE_SECONDS);
            }
            while (!link_->Exited() && pal::NowSeconds() < stop_deadline_) {
                Confirm(pal::NowSeconds());
                pal::SleepMs(20);
            }
            Finish(link_->Exited());
        }
    }

    void Attach(Hooks hooks) override {
        hooks_ = std::move(hooks);
    }

    bool Available(const std::string& system) const override {
        return Find(system).has_value();
    }

    std::optional<Install> Find(const std::string& system) const override {
        if (!pal::GetCaps().child_processes) {
            return std::nullopt;
        }
        return Locate(system, config_.data_dir, config_.settings);
    }

    std::string UnavailableReason(const std::string& system) const override {
        if (!KnownSystem(system)) {
            return "There is no engine for this console.";
        }
        if (!pal::GetCaps().child_processes) {
            return "This console is not available on this device yet.";
        }
        if (Find(system)) {
            return {};
        }
        const std::string chosen =
            config_.settings ? Trim(config_.settings->GetString(PathKey(system))) : std::string();
        if (!chosen.empty()) {
            return "No engine was found at the engine path set in Settings (" + chosen +
                   "). Correct the path, or clear it to find the engine automatically.";
        }
        return "The engine for this console is not installed. Install the console from "
               "Consoles, or set the engine path in Settings.";
    }

    Status Start(const LaunchRequest& request) override {
        if (link_) {
            return Fail(request, "Another game is still running.");
        }
        const auto install = Find(request.system);
        if (!install) {
            return Fail(request, UnavailableReason(request.system));
        }
        if (!IsRegularFile(request.content) && !IsDirectory(request.content)) {
            return Fail(request, "The game file is missing: " + PathToUtf8(request.content));
        }
        auto link = OpenLink(*install, request, LaunchArgs(*install, request, config_.settings));
        if (!link) {
            log::Error("engine", "{}", link.error());
            return Fail(request, "The engine could not be started (" + link.error() + ").");
        }
        log::Info("engine", "started {} for {} (level {}, {})", PathToUtf8(install->executable),
                  request.game_id, install->level, install->source);
        link_ = link.take();
        request_ = request;
        started_ = pal::NowSeconds();
        started_unix_ = UnixNow();
        stop_deadline_ = 0;
        confirm_until_ = 0;
        window_hidden_ = false;
        Event event;
        event.type = Event::Type::Started;
        event.game_id = request.game_id;
        events_.push_back(std::move(event));
        return {};
    }

    bool Running() const override {
        return link_ != nullptr;
    }

    const LaunchRequest* Current() const override {
        return link_ ? &request_ : nullptr;
    }

    void Stop() override {
        if (link_ && stop_deadline_ == 0) {
            AskToStop(UNASKED_GRACE_SECONDS);
        }
    }

    void Pump() override {
        if (!link_) {
            return;
        }
        for (Event& event : link_->Drain()) {
            events_.push_back(std::move(event));
        }
        const double now = pal::NowSeconds();
        if (const auto code = link_->Exited()) {
            Finish(code);
            return;
        }
        Confirm(now);
        if (stop_deadline_ != 0 && now >= stop_deadline_) {
            log::Warn("engine", "{} did not close in time and was ended", request_.game_id);
            link_->Kill();
            Finish(link_->Exited());
            return;
        }
        if (!window_hidden_ && hooks_.window && now - started_ >= HIDE_AFTER_SECONDS) {
            hooks_.window->Hide();
            window_hidden_ = true;
        }
    }

    std::vector<Event> PollEvents() override {
        Pump();
        std::vector<Event> out;
        out.swap(events_);
        return out;
    }

private:
    void AskToStop(double unasked_grace) {
        const double now = pal::NowSeconds();
        const bool asked = link_->RequestStop();
        stop_deadline_ = now + (asked ? STOP_GRACE_SECONDS : unasked_grace);
        confirm_until_ = asked ? now + CONFIRM_SECONDS : 0;
        confirm_next_ = now + CONFIRM_EVERY_SECONDS;
    }

    void Confirm(double now) {
        if (now < confirm_until_ && now >= confirm_next_) {
            link_->ConfirmStop();
            confirm_next_ = now + CONFIRM_EVERY_SECONDS;
        }
    }

    Status Fail(const LaunchRequest& request, std::string message) {
        Event event;
        event.type = Event::Type::Failed;
        event.game_id = request.game_id;
        event.message = message;
        events_.push_back(std::move(event));
        return Status::Fail(std::move(message));
    }

    void Finish(std::optional<int> code) {
        link_.reset(); // ends the engine if it somehow still runs
        const double seconds = std::max(0.0, pal::NowSeconds() - started_);
        if (window_hidden_ && hooks_.window) {
            hooks_.window->Show();
        }
        window_hidden_ = false;
        log::Info("engine", "{} ended after {:.0f} s (exit code {})", request_.game_id, seconds,
                  code.value_or(-1));
        if (hooks_.record_play && seconds >= 1.0) {
            hooks_.record_play(request_.game_id, started_unix_, std::llround(seconds));
        }
        Event event;
        event.type = Event::Type::Exited;
        event.game_id = request_.game_id;
        event.exit_code = code.value_or(-1);
        event.seconds = seconds;
        events_.push_back(std::move(event));
    }

    Config config_;
    Hooks hooks_;
    std::unique_ptr<Link> link_;
    LaunchRequest request_;
    double started_ = 0;
    std::int64_t started_unix_ = 0;
    double stop_deadline_ = 0;
    double confirm_until_ = 0;
    double confirm_next_ = 0;
    bool window_hidden_ = false;
    std::vector<Event> events_;
};

} // namespace

std::unique_ptr<Host> CreateHost(const Config& config) {
    return std::make_unique<RealHost>(config);
}

} // namespace den::engine
