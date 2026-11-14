// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Level 0: the engine is an ordinary program started with the game on its command line;
// the session is over when the process ends. An app of its own (Android) is handed the game
// to view instead, and the session is over once this app's window is back in front.

#include "den/base/log.h"
#include "den/platform/apps.h"
#include "den/platform/child.h"
#include "den/platform/engine_apps.h"
#include "den/platform/lifecycle.h"
#include "den/platform/pal.h"
#include "protocol/link.h"

namespace den::engine {

namespace {

class ExitWaitLink final : public Link {
public:
    explicit ExitWaitLink(std::unique_ptr<pal::Child> child) : child_(std::move(child)) {}

    std::optional<int> Exited() override {
        return child_->Poll();
    }
    bool RequestStop() override {
        return child_->RequestClose();
    }
    void ConfirmStop() override {
        child_->ConfirmClose();
    }
    void Kill() override {
        child_->Kill();
    }

private:
    std::unique_ptr<pal::Child> child_;
};

// An app that never covers this one this soon after its start did not take the game.
constexpr double APP_COVER_SECONDS = 60.0;

class AppOpenLink final : public Link {
public:
    std::optional<int> Exited() override {
        if (ended_) {
            return 0;
        }
        if (pal::MainWindowHidden()) {
            covered_ = true;
        } else if (covered_) {
            return 0;
        } else if (pal::NowSeconds() - started_ > APP_COVER_SECONDS) {
            log::Warn("engine", "the app never came to the front");
            return 1;
        }
        return std::nullopt;
    }
    // Another app cannot be asked to close: this one comes back and the session ends.
    bool RequestStop() override {
        Kill();
        return true;
    }
    void Kill() override {
        ended_ = true;
        pal::apps::Raise(pal::apps::Display::Main);
    }

private:
    double started_ = pal::NowSeconds();
    bool covered_ = false;
    bool ended_ = false;
};

} // namespace

Result<std::unique_ptr<Link>> OpenLink(const Install& install, const LaunchRequest& request,
                                       const std::vector<std::string>& args, const LinkContext& context) {
    if (install.level >= 1) {
        return OpenEcpLink(install, request, context);
    }
    if (!install.package.empty()) {
        pal::engine_apps::PlayRequest open;
        open.package = install.package;
        open.cert_sha256 = install.cert_sha256;
        open.content = request.content;
        open.display = pal::apps::Display::Main;
        if (const Status started = pal::engine_apps::Open(open); !started) {
            return started;
        }
        return std::unique_ptr<Link>(new AppOpenLink());
    }
    pal::ChildDesc desc;
    desc.executable = install.executable;
    desc.args = args;
    auto child = pal::StartChild(desc);
    if (!child) {
        return child.status();
    }
    return std::unique_ptr<Link>(new ExitWaitLink(child.take()));
}

} // namespace den::engine
