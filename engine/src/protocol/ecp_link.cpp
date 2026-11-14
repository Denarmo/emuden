// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Levels 1 and 2: the engine speaks the Emulator Control Protocol. On the desktop it is a
// child started with `--ecp stdio`; as an app (Android) it is started with an intent and
// connects to a loopback listener with the session's token.

#include <system_error>

#include "den/base/files.h"
#include "den/base/log.h"
#include "den/platform/apps.h"
#include "den/platform/child.h"
#include "den/platform/engine_apps.h"
#include "den/platform/pal.h"
#include "ecp/channel.h"
#include "protocol/link.h"

namespace den::engine {

namespace {

// The engine's hello must come this soon after it was started (section 4).
constexpr double HELLO_SECONDS = 10.0;

class EcpLink final : public Link {
public:
    EcpLink(std::unique_ptr<pal::Child> child, std::string package, std::string game_id)
        : child_(std::move(child)), package_(std::move(package)), game_id_(std::move(game_id)),
          started_(pal::NowSeconds()) {}

    ~EcpLink() override {
        // The child goes first, so a read blocked on its pipe returns.
        if (child_) {
            child_->Kill();
        }
        channel_.reset();
        child_.reset();
    }

    void Attach(std::unique_ptr<ecp::Channel> channel, pal::Window* window) {
        channel_ = std::move(channel);
        channel_->on_front = [this, window](bool engine_in_front) {
            if (!package_.empty()) {
                if (engine_in_front) {
                    if (const Status status = pal::engine_apps::Resume(package_, pal::apps::Display::Main); !status) {
                        log::Warn("engine", "the engine app could not be brought back: {}", status.error);
                    }
                } else {
                    // After the engine's menu intent this app is in front already.
                    pal::apps::Raise(pal::apps::Display::Main);
                }
                return;
            }
            if (window != nullptr) {
                if (engine_in_front) {
                    window->Hide();
                } else {
                    window->Raise();
                }
            }
        };
    }

    std::optional<int> Exited() override {
        if (child_) {
            return child_->Poll();
        }
        // An app: the session is over when the channel closed or never opened.
        if (channel_->Closed() || failed_) {
            return channel_->Stopped() && channel_->StopError().empty() ? 0 : 1;
        }
        return std::nullopt;
    }

    bool RequestStop() override {
        if (channel_->RequestStop()) {
            return true;
        }
        if (child_) {
            return child_->RequestClose();
        }
        channel_->Close();
        return false;
    }

    void Kill() override {
        if (child_) {
            child_->Kill();
        }
        channel_->Close();
    }

    std::vector<Event> Drain() override {
        std::vector<Event> events = channel_->Drain(game_id_);
        if (!channel_->HelloDone() && !failed_ && pal::NowSeconds() - started_ > HELLO_SECONDS) {
            log::Warn("engine", "{}: no hello within {:.0f} s", game_id_, HELLO_SECONDS);
            Event event;
            event.type = Event::Type::Failed;
            event.game_id = game_id_;
            event.message = "The engine did not answer.";
            events.push_back(std::move(event));
            failed_ = true;
            Kill();
        }
        for (const Event& event : events) {
            failed_ = failed_ || (event.type == Event::Type::Failed && !channel_->Ready());
        }
        return events;
    }

    engine::Control* GetControl() override {
        return channel_.get();
    }

    double FirstFrameAt() const override {
        return channel_->FirstFrameAt();
    }

private:
    std::unique_ptr<pal::Child> child_;
    std::string package_;
    std::string game_id_;
    double started_;
    bool failed_ = false;
    std::unique_ptr<ecp::Channel> channel_;
};

ecp::LaunchInfo LaunchFor(const LaunchRequest& request, std::string content) {
    ecp::LaunchInfo launch;
    launch.content = std::move(content);
    launch.system = request.system;
    launch.game_id = request.game_id;
    launch.title = request.title;
    launch.locale = request.locale;
    launch.fullscreen = request.fullscreen;
    launch.settings = request.settings;
    // EmuDEN's own way into its menu in a game: Start and Select together.
    launch.menu_chord = {"start", "select"};
    return launch;
}

} // namespace

Result<std::unique_ptr<Link>> OpenEcpLink(const Install& install, const LaunchRequest& request,
                                          const LinkContext& context) {
    if (install.attach) {
        auto listener = pal::ListenLoopback();
        if (!listener) {
            return listener.status();
        }
        ecp::Opening opening;
        opening.listener = listener.take();
        opening.token = ecp::NewToken();
        const int port = opening.listener->Port();
        const std::string token = opening.token;
        auto link = std::make_unique<EcpLink>(nullptr, std::string(), request.game_id);
        link->Attach(std::make_unique<ecp::Channel>(std::move(opening), LaunchFor(request, PathToUtf8(request.content)),
                                                    context.app_version),
                     context.window);
        install.attach(port, token);
        return std::unique_ptr<Link>(std::move(link));
    }
    if (!install.package.empty()) {
        auto listener = pal::ListenLoopback();
        if (!listener) {
            return listener.status();
        }
        ecp::Opening opening;
        opening.listener = listener.take();
        opening.token = ecp::NewToken();
        pal::engine_apps::PlayRequest play;
        play.package = install.package;
        play.cert_sha256 = install.cert_sha256;
        play.content = request.content;
        play.port = opening.listener->Port();
        play.token = opening.token;
        play.session = request.game_id;
        play.fullscreen = request.fullscreen;
        // The game takes this app's display; on a device with two the companion panel stays
        // on the other one.
        play.display = pal::apps::Display::Main;
        auto link = std::make_unique<EcpLink>(nullptr, install.package, request.game_id);
        link->Attach(std::make_unique<ecp::Channel>(std::move(opening), LaunchFor(request, "intent"),
                                                    context.app_version),
                     context.window);
        if (const Status started = pal::engine_apps::Play(play); !started) {
            return started;
        }
        return std::unique_ptr<Link>(std::move(link));
    }

    pal::ChildDesc desc;
    desc.executable = install.executable;
    desc.args = {"--ecp", "stdio"};
    desc.args.insert(desc.args.end(), install.args.begin(), install.args.end());
    desc.channel = true;
    if (!context.log_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(context.log_dir, ec);
        desc.log_file = context.log_dir / PathFromUtf8((install.core_id.empty() ? request.system : install.core_id) + ".log");
    }
    auto child = pal::StartChild(desc);
    if (!child) {
        return child.status();
    }
    pal::Stream* stream = child.value()->Channel();
    auto link = std::make_unique<EcpLink>(child.take(), std::string(), request.game_id);
    ecp::Opening opening;
    opening.stream = stream;
    link->Attach(std::make_unique<ecp::Channel>(std::move(opening), LaunchFor(request, PathToUtf8(request.content)),
                                                context.app_version),
                 context.window);
    return std::unique_ptr<Link>(std::move(link));
}

} // namespace den::engine
