// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// One running engine session, whatever protocol level drives it. The host only talks to
// this: ExitWaitLink (level0.cpp) or EcpLink (ecp_link.cpp), chosen by OpenLink.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "den/engine/host.h"
#include "ecp/channel.h"

namespace den::engine {

class Link {
public:
    /// Ends the engine if it still runs.
    virtual ~Link() = default;
    /// The exit code once the engine has ended. Never blocks.
    virtual std::optional<int> Exited() = 0;
    /// Asks the engine to end the session and close. False when it could not be asked.
    virtual bool RequestStop() = 0;
    /// Called now and then after RequestStop: answers a "really close?" the engine raised.
    virtual void ConfirmStop() {}
    virtual void Kill() = 0;
    /// Session events beyond start and exit (none at level 0).
    virtual std::vector<Event> Drain() {
        return {};
    }
    /// The protocol controls (level 1 and up).
    virtual engine::Control* GetControl() {
        return nullptr;
    }
    /// When the game was first on screen (pal::NowSeconds); 0 when the link cannot tell.
    virtual double FirstFrameAt() const {
        return 0;
    }
};

/// What a link may need from the app around it.
struct LinkContext {
    std::filesystem::path log_dir; // the engine's standard error goes here
    pal::Window* window = nullptr; // raised for the menu, hidden while the game runs
    std::string app_version;
};

/// Starts `install` for the request at the install's protocol level.
Result<std::unique_ptr<Link>> OpenLink(const Install& install, const LaunchRequest& request,
                                       const std::vector<std::string>& args, const LinkContext& context);

/// The configure fields for an engine: paths on the desktop (`package` empty), content URIs
/// granted to the app's package on Android.
ecp::ConfigureInfo ConfigureInfoFor(const EngineSetup& setup, const std::string& package);

/// Level 1 and up, on the desktop (a child speaking on its standard input and output) or as
/// an app (Android, over a loopback connection).
Result<std::unique_ptr<Link>> OpenEcpLink(const Install& install, const LaunchRequest& request,
                                          const LinkContext& context);

} // namespace den::engine
